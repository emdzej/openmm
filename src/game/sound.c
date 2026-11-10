/* The program's side of the sound driver (docs/re/functions.md, "snd"): DRIVERn.BIN loaded over FONT.BIN
   at 2424:0000 and called far with a function number in AH. n is the sound setting [0f64] (0 none, 1 and
   2 the AdLib and the speaker drivers, inferred). The timer handler calls the driver's tick (AH = 3) only
   once the driver is up (cs:3279). */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* Snd_StopAll 1000:00e2
   Unless the sound setting [0f64] is 1, the driver's AH = 10h with AL = 0 (inferred: all voices off).
   The registers as the driver leaves them; flags of the compare when it is skipped. */
void Snd_StopAll(void)
{
    uint16_t v = mw(R.ds, 0x0f64);
    R.cf = v < 1;
    flags_zs16((uint16_t)(v - 1));
    if (R.zf) return;
    SET_AH(0x10);
    SET_AL(0);
    mach_drv();
}

/* Snd_LoadDriver 1000:321c
   The digit '0' + [0f64] patched into the name DRIVERn.BIN (193c:11da, the digit at 11e0), the timer's
   driver tick held off (cs:3279 = 0), the file read whole (up to FFFFh bytes, the handle not checked)
   to 2424:0000; the driver's AH = 0 (initialise) with DX = 388h (the AdLib's port) and BX, CX = the
   words at 193c:000b and 000d (read through CS while DS is the overlay's); then AH = 2 with CX = 4287h,
   repeated until it answers AL = 0. cs:3279 = 1. DS kept; the rest as the driver leaves it. */
void Snd_LoadDriver(void)
{
    uint16_t ds = R.ds;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.ax = mw(R.ds, 0x0f64);
    R.cf = AL + 0x30 > 0xff;
    SET_AL((uint8_t)(AL + 0x30));
    flags_zs8(AL);
    mbw(R.ds, 0x11e0, AL);
    c8w(0x3279, 0);
    R.dx = 0x11da;
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
    SET_AH(0);
    R.dx = 0x388;
    R.bx = c16(0x93cb);
    R.cx = c16(0x93cd);
    mach_drv();
    do {
        SET_AH(2);
        R.cx = 0x4287;
        mach_drv();
        R.cf = false;
        flags_zs8(AL);
    } while (AL != 0);
    R.ds = SEG_DATA;
    c8w(0x3279, 1);
    R.ds = ds;
}
