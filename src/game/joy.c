/* The joysticks (docs/re/functions.md, "joy"), read through the game port at 201h: a write starts the
   four one-shots, and each axis bit (0, 1 joystick A's X and Y, 2, 3 B's) stays high for a time that
   grows with the stick's resistance. The program counts polls while each bit is high, interrupts off,
   with a limit of 30000 polls; the buttons are bits 4-7, active low (4, 5 A's, 6, 7 B's). Only X is used:
   the calibration keeps X thresholds ([28fd] [28ff] for A, [2905] [2907] for B, in SETTINGS.DAT) and the
   slots compare Y but drop the results.

   The port's machine has no joystick: 201h reads FFh, every axis "high" until the limit, so each timing
   counts 30000 for all four axes and every button reads up. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* The timing loop shared by the three entries: starts the one-shots (out 201h, AL as it is), then
   counts in SI, DI, BX, BP how many polls each axis bit 0-3 is high (each `ror al, 1` moves the next bit
   into the carry, added in), until the bits `mask` selects (in the rotated byte: axis bits moved up to
   4-7) are all low or CX runs out (loopne). DX = 201h, AL = a last read of the port, flags of the last
   `test`. BP kept. */
static void joy_time(uint8_t mask, uint16_t *bp)
{
    R.dx = 0x201;
    io_out8(R.dx, AL);
    R.si = R.di = R.bx = 0;
    *bp = 0;
    R.cx = 0x7530;
    for (;;) {
        uint8_t v = io_in8(R.dx);
        R.si = (uint16_t)(R.si + (v & 1));
        R.di = (uint16_t)(R.di + (v >> 1 & 1));
        R.bx = (uint16_t)(R.bx + (v >> 2 & 1));
        *bp = (uint16_t)(*bp + (v >> 3 & 1));
        v = (uint8_t)(v >> 4 | v << 4);   /* four ror al, 1 */
        SET_AL(v);
        uint8_t t = v & mask;
        R.cf = false;
        flags_zs8(t);
        R.cx--;
        if (R.cx == 0 || t == 0) break;
    }
    SET_AL(io_in8(R.dx));
}

/* Joy_TimeA 1000:2f9e
   Until joystick A's two axes drop. Out: SI = A's X, DI = A's Y (BX = B's X so far), AL = the port. */
void Joy_TimeA(void)
{
    uint16_t bp;
    joy_time(0x30, &bp);
}

/* Joy_TimeB 1000:2fcc
   Until joystick B's axes drop. Out: SI = B's X, DI = B's Y (BX = B's X too), AL = the port. */
void Joy_TimeB(void)
{
    uint16_t bp;
    joy_time(0xc0, &bp);
    R.si = R.bx;
    R.di = bp;
}

/* Joy_TimeBoth 1000:2ffe
   Until all four axes drop. Out: SI, DI = A's X, Y; CX, DX = B's X, Y; AL = the port. */
void Joy_TimeBoth(void)
{
    uint16_t bp;
    joy_time(0xf0, &bp);
    R.cx = R.bx;
    R.dx = bp;
}

/* The flags of `cmp a, b` (byte). */
static void cmp8(uint8_t a, uint8_t b)
{
    R.cf = a < b;
    flags_zs8((uint8_t)(a - b));
}

/* The slot of one joystick from its last timing: buttons `btn` (its two in bits 4-5 of AH), X at `x`
   against the thresholds at `lo` and `hi`, Y at x + 2 against y_lo and y_hi, compared and dropped. */
static void joy_slot(uint8_t btn, uint16_t x, uint16_t lo, uint16_t hi, uint16_t y_lo, uint16_t y_hi)
{
    SET_AL(0);
    SET_AH(btn & 0x30);
    if (AH) SET_AL(8);                 /* either button: fire */
    R.cx = mw(R.ds, x);
    if (!((int16_t)R.cx > (int16_t)mw(R.ds, lo))) SET_AL(AL | 0x80);
    if (!((int16_t)R.cx < (int16_t)mw(R.ds, hi))) SET_AL(AL | 0x40);
    R.cx = mw(R.ds, (uint16_t)(x + 2));
    if (AH == 0x20) SET_AL(AL | 0x20);
    else (void)mw(R.ds, y_lo);         /* cmp cx, [y_lo]: the flags are overwritten (as the original) */
    if (AH == 0x10) SET_AL(AL | 0x10);
    else (void)mw(R.ds, y_hi);
    cmp8(AH, 0x30);
    if (R.zf) {
        SET_AL(AL | 0x30);
        R.cf = false;
        flags_zs8(AL);
    }
}

