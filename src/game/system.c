/* The system routines (docs/re/functions.md, "sys" and "file"): the code-card check at start-up, the
   CPU speed test behind smoothness AUTO, the tick wait, the exit's clean-up (the vectors and the DOS
   clock), and two file helpers (the data set's GAMEn.LVL, the debug screenshot). */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"
#include "vga.h"

/* The flags of `cmp a, b` (word, signed results read from SF as the callers do). */
static void cmp16(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
}

/* Sys_WaitTick 1000:3165
   Waits for the next tick: until cs:4ade, counted by the timer handler, differs from its value at entry
   (AL). Flags as the last compare leaves them. */
void Sys_WaitTick(void)
{
    SET_AL(c8(0x4ade));
    while (c8(0x4ade) == AL) mach_frame();
    uint8_t v = c8(0x4ade);
    R.cf = v < AL;
    flags_zs8((uint8_t)(v - AL));
}

/* Sys_ReadCrtcBase 1000:319f
   [2643] = the CRTC's base port as the BIOS keeps it (0040:0063, 3D4h on a colour card). DS = 193c,
   AX = the port; ES and SI kept. */
void Sys_ReadCrtcBase(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.ax = mw(0, 0x463);
    mww(R.ds, 0x2643, R.ax);
}

/* Sys_ClearRegs 1000:31b6
   EAX to EDI zeroed as 32-bit registers (a 386 instruction; the port's registers are the 16-bit
   halves), then the 16-bit halves again; the direction flag and the carry cleared, interrupts on. */
void Sys_ClearRegs(void)
{
    R.ax = R.bx = R.cx = R.dx = R.si = R.di = 0;
    R.df = false;
    R.cf = false;
}

/* Sys_CodeCardCheck 1000:31f0
   FONT.BIN (the name at 193c:1241, opened with mode C0h, the handle not checked) read whole, up to FFFFh
   bytes, to 2424:0000 and called far with AX = 0. FONT.BIN is code: the code-card question; AX = 0 back
   is a pass. DS kept; the other registers as FONT.BIN leaves them. */
void Sys_CodeCardCheck(void)
{
    uint16_t ds = R.ds;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.dx = 0x1241;
    R.ax = 0x3dc0;
    mach_int(0x21);
    R.bx = R.ax;
    R.dx = 0;
    R.cx = 0xffff;
    R.ax = SEG_OVL;
    R.ds = R.ax;
    SET_AH(0x3f);
    mach_int(0x21);
    SET_AH(0x3e);
    mach_int(0x21);
    R.ax = 0;
    R.cf = false, R.zf = true, R.sf = false;
    mach_drv();
    R.ds = ds;
}

/* Sys_Ret 1000:35be
   A bare ret: the null entry of the terrain and vehicle-state handler tables. */
void Sys_Ret(void)
{
}

/* File_LoadGameSet 1000:0f99
   The data set's GAMEn.LVL, named at DS:SI, read in two parts into the data segment: 414h bytes at
   DS:040a (to 081e) and 3cch bytes at DS:1feb (to 23b7). No error check; closed. */
void File_LoadGameSet(void)
{
    R.ax = 0x3d00;
    R.dx = R.si;
    mach_int(0x21);
    R.bx = R.ax;
    SET_AH(0x3f);
    R.cx = 0x81e;
    R.dx = 0x40a;
    cmp16(R.cx, R.dx);
    R.cx = (uint16_t)(R.cx - R.dx);
    mach_int(0x21);
    SET_AH(0x3f);
    R.cx = 0x23b7;
    R.dx = 0x1feb;
    cmp16(R.cx, R.dx);
    R.cx = (uint16_t)(R.cx - R.dx);
    mach_int(0x21);
    SET_AH(0x3e);
    mach_int(0x21);
}

