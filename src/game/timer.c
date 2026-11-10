/* The timer (docs/re/functions.md, "timer"). The program locks its IRQ 0 to the VGA's vertical retrace:
   at start-up it measures a frame in PIT counts between two retrace starts, and on every tick its
   handler waits for the retrace, then reloads the PIT with the frame less 96h counts, so that the next
   tick comes just before the next retrace. The handler counts frames for the rest of the program and
   chains to the BIOS's handler (so the DOS clock runs at the frame rate: the exit sets the clock back,
   1000:49af). A VGA that raises IRQ 2 at the retrace makes all this unnecessary: the program tests for
   one (1000:4a21, 1000:4a94) and, if it fires, the timer handler only counts.

   Data: 193c:2610 a rate factor (12_3428h / the frame), 2612 the frame in counts, 2614 the reload,
   260c/260e the BIOS's IRQ 0 vector, 2618 1 once installed, 2925/2927 the BIOS's IRQ 1 vector;
   cs:4a8d the CRTC's register 11h as found, cs:4a8e/4a90 the old IRQ 2 vector, cs:4a92 1 once the
   retrace interrupt has fired, cs:4ade a tick counter (FFh after the IRQ 2 test is armed). */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"
#include "vga.h"

/* Timer_Irq 1000:489c
   Counters, once per tick (once per frame): words 193c:28f7, 193c:0002 and 193c:261f; the byte 26d0 runs
   0..1fh and toggles 26cf at each wrap (a blink every 32 frames; signed compare, as the original). The
   sound driver's tick (AH = 3) while cs:3279 is set. */
void Timer_Irq(void)
{
    Cpu saved = R;
    R.ds = SEG_DATA;
    R.ax = SEG_DATA;
    if (c16(0x4a92) != 1) {
        io_out8(0x43, 0x34);   /* channel 0, rate generator, a full count until the retrace */
        io_out8(0x40, 0xff);
        io_out8(0x40, 0xff);
        R.dx = 0x3da;
        vga_wait_retrace_start();
        io_out8(0x43, 0x34);
        uint16_t reload = d16(0x2614);
        io_out8(0x40, (uint8_t)reload);
        io_out8(0x40, (uint8_t)(reload >> 8));
        R.ax = (uint16_t)(reload & 0xff00) | (reload >> 8);
        c8w(0x4ade, (uint8_t)(c8(0x4ade) + 1));
    }
    d16w(0x28f7, (uint16_t)(d16(0x28f7) + 1));
    d16w(0x0002, (uint16_t)(d16(0x0002) + 1));
    d16w(0x261f, (uint16_t)(d16(0x261f) + 1));
    d8w(0x26d0, (uint8_t)(d8(0x26d0) + 1));
    if ((int8_t)d8(0x26d0) >= 0x20) {
        d8w(0x26cf, d8(0x26cf) ^ 1);
        d8w(0x26d0, 0);
    }
    if (c8(0x3279) != 0) {
        SET_AH(3);
        mach_drv();
    }
    mach_call_far(d16(0x260e), d16(0x260c));   /* the BIOS's handler: the day's tick count */
    io_out8(0x20, 0x20);
    R = saved;
}

/* Timer_Measure 1000:485b
   The frame in PIT counts: the counter free-running from 0 (65536), read at two retrace starts a frame
   apart. Returns AX = CX = the difference, DX = the second count; flags as the subtraction leaves them. */
void Timer_Measure(void)
{
    io_out8(0x43, 0x34);
    io_out8(0x40, 0);
    io_out8(0x40, 0);
    R.dx = 0x3da;
    vga_wait_retrace_end();
    vga_wait_retrace_start();
    io_out8(0x43, 0);          /* latch channel 0 */
    uint8_t lo = io_in8(0x40);
    uint8_t hi = io_in8(0x40);
    uint16_t first = (uint16_t)(hi << 8 | lo);
    vga_wait_retrace_end();
    vga_wait_retrace_start();
    io_out8(0x43, 0);
    lo = io_in8(0x40);
    hi = io_in8(0x40);
    uint16_t second = (uint16_t)(hi << 8 | lo);
    R.dx = second;
    R.cf = first < second;
    R.cx = (uint16_t)(first - second);
    flags_zs16(R.cx);
    R.ax = R.cx;
}

/* Timer_Install 1000:490d
   Once ([2618]): measures the frame, sets the reload to it less 96h, the rate factor, saves the BIOS's
   IRQ 0 vector and installs Timer_Irq, then starts the PIT on the reload. AX = 1 if already done. */
void Timer_Install(void)
{
    R.ax = 1;   /* (BP: the original's frame, push bp / mov bp, sp, unused) */
    if (d16(0x2618) == 1) return;
    Timer_Measure();
    d16w(0x2612, R.ax);
    R.ax = (uint16_t)(R.ax - 0x96);
    d16w(0x2614, R.ax);
    /* idiv: 12_3428h / the frame, signed; a frame of 0 or one whose quotient overflows would fault (it
       cannot happen with a VGA's 70 Hz) */
    int32_t num = 0x123428;
    int16_t den = (int16_t)d16(0x2612);
    if (den != 0 && num / den >= -32768 && num / den <= 32767) {
        R.ax = (uint16_t)(int16_t)(num / den);
        R.dx = (uint16_t)(int16_t)(num % den);
    }
    d16w(0x2610, R.ax);
    R.ax = 0x3508;
    mach_int(0x21);
    R.ax = R.es;
    d16w(0x260c, R.bx);
    d16w(0x260e, R.ax);
    d16w(0x2618, 1);
    R.ax = 0x2508;
    uint16_t ds = R.ds;
    R.dx = SEG_CODE;
    R.ds = SEG_CODE;
    R.dx = 0x489c;
    mach_int(0x21);
    R.ds = ds;
    io_out8(0x43, 0x34);
    R.ax = d16(0x2614);
    io_out8(0x40, AL);
    SET_AL(AH);
    io_out8(0x40, AL);
}

