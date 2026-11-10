/* SM.EXE, the Codemasters splash MICRO.COM runs before the game (docs/re/launcher.md): the company's
   logo built up in mode 13h from GFX1.GFX, a highlight swept across it, the words "(c) Codemasters
   1994." in ANTIFONT.BIN's letters, then about four seconds of rest. A small real-mode program (code
   0788-0dbb, its data in the same segment: CS = DS = ES = 1000 here, loaded as DOS loads it), ported
   routine by routine in its own words; addresses SM:offset (the segment's offset, image at 1000h).
   The pictures, the palette, the font's widths and the strings are read from the loaded file in g_mem.

   Variables (cs:): 0002/0004 the two 64 KB blocks it allocates (GFX1.GFX, ANTIFONT.BIN), 0362 the last
   scan code (its own IRQ 1 handler), 0364 the screen's segment (A000), 0366 the pitch (320), 036a the
   mode (13h), 036c the palette (256 x RGB); 02be the logo's frame offset (0..292h by 0Eh; the frames at
   001e), 02c0 where the two halves meet, 02c4/02c5 'A' and 'B' held, 02c6.. a blit record (x, y,
   width, height, segment, source; 02d6, 02e6, 02f6 more of them), 066c.. the build's date and time
   digits, 0680 the build string, 06a2 the copyright line, 06b8 done, 06ba the rest's frame count,
   06bc no VGA, 06be no mouse, 06bf/06c1 the build string's place, 06c3 the halves have met, 06c5 the
   build string shown, 06c7/06c9 the highlight's place, 06cb its length, 06cd the sweep done, 06cf the
   font's width table (character, width pairs, FFh-ended).

   Checked against the original as a whole (tools/emu/run.py --exe SM.EXE vs the port's frames, frame
   by frame), not routine by routine: its routines are not in names.tsv (their addresses are those of
   MICRO.EXE's). Dead in the file: 0ad8 (clears the build string's band), 0c78 (waits for a key),
   0d1a (clears the screen); not ported. */
#include "sm.h"
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "vga.h"

static uint8_t b(uint16_t o) { return mb(SEG_CODE, o); }
static uint16_t w(uint16_t o) { return mw(SEG_CODE, o); }
static void bw(uint16_t o, uint8_t v) { mbw(SEG_CODE, o, v); }
static void ww(uint16_t o, uint16_t v) { mww(SEG_CODE, o, v); }

/* Sm_Clip SM:0c24
   CX = x, BX = the width: the width left on the 320-pixel line (BX = 0: nothing), AX = the source
   columns to skip, CX = the first column drawn. */
static void Sm_Clip(void)
{
    if (R.cx < 0x140) {
        uint16_t room = (uint16_t)(0x140 - R.cx);
        if (R.bx >= room) R.bx = room;
        R.ax = 0;
    } else if (R.cx & 0x8000) {   /* left of the screen */
        R.bx = (uint16_t)(R.bx + R.cx);
        if (R.bx & 0x8000) { R.bx = 0; return; }
        R.ax = (uint16_t)-R.cx;
        R.cx = 0;
    } else {
        R.bx = 0;
    }
}

/* Sm_Blit SM:0ccc
   The record at SI: x, y, width, height, the picture's segment, its offset. Rows of the width (in
   words: an odd byte is dropped; a width of 1 copies 65536 words, as the original's loop would) from
   the picture to the screen, clipped left and right. */
static void Sm_Blit(void)
{
    uint16_t rec = R.si;
    R.cx = w(rec), R.dx = w((uint16_t)(rec + 2)), R.bx = w((uint16_t)(rec + 4));
    Sm_Clip();
    if (R.bx == 0) return;
    R.di = (uint16_t)(w(0x366) * R.dx + R.cx);   /* Sm_ScreenOffset SM:0c54 */
    uint16_t stride = w((uint16_t)(rec + 4)), rows = w((uint16_t)(rec + 6));
    uint16_t src_seg = w((uint16_t)(rec + 8)), src = (uint16_t)(w((uint16_t)(rec + 0x0a)) + R.ax);
    uint16_t dst_seg = w(0x364);
    uint16_t di = R.di;
    for (; rows; rows--) {
        uint16_t n = (uint16_t)(R.bx >> 1), s = src, d = di;
        do {
            mww(dst_seg, d, mw(src_seg, s));
            d = (uint16_t)(d + 2), s = (uint16_t)(s + 2);
        } while (--n);
        di = (uint16_t)(di + 0x140);
        src = (uint16_t)(src + stride);
    }
}

