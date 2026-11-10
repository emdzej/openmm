/* The players' input (docs/re/functions.md, "input" and "mouse"). Each of the four vehicles has a slot,
   a near routine chosen from its control type (193c:2658.. : 1 joystick A, 2 joystick B, 3 mouse,
   4 keys 1, 5 keys 2, anything else none, the computer driving during a race). Once per frame Input_Read
   times the joysticks in use, calls the four slots and stores each answer, one byte per vehicle:
   80h left, 40h right, 20h and 10h the two drive buttons, 08h fire (or select in the menus).

   Data: 193c:1083..1089 the slots (near addresses), 108c which joysticks to time (bit 0 A, bit 1 B),
   108d/108f joystick A's X, Y timings, 1091/1093 joystick B's, 1095 the buttons (port 201h inverted),
   2660..2666 the vehicle records, 137b 14df 1643 17a7 the vehicles' input bytes, 1080 0 or the input
   byte the menus read, 108b the menus' input. */
#include "cpu.h"
#include "fntab.h"
#include "machine.h"
#include "mem.h"
#include "platform.h"
#include "routines.h"
#include <stdio.h>

/* call word ptr [slot]: the slot's routine by its address in the code segment. The slots only ever hold
   the six input routines Input_SetSlots stores; anything else would send the original into whatever code
   is there (logged, AL left as it is). */
static void call_slot(uint16_t off)
{
    const FnEntry *e = fntab_find((uint32_t)SEG_CODE << 16 | off);
    if (e) {
        e->fn();
        return;
    }
    char m[64];
    snprintf(m, sizeof m, "input: a slot calls 1000:%04x, no routine there", off);
    plat_log(m);
}

/* The flags of `cmp a, b` (byte). */
static void cmp8(uint8_t a, uint8_t b)
{
    R.cf = a < b;
    flags_zs8((uint8_t)(a - b));
}

/* Input_SetSlots 1000:2d00
   The four slots from the four control types; [108c] collects the joysticks used. Leaves AX = the last
   type, DX = the last slot, BX = 2660h, DI = 108bh, flags of the last `cmp di, 1089h`. */
void Input_SetSlots(void)
{
    mbw(R.ds, 0x108c, 0);
    R.bx = 0x2658;
    R.di = 0x1083;
    for (;;) {
        R.ax = mw(R.ds, R.bx);
        switch (R.ax) {
        case 4: R.dx = 0x2dfa; break;   /* Input_Keys1 */
        case 5: R.dx = 0x2dfe; break;   /* Input_Keys2 */
        case 3: R.dx = 0x2e02; break;   /* Input_Mouse */
        case 1:
            mbw(R.ds, 0x108c, mb(R.ds, 0x108c) | 1);
            R.dx = 0x2e6c;              /* Input_JoyA */
            break;
        case 2:
            mbw(R.ds, 0x108c, mb(R.ds, 0x108c) | 2);
            R.dx = 0x2eb3;              /* Input_JoyB */
            break;
        default: R.dx = 0x2ded; break;  /* Input_None */
        }
        mww(R.ds, R.di, R.dx);
        R.di = (uint16_t)(R.di + 2);
        R.bx = (uint16_t)(R.bx + 2);
        R.cf = R.di < 0x1089;
        flags_zs16((uint16_t)(R.di - 0x1089));
        if ((int16_t)R.di > 0x1089) break;
    }
}

/* Input_Read 1000:2d5b
   The joysticks [108c] asks for timed (exactly 1: A, 2: B, 3: both; the buttons inverted into [1095]),
   the four slots called with BX = their vehicle's record and their answers stored; then [108b] = the
   first two vehicles' inputs together when [1080] = 0, else the byte [1080] points to. All registers
   kept (pusha); flags of the last `or`. */
