/* The PC's devices as the game drives them (machine.h). The reference for every behaviour is the harness
   (tools/emu/dos.py), which serves the same ports to the original; where the two differ, the port is
   wrong. Time: the CPU is instant, so all time passes at mach_frame(), one frame of the VGA's 70.086 Hz
   at a time; the PIT's counts read between two boundaries are those of the boundary (the retrace's
   start). */
#include "machine.h"
#include "cpu.h"
#include "fntab.h"
#include "mem.h"
#include "platform.h"
#include "sound/sound.h"
#include "vga.h"
#include <stdio.h>
#include <string.h>

#define PIT_HZ 1193182.0
#define FRAME_HZ 70.086
#define BIOS_SEG 0xf000   /* the BIOS's handlers: vector n points at F000:n (as in the harness) */

void dos_int21(void);
void bios_int10(void);
void bios_int16(void);
void bios_int1a(void);
void drv_call(void);
void bios_key_event(uint8_t code);
void dos_reset(void);
void bios_reset(void);

static const MachHooks *hooks;

static struct {
    uint8_t dac[768];
    int dac_w, dac_r;
    uint8_t crtc_idx, crtc[256];
    uint8_t seq_idx, seq[8], gc_idx, gc[16];
    bool attr_flip;
    uint8_t attr_idx, attr[32];
    bool retrace;            /* 3DAh bit 3: set from the boundary until the game waits for its end */
    uint8_t mode;
} vga;

static struct {
    uint32_t div;            /* channel 0's reload value (0 = 65536) */
    int lo;                  /* the low byte of a reload being written, -1 for none */
    double loaded;           /* virtual time (PIT counts) of the last load */
    int latch;               /* the latched count, -1 for none */
    bool read_hi;
} pit;

static struct {
    uint8_t mask;
    bool in_service;
} pic;

static struct {
    uint8_t port60;
} kbd;

static double now;           /* virtual time in PIT counts */
static uint32_t frames;

uint32_t mach_frames(void) { return frames; }
double mach_time(void) { return now; }
uint8_t mach_video_mode(void) { return vga.mode; }
const uint8_t *mach_dac(void) { return vga.dac; }
void mach_set_hooks(const MachHooks *h) { hooks = h; }
bool mach_replay_call(uint32_t addr) { return hooks && hooks->call && hooks->call(addr); }

void mach_reset(void)
{
    memset(&vga, 0, sizeof vga);
    vga.mode = 3;
    memset(&pit, 0, sizeof pit);
    pit.lo = pit.latch = -1;
    pic.mask = 0xb8, pic.in_service = false;
    kbd.port60 = 0;
    now = 0, frames = 0;
    for (int n = 0; n < 256; n++) mww(0, (uint16_t)(n * 4), (uint16_t)n), mww(0, (uint16_t)(n * 4 + 2), BIOS_SEG);
    for (int n = 0; n < 256; n++) mbw(BIOS_SEG, (uint16_t)n, 0xcf);   /* iret */
    dos_reset();
    bios_reset();
    opl2_reset();
    speaker_reset();
}

/* ---- ports ---- */

static uint32_t pit_period(void) { return pit.div ? pit.div : 0x10000; }

static int pit_count(void)
{
    double since = now - pit.loaded;
    uint32_t p = pit_period();
    return (int)(p - (uint32_t)since % p) & 0xffff;
}