/* The width of character c in the font's table (SM:06cf), and its index; false if it is not there. */
static bool glyph(uint8_t c, uint16_t *width, uint16_t *index)
{
    uint16_t di = 0x6cf, n = 0;
    for (;;) {
        if (b(di) == c) {
            *width = b((uint16_t)(di + 1)), *index = n;
            return true;
        }
        di = (uint16_t)(di + 2), n++;
        if (b((uint16_t)(di + 1)) == 0xff) return false;
    }
}

/* A string's width in the font, for centring it (the loops at SM:0855, 08ef). */
static uint16_t text_width(uint16_t s)
{
    uint16_t total = 0, wd, ix;
    for (; b(s); s++)
        if (glyph(b(s), &wd, &ix)) total = (uint16_t)(total + wd);
    return total;
}

/* A string at s from the blit record at 02c6 (x, y already set): each character's cell (11 bytes wide,
   8Fh bytes apart in ANTIFONT.BIN) drawn, then x moved on by its width; characters not in the table
   skipped (the second loops of SM:0855 and 0af9). */
static void draw_text(uint16_t s)
{
    for (; b(s); s++) {
        uint16_t wd, ix;
        if (!glyph(b(s), &wd, &ix)) continue;
        ww(0x2ca, 0xb);
        ww(0x2d0, (uint16_t)(0x8f * ix));
        R.si = 0x2c6;
        Sm_Blit();
        ww(0x2c6, (uint16_t)(w(0x2c6) + wd));
    }
}

/* Sm_Load SM:0d63
   BX = the file's name, CX = the bytes, AX:DX = where: opened read/write (AL = 2), read, closed; no
   error check (a missing file reads nothing into the block). */
static void Sm_Load(uint16_t name, uint16_t bytes, uint16_t seg)
{
    R.dx = name, R.ax = 0x3d02;
    mach_int(0x21);
    R.bx = R.ax;
    uint16_t h = R.bx;
    R.cx = bytes, R.ds = seg, R.dx = 0;
    R.ax = 0x3f00;
    mach_int(0x21);
    R.bx = h, R.ax = 0x3e00;
    mach_int(0x21);
    R.ds = SEG_CODE;
}

/* Sm_KeyIrq SM:0cbf: IRQ 1, the scan code into cs:0362, acknowledged. */
void Sm_KeyIrq(void)
{
    bw(0x362, io_in8(0x60));
    io_out8(0x20, 0x20);
}

/* Sm_Init SM:07f1: the animation's variables. */
static void Sm_Init(void)
{
    static const uint16_t zero[] = {0x2be, 0x2c0, 0x2c2, 0x6b8, 0x6ba, 0x6bf, 0x6c1, 0x6c3, 0x6c7, 0x6c9, 0x6cb, 0x6cd};
    for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++) ww(zero[i], 0);
    bw(0x2c4, 0), bw(0x2c5, 0);
    ww(0x6c5, 0);
    ww(0x2c0, 0x48);
    bw(0x6be, 0);
}

/* Sm_DrawCopyright SM:0855: the copyright line, centred, at y = B4h. */
static void Sm_DrawCopyright(void)
{
    ww(0x2c6, (uint16_t)((uint16_t)(w(0x366) - text_width(0x6a2)) >> 1));
    ww(0x2c8, 0xb4);
    draw_text(0x6a2);
}

/* Sm_FormatBuild SM:08ef: the build's date and time digits into the build string, and its centred
   place (y = A0h). */
static void Sm_FormatBuild(void)
{
    static const uint8_t at[7][2] = {{3, 5}, {0xb, 0xd}, {0x12, 0x13}, {0x15, 0x16}, {0x18, 0x19}, {0x1b, 0x1c}, {0x1e, 0x1f}};
    static const uint16_t from[7] = {0x66c, 0x670, 0x674, 0x676, 0x678, 0x67a, 0x67c};
    for (int i = 0; i < 7; i++) {
        uint16_t v = w(from[i]);
        bw((uint16_t)(0x680 + at[i][0]), (uint8_t)v);
        bw((uint16_t)(0x680 + at[i][1]), (uint8_t)(v >> 8));
    }
    ww(0x6bf, (uint16_t)((uint16_t)(w(0x366) - text_width(0x680)) >> 1));
    ww(0x6c1, 0xa0);
}