/* File_SaveScreen 1000:35bf
   The debug screenshot (F12 with the pause debug keys on): the digit [292d] of the name at [2929]
   (SCRE0.RAW) counted up, the file created and FFFAh bytes of 7d78:0000 (the frame being composed)
   written to it. Nothing written if the create fails. All registers kept; CF from the last DOS call. */
void File_SaveScreen(void)
{
    Cpu saved = R;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    mbw(R.ds, 0x292d, (uint8_t)(mb(R.ds, 0x292d) + 1));
    SET_AH(0x3c);
    R.cx = 0;
    R.dx = 0x2929;
    mach_int(0x21);
    if (!R.cf) {
        R.bx = R.ax;
        R.ax = 0x7d78;   /* the work segment (6d78 in the file, relocated) */
        R.ds = R.ax;
        R.dx = 0;
        SET_AH(0x40);
        R.cx = 0xfffa;
        uint16_t bx = R.bx;
        mach_int(0x21);
        R.bx = bx;
        SET_AH(0x3e);
        mach_int(0x21);
    }
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    uint16_t sp = R.sp, ss = R.ss;
    R = saved;
    R.sp = sp, R.ss = ss;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* The speed test's model (Sys_SpeedTest below). The port's CPU is instant: time passes only at frame
   boundaries (src/machine.c), and the retrace the model shows never ends while the loop counts. The
   loop stops there after 65536 passes (BX = 600h, CX = 1: the fastest class, a machine 2730 times past
   the top threshold), and the retrace is ended as it would have been by then. Under the checker the
   status port is the recording's and the loop ends where the original's did. */

/* Sys_SpeedTest 1000:3ad0
   The benchmark behind smoothness AUTO (the game options screen, when [263a] = 5). The CRTC base from
   the BIOS (0040:0063) into [2643], its status port (base + 6, 3DAh), then with interrupts off: the
   retrace's end, the next retrace's start, and passes of a loop counted for as long as that retrace
   lasts (bit 3 of the status set). Each pass stores a word at A000:0030 under a rep prefix with CX =
   1000, but a rep on a non-string instruction does nothing more: it stores once (as the original). BX
   counts the passes, wrapping to 0 into CX at FA00h. Then [263a] = 4 below 13 passes, 3 below 20, 2
   below 24, else 1 (signed compares). So it times the retrace pulse by CPU work: a slower machine gets
   more game steps per presented frame. SI is left at 463h; AX to DX and ES kept. */
void Sys_SpeedTest(void)
{
    uint16_t ax = R.ax, bx = R.bx, cx = R.cx, dx = R.dx, es = R.es;
    R.ax = 0;
    R.es = R.ax;
    R.si = 0x463;
    R.ax = mw(R.es, R.si);
    mww(R.ds, 0x2643, R.ax);
    R.dx = mw(R.ds, 0x2643);
    R.dx = (uint16_t)(R.dx + 6);
    R.ax = SEG_VGA;
    R.es = R.ax;
    if (R.dx == 0x3da) {
        vga_wait_retrace_end();
        vga_wait_retrace_start();
    } else {   /* another card's status port: the original's loops as they are */
        do SET_AL(io_in8(R.dx));
        while (AL & 8);
        do SET_AL(io_in8(R.dx));
        while (!(AL & 8));
    }
    R.bx = 0;
    R.cx = 0;
    uint32_t passes = 0;
    for (;;) {
        mww(R.es, 0x30, 0);   /* rep (CX = 3e8h, pushed and popped around it): once */
        R.bx++;
        if (R.bx == 0xfa00) {
            R.cx++;
            R.bx = 0;
        }
        SET_AL(io_in8(R.dx));
        if (!(AL & 8)) break;
        if (++passes == 0x10000) {   /* the instant CPU: see above */
            if (R.dx == 0x3da) vga_wait_retrace_end();
            break;
        }
    }
    int16_t n = (int16_t)R.bx;
    uint16_t smooth;
    if (n >= 0x18) cmp16(R.bx, 0x18), smooth = 1;
    else if (n >= 0x14) cmp16(R.bx, 0x14), smooth = 2;
    else if (n >= 0x0d) cmp16(R.bx, 0x0d), smooth = 3;
    else cmp16(R.bx, 0x0d), smooth = 4;
    mww(R.ds, 0x263a, smooth);
    R.ax = ax, R.bx = bx, R.cx = cx, R.dx = dx, R.es = es;
}

/* Sys_BcdToBin 1000:4adf
   AL, two BCD digits, to binary: tens = AL / 16 (div), units = the remainder (kept in BH), AL = tens
   x 10 + units. BL = 0Ah, BH = the units, AH = the high byte of the product (0). Flags of the add. */
void Sys_BcdToBin(void)
{
    SET_BL(0x10);
    SET_AH(0);
    uint8_t q = (uint8_t)(AL / BL), r = (uint8_t)(AL % BL);
    SET_AL(q);
    SET_AH(r);
    SET_BH(AH);
    SET_BL(0x0a);
    R.ax = (uint16_t)(AL * BL);
    uint16_t s = (uint16_t)(AL + BH);
    R.cf = s > 0xff;
    SET_AL(s);
    flags_zs8(AL);
}

/* Sys_RemoveIrqs 1000:49af
   The exit's clean-up, interrupts off: the timer's vector and rate back (Timer_Remove), the BIOS's IRQ 1
   vector from [2925]/[2927]; then DOS's time and date set again from the real-time clock, because the
   program's timer chained the BIOS's handler at the frame rate and ran the day's clock fast. The time:
   int 1Ah 02h (retried while CF), hours, minutes, seconds from BCD, int 21h 2Dh with hundredths 0. The
   date: int 1Ah 04h, the four BCD bytes converted, then the year computed as 100 x the year in the
   century plus that year again (CL, where the century CH was meant: 1994 gives 9494, a year DOS
   refuses, so the date is never set; as the original). Then the driver stopped (AH = 6) and the
   retrace interrupt removed. ES kept. */
void Sys_RemoveIrqs(void)
{
    uint16_t es = R.es;
    Timer_Remove();
    uint16_t ds = R.ds;
    R.ax = 0x2509;
    R.dx = mw(R.ds, 0x2925);
    R.bx = mw(R.ds, 0x2927);
    R.ds = R.bx;
    mach_int(0x21);
    R.ds = ds;
    for (;;) {
        SET_AH(2);
        mach_int(0x1a);
        if (!R.cf) break;   /* CF: the clock is being updated; asked again at once, interrupts off */
    }
    SET_AL(CH);
    Sys_BcdToBin();
    SET_CH(AL);
    SET_AL(CL);
    Sys_BcdToBin();
    SET_CL(AL);
    SET_AL(DH);
    Sys_BcdToBin();
    SET_DH(AL);
    SET_DL(0);
    SET_AH(0x2d);
    mach_int(0x21);
    for (;;) {
        SET_AH(4);
        mach_int(0x1a);
        if (!R.cf) break;
    }
    SET_AL(CH);
    Sys_BcdToBin();
    SET_CH(AL);
    SET_AL(CL);
    Sys_BcdToBin();
    SET_CL(AL);
    SET_AL(DH);
    Sys_BcdToBin();
    SET_DH(AL);
    SET_AL(DL);
    Sys_BcdToBin();
    SET_DL(AL);
    SET_AL(0x64);
    R.ax = (uint16_t)(AL * CL);   /* mul cl: the year, not the century (CH) */
    R.cf = AH != 0;
    SET_CH(0);
    uint32_t s = (uint32_t)R.cx + R.ax;
    R.cf = s > 0xffff;
    R.cx = (uint16_t)s;
    flags_zs16(R.cx);
    SET_AH(0x2b);
    mach_int(0x21);
    SET_AH(6);
    mach_drv();
    Retrace_IrqRemove();
    R.es = es;
}