uint8_t io_in8(uint16_t port)
{
    if (hooks) return hooks->in8(port);
    switch (port) {
    case 0x3da: vga.attr_flip = false; return vga.retrace ? 0x09 : 0x00;
    case 0x3c2: return 0x00;   /* no retrace interrupt pending: this VGA raises no IRQ 2 */
    case 0x3c9: { uint8_t v = vga.dac[vga.dac_r]; vga.dac_r = (vga.dac_r + 1) % 768; return v; }
    case 0x3d5: return vga.crtc[vga.crtc_idx];
    case 0x3c5: return vga.seq[vga.seq_idx & 7];
    case 0x3cf: return vga.gc[vga.gc_idx & 15];
    case 0x40: {
        if (pit.latch < 0) pit.latch = pit_count(), pit.read_hi = false;
        if (!pit.read_hi) { pit.read_hi = true; return (uint8_t)pit.latch; }
        uint8_t v = (uint8_t)(pit.latch >> 8);
        pit.latch = -1;
        return v;
    }
    case 0x21: return pic.mask;
    case 0x60: return kbd.port60;
    case 0x61: return speaker_port61();   /* as last written (the speaker's gate and data, bits 0-1) */
    case 0x64: return 0x1c;    /* the input buffer empty, the system flag */
    case 0x201: return 0xff;   /* no joystick: the buttons up, the axes timed out */
    case 0x388: return opl2_read_status();
    default: return 0xff;
    }
}

void io_out8(uint16_t port, uint8_t v)
{
    if (hooks) { hooks->out8(port, v); return; }
    switch (port) {
    case 0x3c8: vga.dac_w = v * 3; break;
    case 0x3c7: vga.dac_r = v * 3; break;
    case 0x3c9: vga.dac[vga.dac_w] = v & 0x3f, vga.dac_w = (vga.dac_w + 1) % 768; break;
    case 0x3c4: vga.seq_idx = v & 7; break;
    case 0x3c5: vga.seq[vga.seq_idx & 7] = v; break;
    case 0x3ce: vga.gc_idx = v & 15; break;
    case 0x3cf: vga.gc[vga.gc_idx & 15] = v; break;
    case 0x3d4: vga.crtc_idx = v; break;
    case 0x3d5: vga.crtc[vga.crtc_idx] = v; break;
    case 0x3c0:
        if (!vga.attr_flip) vga.attr_idx = v;
        else vga.attr[vga.attr_idx & 31] = v;
        vga.attr_flip = !vga.attr_flip;
        break;
    case 0x43:
        if ((v >> 6) == 2) { speaker_out(port, v); break; }   /* channel 2: the speaker's */
        if (((v >> 4) & 3) == 0) {          /* latch */
            if (v >> 6 == 0) pit.latch = pit_count(), pit.read_hi = false;
        } else if (v >> 6 == 0) pit.lo = -1;
        break;
    case 0x40:
        if (pit.lo < 0) pit.lo = v;
        else pit.div = (uint32_t)(v << 8 | pit.lo), pit.lo = -1, pit.loaded = now;
        break;
    case 0x20:
        if (v == 0x20 || (v >= 0x60 && v <= 0x67)) pic.in_service = false;   /* (specific) end of interrupt */
        break;
    case 0x21: pic.mask = v; break;
    case 0x388: opl2_write_index(v); break;   /* the AdLib (src/sound/opl2.c) */
    case 0x389: opl2_write_data(v); break;
    case 0x42: case 0x61: speaker_out(port, v); break;     /* the PC speaker (src/sound/speaker.c) */
    default: break;
    }
}

void io_out16(uint16_t port, uint16_t v)
{
    io_out8(port, (uint8_t)v);
    io_out8((uint16_t)(port + 1), (uint8_t)(v >> 8));
}

/* ---- interrupts ---- */

void mach_int(uint8_t n)
{
    if (hooks) { hooks->intr(n); return; }
    switch (n) {
    case 0x21: dos_int21(); break;
    case 0x10: bios_int10(); break;
    case 0x16: bios_int16(); break;
    case 0x1a: bios_int1a(); break;
    case 0x33: R.ax = 0; break;                       /* no mouse driver */
    case 0x03: break;   /* a breakpoint left in DRIVER1 (no free voice): DOS's handler only returns */
    case 0x15: R.cf = true, SET_AH(0x86); break;      /* no BIOS extensions */
    default: {
        char m[80];
        snprintf(m, sizeof m, "machine: int %02xh ah=%02x ignored", n, AH);
        plat_log(m);
    }
    }
}