/* Sm_ShowBuild SM:0af9: the build string, once ([06c5]), where 08ef put it; the rest's count restarts. */
static void Sm_ShowBuild(void)
{
    ww(0x6ba, 0);
    if (w(0x6c5) != 0) return;
    ww(0x2c6, w(0x6bf));
    ww(0x2c8, w(0x6c1));
    draw_text(0x680);
    ww(0x6c5, 1);
}

/* Sm_CheckBuildKeys SM:0ac6: 'A' and 'B' held together show the build string. */
static void Sm_CheckBuildKeys(void)
{
    if ((uint8_t)(b(0x2c4) + b(0x2c5)) == 2) Sm_ShowBuild();
}

/* Sm_TrackKeys SM:09d0: 'A' and 'B' pressed and released, from the last scan code. */
static void Sm_TrackKeys(void)
{
    uint8_t c = b(0x362);
    if (c == 0x9e) bw(0x2c4, 0);
    if (c == 0xb0) bw(0x2c5, 0);
    if (c == 0x1e) bw(0x2c4, 1);
    if (c == 0x30) bw(0x2c5, 1);
}

/* Sm_CountRest SM:0aac: after the sweep, FAh frames, then done. */
static void Sm_CountRest(void)
{
    if (w(0x6cd) == 0) return;
    ww(0x6ba, (uint16_t)(w(0x6ba) + 1));
    if (w(0x6ba) == 0xfa) ww(0x6b8, 0xffff);
}

/* Sm_StepLogo SM:0bb3: the logo's next frame (a record of the table at 001e + [02be]) drawn. */
static void Sm_StepLogo(void)
{
    uint16_t di = (uint16_t)(0x1e + w(0x2be));
    ww(0x2d6, (uint16_t)(w(di) + w((uint16_t)(di + 2))));
    ww(0x2d8, w((uint16_t)(di + 6)));
    ww(0x2e0, w((uint16_t)(di + 8)));
    ww(0x2da, w((uint16_t)(di + 0x0a)));
    ww(0x2dc, w((uint16_t)(di + 0x0c)));
    R.si = 0x2d6;
    Sm_Blit();
    if (w(0x2be) != 0x292) ww(0x2be, (uint16_t)(w(0x2be) + 0xe));
}

/* Sm_StepHalves SM:0b83: the two halves of the words slide in, 8 pixels a frame, until they meet. */
static void Sm_StepHalves(void)
{
    if (w(0x6c3) != 0) return;
    ww(0x2e6, (uint16_t)(w(0x2e6) + 8));
    ww(0x2f6, (uint16_t)(w(0x2f6) - 8));
    R.si = 0x2e6;
    Sm_Blit();
    R.si = 0x2f6;
    Sm_Blit();
    if (w(0x2c0) == w(0x2e6)) ww(0x6c3, 0xff);
}

/* The highlight's band: 70 rows of 8 pixels from (x, y), each row one pixel further left (a slant),
   their colours moved by d (SM:0a48 +10h, SM:09f8 -10h). */
static void band(uint16_t x, uint16_t y, uint8_t d)
{
    for (uint16_t n = 0x46; n; n--) {
        uint16_t o = (uint16_t)(0x140 * y + x);
        for (int i = 0; i < 8; i++, o++) mbw(w(0x364), o, (uint8_t)(mb(w(0x364), o) + d));
        y++, x--;
    }
}

/* Sm_SweepLight SM:0a48: the band brightened at the highlight's place, the highlight moved 8 on; at
   x = 138h the sweep is done. */
static void Sm_SweepLight(void)
{
    band(w(0x6c7), w(0x6c9), 0x10);
    if (w(0x6cb) != 8) ww(0x6cb, (uint16_t)(w(0x6cb) + 1));
    ww(0x6c7, (uint16_t)(w(0x6c7) + 8));
    if (w(0x6c7) == 0x138) ww(0x6cd, 0xffff);
}