/* Input_JoyA 1000:2e6c
   Joystick A's slot: either button 08h; X at or below [28fd] 80h (left), at or above [28ff] 40h;
   button 1 20h, button 2 10h, both 30h. Out: AL; AH = the buttons, CX = the Y timing. */
void Input_JoyA(void)
{
    joy_slot(mb(R.ds, 0x1095), 0x108d, 0x28fd, 0x28ff, 0x2901, 0x2903);
}

/* Input_JoyB 1000:2eb3
   The same for joystick B: its buttons are bits 6-7 of [1095] (shifted down by two), X [1091] against
   [2905] [2907], Y [1093] against [2909] [290b] dropped. */
void Input_JoyB(void)
{
    joy_slot((uint8_t)(mb(R.ds, 0x1095) >> 2), 0x1091, 0x2905, 0x2907, 0x2909, 0x290b);
}

/* Joy_Detect 1000:3a12
   [2625] = how many joysticks the BIOS sees: int 15h 84h, DX = 1 reads the four axes into AX, BX, CX,
   DX; A counts when AX > 5 or BX >= 5, B when CX > 5 or DX >= 5 (signed). 0 when the BIOS has no such
   service (CF). Out: AX = the count. */
void Joy_Detect(void)
{
    R.cf = false;
    SET_AH(0x84);
    R.dx = 1;
    mach_int(0x15);
    if (R.cf) {
        R.ax = 0;
    } else {
        int16_t ax = (int16_t)R.ax, bx = (int16_t)R.bx, cx = (int16_t)R.cx, dx = (int16_t)R.dx;
        uint16_t cmp_a, cmp_b;
        if (ax > 5) R.ax = 1;
        else if (bx >= 5) R.ax = 1;
        else R.ax = 0;
        if (cx > 5 || dx >= 5) {
            uint16_t r = (uint16_t)(R.ax + 1);
            R.cf = false;
            flags_zs16(r);
            R.ax = r;
        } else {
            cmp_a = (uint16_t)dx, cmp_b = 5;   /* the last compare: dx with 5 */
            R.cf = cmp_a < cmp_b;
            flags_zs16((uint16_t)(cmp_a - cmp_b));
        }
    }
    mww(R.ds, 0x2625, R.ax);
}

/* Joy_WaitButton 1000:2b93
   For the calibration: BL = the buttons of one joystick (30h A, C0h B). [107e] [107f] cleared; waits
   for those buttons to be up, then shows the place-the-joystick prompt (the message 0df8 at 0b40,
   row B4h, through Menu_BlinkMessage, so it blinks), asks the BIOS for the axes (int 15h 84h, unused)
   and presents, until a button is pressed or Enter is released ([107e] = 1Ch); then times all four
   axes. Out: AX, BX = A's X, Y; CX, DX = B's X, Y.
   Both waits spin on the port and on the keyboard handler's [107e] with the interrupts on; in the
   port, whose CPU is instant, each pass that waits on takes a frame. */
void Joy_WaitButton(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    mbw(R.ds, 0x107e, 0);
    mbw(R.ds, 0x107f, 0);
    uint16_t bx = R.bx;
    R.dx = 0x201;
    for (;;) {
        SET_AL((uint8_t)~io_in8(R.dx));
        if (!(BL & AL)) break;
        mach_frame();
    }
    for (;;) {
        mww(R.ds, 0x019a, 0x0df8);
        mww(R.ds, 0x0194, 0x0b40);
        mww(R.ds, 0x0196, 0x00b4);
        Menu_BlinkMessage();
        SET_AH(0x84);
        R.dx = 1;
        mach_int(0x15);
        Gfx_Present();
        R.bx = bx;
        R.dx = 0x201;
        SET_AL((uint8_t)~io_in8(R.dx));
        if (BL & AL) break;
        if (mb(R.ds, 0x107e) == 0x1c) break;
        mach_frame();
    }
    R.bx = bx;
    Joy_TimeBoth();
    R.ax = R.si;
    R.bx = R.di;
}

/* Joy_WaitButtonA 1000:2b8b
   Joy_WaitButton for joystick A's buttons. */
void Joy_WaitButtonA(void)
{
    SET_BL(0x30);
    Joy_WaitButton();
}

/* Joy_WaitButtonB 1000:2b8f
   Joy_WaitButton for joystick B's buttons. */
void Joy_WaitButtonB(void)
{
    SET_BL(0xc0);
    Joy_WaitButton();
}