void mach_drv(void)
{
    if (hooks) { hooks->drv(); return; }
    drv_call();
}

/* The BIOS's own handler for vector n (F000:n): the timer counts the day's ticks at 0040:006C. */
static void bios_handler(uint8_t n)
{
    if (n == 8) {
        uint32_t t = (md(SEG_BIOS, 0x6c) + 1) % 0x1800b0;
        mdw(SEG_BIOS, 0x6c, t);
    }
    if (n == 8 || n == 9) pic.in_service = false;
}

void mach_call_far(uint16_t seg, uint16_t off)
{
    if (seg == BIOS_SEG && off < 256) { bios_handler((uint8_t)off); return; }
    const FnEntry *e = fntab_find((uint32_t)seg << 16 | off);
    if (e) { e->fn(); return; }
    char m[80];
    snprintf(m, sizeof m, "machine: far call to %04x:%04x, no routine there", seg, off);
    plat_log(m);
}

/* A hardware interrupt through the vector table, as the CPU takes it: the handler's registers are its
   own (it saves and restores what it uses; R is put back regardless, as iret leaves the interrupted
   code's state). */
static void deliver(uint8_t vec)
{
    Cpu saved = R;
    pic.in_service = true;
    mach_call_far(mw(0, (uint16_t)(vec * 4 + 2)), mw(0, (uint16_t)(vec * 4)));
    R = saved;
}

/* ---- time ---- */

static uint8_t screen[320 * 200];
static uint8_t pal8[768];

static void present(void)
{
    for (int i = 0; i < 768; i++) pal8[i] = (uint8_t)(vga.dac[i] << 2 | vga.dac[i] >> 4);
    if (vga.mode == 0x13) memcpy(screen, mem_ptr(SEG_VGA, 0), sizeof screen);
    else memset(screen, 0, sizeof screen);   /* the text and planar modes: the code card's (FONT.BIN) */
    plat_present(screen, 320, 200, 320, pal8);
}

void mach_frame(void)
{
    present();
    plat_wait_frame();
    frames++;
    now = frames * (PIT_HZ / FRAME_HZ);
    vga.retrace = true;
    /* the timer: expired since its last load (the game's handler keeps it just short of a frame) */
    bool irq0 = now - pit.loaded >= pit_period();
    if (irq0) pit.loaded += pit_period() * (double)(uint64_t)((now - pit.loaded) / pit_period());
    if (irq0 && !(pic.mask & 1) && !pic.in_service) deliver(8);
    uint16_t keys[32];
    int n = plat_key_events(keys, 32);
    bool bios_kbd = mw(0, 9 * 4 + 2) == BIOS_SEG;
    for (int i = 0; i < n; i++) {
        uint16_t k = keys[i];
        if (bios_kbd) { bios_key_event((uint8_t)k); continue; }
        if (k & 0x100) {   /* an extended key: the E0 prefix first, its own interrupt */
            kbd.port60 = 0xe0;
            if (!(pic.mask & 2) && !pic.in_service) deliver(9);
        }
        kbd.port60 = (uint8_t)k;
        if (!(pic.mask & 2) && !pic.in_service) deliver(9);
    }
}

/* The original's waits on 3DAh: for the retrace's start (a frame boundary unless one just passed) and
   for its end. The loops themselves, when the ports are replayed. */
void vga_wait_retrace_start(void)
{
    if (hooks) { while (!(io_in8(0x3da) & 8)) {} return; }
    if (!vga.retrace) mach_frame();
}

void vga_wait_retrace_end(void)
{
    if (hooks) { while (io_in8(0x3da) & 8) {} return; }
    vga.retrace = false;
}

/* int 10h's view of the devices (bios.c). */
void mach_set_mode(uint8_t mode)
{
    vga.mode = mode;
    vga.dac_w = vga.dac_r = 0;
}
uint8_t *mach_dac_rw(void) { return vga.dac; }
