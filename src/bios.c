/* The BIOS services the game calls (int 10h, 16h, 1Ah), as the reference harness serves them
   (tools/emu/dos.py). The keyboard buffer is the BIOS's while its own IRQ 1 handler is installed (the
   code card, FONT.BIN, reads keys through int 16h before the game installs its handler). */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "vga_planar.h"
#include <stdio.h>
#include <string.h>

double mach_time(void);
void mach_set_mode(uint8_t mode, bool clear);
uint8_t *mach_dac_rw(void);
uint8_t *mach_attr_rw(void);

static uint16_t keys[32];   /* scan << 8 | ascii */
static int nkeys;

void bios_reset(void) { nkeys = 0; }

/* A key from the BIOS's IRQ 1 handler: makes only, with the ASCII code of the few keys the code card
   uses (as the harness). */
void bios_key_event(uint8_t code)
{
    if (code & 0x80 || nkeys == 32) return;
    uint8_t asc = code == 0x1c ? 0x0d : code == 0x01 ? 0x1b : code == 0x39 ? 0x20 : code == 0x0e ? 0x08 : code == 0x0f ? 0x09 : 0;
    keys[nkeys++] = (uint16_t)(code << 8 | asc);
}

/* Write string (AH = 13h) in mode 10h, as the VGA BIOS does it: the characters of CX at ES:BP (with AL
   bit 1, character and attribute pairs) from row DH, column DL of page BH, in the attribute BL (bit 7:
   XOR); bell, backspace, line feed and carriage return act as on a teletype, the line wraps at 80
   columns and the page scrolls past row 24; AL bit 0 leaves the cursor (0040:0050 + 2 * page) after the
   text. The cells are 8 x 14 of the port's own font (font8x14.h), not IBM's. Other modes: logged only. */
static void write_string(void)
{
    char m[160], t[128];
    int n = R.cx < 127 ? R.cx : 127;
    for (int i = 0; i < n; i++) t[i] = (char)mb(R.es, (uint16_t)(R.bp + (AL & 2 ? 2 * i : i)));
    t[n] = 0;
    snprintf(m, sizeof m, "bios: text at row %d col %d: %s", DH, DL, t);
    plat_log(m);
    if (mach_video_mode() != 0x10) return;
    int row = DH, col = DL;
    uint8_t page = BH;
    uint16_t p = R.bp;
    for (uint16_t i = 0; i < R.cx; i++) {
        uint8_t ch = mb(R.es, p++), attr = BL;
        if (AL & 2) attr = mb(R.es, p++);
        if (ch == 0x07) continue;
        if (ch == 0x08) { if (col) col--; continue; }
        if (ch == 0x0d) { col = 0; continue; }
        if (ch != 0x0a) {
            vga_planar_char(ch, attr, page, row, col);
            if (++col < 80) continue;
            col = 0;
        }
        if (++row > 24) vga_planar_scroll(page, 0, 24), row = 24;
    }
    if (AL & 1) mbw(SEG_BIOS, (uint16_t)(0x50 + 2 * (page & 7)), (uint8_t)col), mbw(SEG_BIOS, (uint16_t)(0x51 + 2 * (page & 7)), (uint8_t)row);
}

void bios_int10(void)
{
    uint8_t al = AL;
    uint8_t *dac = mach_dac_rw();
    if (AH == 0x13) { write_string(); return; }
    switch (AH) {
    case 0x00: {
        uint8_t mode = al & 0x7f;
        mach_set_mode(mode, !(al & 0x80));
        mbw(SEG_BIOS, 0x49, mode);
        if (!(al & 0x80)) memset(mem_ptr(SEG_VGA, 0), 0, 0x10000);
        break;
    }
    case 0x1a:                   /* the display combination: a VGA with a colour monitor */
        if (al == 0) SET_AL(0x1a), R.bx = 0x0008;
        break;
    case 0x0f:
        R.ax = (uint16_t)((mach_video_mode() == 0x13 ? 40 : 80) << 8 | mach_video_mode());
        R.bx &= 0x00ff;
        break;
    case 0x10:
        if (al == 0x00) {        /* one attribute (palette) register: BL = BH */
            if (BL < 0x15) mach_attr_rw()[BL] = BH;
        } else if (al == 0x12) {        /* a block of DAC registers from ES:DX */
            for (int i = 0; i < 3 * R.cx && R.bx * 3 + i < 768; i++) dac[R.bx * 3 + i] = mb(R.es, (uint16_t)(R.dx + i)) & 0x3f;
        } else if (al == 0x10) { /* one DAC register */
            int i = (R.bx & 0xff) * 3;
            dac[i] = DH & 0x3f, dac[i + 1] = CH & 0x3f, dac[i + 2] = CL & 0x3f;
        } else if (al == 0x17) { /* a block into ES:DX */
            for (int i = 0; i < 3 * R.cx && R.bx * 3 + i < 768; i++) mbw(R.es, (uint16_t)(R.dx + i), dac[R.bx * 3 + i]);
        }
        break;
    default: break;              /* scrolling, characters: only in text mode */
    }
}

void bios_int16(void)
{
    switch (AH) {
    case 0x01: case 0x11:
        if (nkeys) R.ax = keys[0], R.zf = false;
        else R.zf = true;
        break;
    case 0x00: case 0x10:
        while (!nkeys) mach_frame();   /* the BIOS waits for a key */
        R.ax = keys[0];
        memmove(keys, keys + 1, (size_t)--nkeys * sizeof keys[0]);
        break;
    case 0x03: break;                  /* the typematic rate */
    default: break;
    }
}

void bios_int1a(void)
{
    switch (AH) {
    case 0x00: {
        uint32_t t = (uint32_t)(mach_time() / 65536);
        R.cx = (uint16_t)(t >> 16), R.dx = (uint16_t)t, R.ax = 0;
        break;
    }
    case 0x02: R.cx = 0, R.dx = 0, R.cf = false; break;           /* the RTC: 00:00:00 */
    case 0x04: R.cx = 0x1994, R.dx = 0x0914, R.cf = false; break; /* 1994-09-14 */
    default: break;
    }
}