void Input_Read(void)
{
    Cpu saved = R;
    cmp8(mb(R.ds, 0x108c), 1);
    if (R.zf) {
        Joy_TimeA();
        mww(R.ds, 0x108d, R.si);
        mww(R.ds, 0x108f, R.di);
        mbw(R.ds, 0x1095, (uint8_t)~AL);
    }
    cmp8(mb(R.ds, 0x108c), 2);
    if (R.zf) {
        Joy_TimeB();
        mww(R.ds, 0x1091, R.si);
        mww(R.ds, 0x1093, R.di);
        mbw(R.ds, 0x1095, (uint8_t)~AL);
    }
    cmp8(mb(R.ds, 0x108c), 3);
    if (R.zf) {
        Joy_TimeBoth();
        mww(R.ds, 0x108d, R.si);
        mww(R.ds, 0x108f, R.di);
        mww(R.ds, 0x1091, R.cx);
        mww(R.ds, 0x1093, R.dx);
        mbw(R.ds, 0x1095, (uint8_t)~AL);
    }
    static const uint16_t rec[4] = {0x2660, 0x2662, 0x2664, 0x2666};
    static const uint16_t slot[4] = {0x1083, 0x1085, 0x1087, 0x1089};
    static const uint16_t in[4] = {0x137b, 0x14df, 0x1643, 0x17a7};
    for (int i = 0; i < 4; i++) {
        R.bx = mw(R.ds, rec[i]);
        call_slot(mw(R.ds, slot[i]));
        mbw(R.ds, in[i], AL);
    }
    R.bx = mw(R.ds, 0x1080);
    R.cf = false;
    if (R.bx == 0) {
        SET_AL(mb(R.ds, 0x137b) | mb(R.ds, 0x14df));
        flags_zs8(AL);
    } else {
        flags_zs16(R.bx);
        SET_AL(mb(R.ds, R.bx));
    }
    mbw(R.ds, 0x108b, AL);
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    uint16_t sp = R.sp, ss = R.ss, ds = R.ds, es = R.es;
    bool df = R.df;
    R = saved;
    R.sp = sp, R.ss = ss, R.ds = ds, R.es = es, R.df = df;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Input_Keys1 1000:2dfa
   Keys 1: AL = the high byte of the key mask (the scan codes at [106c]..[1073], bit 7 the first). */
void Input_Keys1(void)
{
    SET_AL(mb(R.ds, 0x107d));
}

/* Input_Keys2 1000:2dfe
   Keys 2: AL = the low byte (the scan codes at [1074]..[107b]). */
void Input_Keys2(void)
{
    SET_AL(mb(R.ds, 0x107c));
}

/* Input_Mouse 1000:2e02
   The mouse (int 33h 3): left button 28h (drive and fire), right 10h; the pointer's X left of 160 -
   [290d] is 80h, right of 160 + [290d] 40h, Y above 100 - [290d] 20h, below 100 + [290d] 10h (signed,
   "beyond" includes the edge); if any direction was given the pointer is put back at 160,100 (int 33h
   4). The buttons and position are BX, CX, DX as the driver returns them: with no driver (int 33h
   answers only AX = 0) they are what the caller left there, BX the vehicle record (as the original).
   Out: AL (AX = SI = the bits); flags of `test si, F0h`. Reached only if the mouse flag is set, and its
   set-up (1000:3a44) is dead code. */
void Input_Mouse(void)
{
    R.ax = 3;
    mach_int(0x33);
    R.si = 0;
    if (R.bx & 1) R.si |= 0x28;
    if (R.bx & 2) R.si |= 0x10;
    int16_t d = (int16_t)mw(R.ds, 0x290d);
    R.ax = (uint16_t)(0xa0 - d);
    if (!((int16_t)R.cx > (int16_t)R.ax)) R.si |= 0x80;
    else {
        R.ax = (uint16_t)(0xa0 + d);
        if (!((int16_t)R.cx < (int16_t)R.ax)) R.si |= 0x40;
    }
    R.ax = (uint16_t)(0x64 - d);
    if (!((int16_t)R.dx > (int16_t)R.ax)) R.si |= 0x20;
    else {
        R.ax = (uint16_t)(0x64 + d);
        if (!((int16_t)R.dx < (int16_t)R.ax)) R.si |= 0x10;
    }
    uint16_t si = R.si;
    R.cf = false;
    flags_zs16((uint16_t)(R.si & 0xf0));
    if (!R.zf) {
        R.ax = 4;
        R.cx = 0xa0;
        R.dx = 0x64;
        mach_int(0x33);
    }
    R.ax = si;
}