/* Sm_SweepDark SM:09f8: once the highlight is six steps long, the band 20h behind it darkened back. */
static void Sm_SweepDark(void)
{
    if (w(0x6cb) < 6) return;
    band((uint16_t)(w(0x6c7) - 0x20), w(0x6c9), 0xf0);
}

/* Sm_Loop SM:097f: a frame, then the retrace's end; a mouse button (with a mouse) or the rest's end
   ends it; else the retrace's start and the next frame. */
static void Sm_Loop(void)
{
    for (;;) {
        Sm_CountRest();
        Sm_StepLogo();
        Sm_StepHalves();
        Sm_CheckBuildKeys();
        Sm_TrackKeys();
        if (w(0x6c3) != 0 && w(0x6cd) == 0) {
            Sm_SweepLight();
            Sm_SweepDark();
        }
        R.dx = 0x3da;
        vga_wait_retrace_end();
        if (b(0x6be) == 0) {
            R.ax = 3;
            mach_int(0x33);
            if (R.bx != 0) return;
        }
        if (w(0x6b8) != 0) return;
        vga_wait_retrace_start();
    }
}

/* Sm_Start SM:0788: the blocks, the files, IRQ 1, the screen (VGA only: int 10h 1A00h), the
   palette; the animation; the blocks freed, IRQ 1 back; mode 0Eh (as the original leaves it) and
   the exit, with AL = 0Eh. */
void Sm_Start(void)
{
    uint16_t ds = R.ds, es = R.es;
    R.ds = R.es = SEG_CODE;
    /* Sm_Alloc SM:0d80, 0d9e: two blocks of FA0h paragraphs; only the second's result is tested (as
       the original: a first that fails leaves cs:0002 at 0) */
    R.ax = 0x4800, R.bx = 0xfa0;
    mach_int(0x21);
    if (!R.cf) ww(2, R.ax);
    R.ax = 0x4800, R.bx = 0xfa0;
    mach_int(0x21);
    bool ok = !R.cf;
    if (ok) ww(4, R.ax);
    if (ok) {
        Sm_Load(6, 0xd401, w(2));        /* Sm_LoadGfx SM:0bef: GFX1.GFX (the name at 0006) */
        uint16_t g = w(2);
        ww(0x2de, g), ww(0x2ee, g), ww(0x2fe, g), ww(0x30e, g);
        Sm_Load(0x10, 0x32d5, w(4));     /* Sm_LoadFont SM:0c0e: ANTIFONT.BIN */
        ww(0x2ce, w(4));
        /* Sm_Setup SM:0c6b: IRQ 1 saved and taken (0c8b, 0c9e), the VGA's check and mode (0d46), the
           palette (0d32) */
        R.ax = 0x3509;
        mach_int(0x21);
        ww(0x35e, R.es), ww(0x360, R.bx);
        R.es = SEG_CODE;
        R.ax = 0x2509, R.dx = 0xcbf;
        mach_int(0x21);
        R.ax = 0x1a00;
        mach_int(0x10);
        if (AL != 0x1a || BL < 7) ww(0x6bc, 1);
        else R.ax = b(0x36a), mach_int(0x10);
        R.ax = 0x1012, R.bx = 0, R.cx = 0x100, R.dx = 0x36c;
        mach_int(0x10);
        if (w(0x6bc) == 0) {
            Sm_Init();
            Sm_DrawCopyright();
            Sm_FormatBuild();
            R.ax = 0;                    /* Sm_MouseCheck SM:07da */
            mach_int(0x33);
            bw(0x6be, R.ax == 0 ? 1 : 0);
            ww(0x6c7, 0x48), ww(0x6c9, 0x50);
            Sm_Loop();
        }
        R.ax = 0x4900, R.es = w(2);      /* Sm_Free SM:0d93, 0db1 */
        mach_int(0x21);
        R.ax = 0x4900, R.es = w(4);
        mach_int(0x21);
        R.es = SEG_CODE;
        R.ax = 0x2509, R.dx = w(0x360), R.ds = w(0x35e);   /* Sm_Restore SM:0cac */
        mach_int(0x21);
        R.ds = SEG_CODE;
    }
    R.ds = ds, R.es = es;
    R.ax = 0x000e;
    mach_int(0x10);
    SET_AH(0x4c);
    mach_int(0x21);
}