/* Timer_Remove 1000:4966
   The BIOS's IRQ 0 vector back and the PIT at its 18.2 Hz. [2618] stays 1 (as the original). */
void Timer_Remove(void)
{
    if (d16(0x2618) == 0) return;
    R.dx = d16(0x260c);
    R.ax = d16(0x260e);
    uint16_t ds = R.ds;
    R.ds = R.ax;
    R.ax = 0x2508;
    mach_int(0x21);
    R.ds = ds;
    io_out8(0x43, 0x34);
    SET_AL(0);
    io_out8(0x40, 0);
    io_out8(0x40, 0);
}

/* Irq_Install 1000:498a
   The timer, the retrace interrupt's test, then the keyboard: the BIOS's IRQ 1 vector saved, Key_Irq
   installed. ES kept. */
void Irq_Install(void)
{
    uint16_t es = R.es;
    Timer_Install();
    Retrace_IrqInstall();
    R.ax = 0x3509;
    mach_int(0x21);
    R.ax = R.es;
    d16w(0x2925, R.bx);
    d16w(0x2927, R.ax);
    uint16_t ds = R.ds;
    R.ax = 0x2509;
    R.dx = 0x2efd;
    R.ds = SEG_CODE;
    mach_int(0x21);
    R.ds = ds;
    R.es = es;
}

/* Retrace_IrqInstall 1000:4a21
   Arms the VGA's vertical retrace interrupt: CRTC register 11h saved in cs:4a8d, the old IRQ 2 vector
   saved and Retrace_Irq installed, the PIC's mask rewritten unchanged, then register 11h with "clear
   the retrace interrupt" (bit 4 low) and again with bit 4 high and the interrupt enabled (bit 5 low).
   cs:4ade = FFh. ES is left at the old vector's segment. */
void Retrace_IrqInstall(void)
{
    uint16_t ds = R.ds;
    R.dx = 0x3d4;
    SET_AL(0x11);
    io_out8(R.dx, AL);
    R.dx++;
    SET_AL(io_in8(R.dx));
    c8w(0x4a8d, AL);
    R.ax = 0x350a;
    mach_int(0x21);
    R.ax = R.es;
    c16w(0x4a8e, R.bx);
    c16w(0x4a90, R.ax);
    uint16_t ds2 = R.ds;
    R.dx = 0x4a94;
    R.ds = SEG_CODE;
    R.ax = 0x250a;
    mach_int(0x21);
    R.ds = ds2;
    R.dx = 0x21;
    SET_AL(io_in8(R.dx));
    io_out8(R.dx, AL);
    R.dx = 0x3d4;
    SET_AL(0x11);
    SET_AH(c8(0x4a8d) & 0xcf);
    io_out16(R.dx, R.ax);
    SET_AH(AH | 0x10);
    io_out16(R.dx, R.ax);
    c8w(0x4ade, 0xff);
    R.ds = ds;
}

/* Retrace_IrqRemove 1000:4a6c
   The old IRQ 2 vector back, register 11h as found with the interrupt cleared and disabled... as the
   install writes it (bits 4-5 as there). */
void Retrace_IrqRemove(void)
{
    uint16_t ds = R.ds;
    R.ax = 0x250a;
    R.dx = c16(0x4a8e);
    R.ds = c16(0x4a90);
    mach_int(0x21);
    R.dx = 0x3d4;
    SET_AL(0x11);
    SET_AH(c8(0x4a8d) & 0xcf);
    io_out16(R.dx, R.ax);
    SET_AH(AH | 0x10);
    io_out16(R.dx, R.ax);
    R.ds = ds;
}

/* Retrace_Irq 1000:4a94
   IRQ 2: if the VGA says its retrace interrupt is pending (3C2h bit 7), clears it, acknowledges, marks
   cs:4a92 (the retrace interrupt works: the timer handler stops waiting for the retrace) and counts
   cs:4ade; else it is someone else's IRQ 2, passed on to the old vector. */
void Retrace_Irq(void)
{
    Cpu saved = R;
    R.dx = 0x3c2;
    SET_AL(io_in8(R.dx));
    if (!(AL & 0x80)) {
        mach_call_far(c16(0x4a90), c16(0x4a8e));
        R = saved;
        return;
    }
    R.dx = 0x3d4;
    SET_AL(io_in8(R.dx));
    uint8_t idx = AL;
    SET_AL(0x11);
    SET_AH(c8(0x4a8d) & 0xef);
    io_out16(R.dx, R.ax);
    io_out8(0x20, 0x20);
    c16w(0x4a92, 1);
    c8w(0x4ade, (uint8_t)(c8(0x4ade) + 1));
    SET_AL(0x11);
    SET_AH((c8(0x4a8d) & 0xdf) | 0x10);
    io_out16(R.dx, R.ax);
    io_out8(R.dx, idx);
    R = saved;
}
