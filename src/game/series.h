/* Small steps the front end's game routines share (src/game/main.c, series.c, twoplayer.c): the flags
   of compares and arithmetic the original leaves for the next call (the checker compares CF at every
   call), the tune request, the one-tick wait. */
#pragma once
#include "cpu.h"
#include "machine.h"
#include "mem.h"

/* cmp a, b: CF, ZF, SF; returns ZF (equal). */
static inline bool cmpb(uint8_t a, uint8_t b)
{
    R.cf = a < b;
    flags_zs8((uint8_t)(a - b));
    return R.zf;
}
static inline bool cmpw(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
    return R.zf;
}
/* or / and / xor / test: CF = 0. */
static inline void logicb(uint8_t v)
{
    R.cf = false;
    flags_zs8(v);
}
static inline void logicw(uint16_t v)
{
    R.cf = false;
    flags_zs16(v);
}
/* add / sub of words: the result, CF the carry or borrow. */
static inline uint16_t addw(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a + b);
    R.cf = r < a;
    flags_zs16(r);
    return r;
}
static inline uint16_t subw(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
    return (uint16_t)(a - b);
}
/* The word at [off] of the data segment changed by add / sub, with the flags. */
static inline void add_mw(uint16_t off, uint16_t v) { d16w(off, addw(d16(off), v)); }
static inline void sub_mw(uint16_t off, uint16_t v) { d16w(off, subw(d16(off), v)); }

/* A tune through the driver: AH = 9 asks whether tune n can play, AH = 4 plays it (both with the
   flags as they are). AX back as it was on entry (pushed); the flags the driver's when it played, else
   those of `or al, al`. Callers that load AL = n before their push hand over that AX. */
static inline void play_tune(uint8_t n)
{
    uint16_t ax = R.ax;
    R.ax = (uint16_t)(0x0900 | n);
    mach_drv();
    logicb(AL);
    if (AL != 0) {
        R.ax = (uint16_t)(0x0400 | n);
        mach_drv();
    }
    R.ax = ax;
}

/* `mov ax, [0002]` / `cmp [0002], ax` / `je` back: one tick of the timer's counter. */
static inline void wait_tick(void)
{
    R.ax = d16(0x0002);
    while (d16(0x0002) == R.ax) mach_frame();
    cmpw(d16(0x0002), R.ax);
}

/* `mov [off], 0` / `cmp [off], n` / `jl` back: n ticks of a counter the timer moves. */
static inline void wait_ticks(uint16_t off, int16_t n)
{
    d16w(off, 0);
    while ((int16_t)d16(off) < n) mach_frame();
    cmpw(d16(off), (uint16_t)n);
}

/* The race list's byte for race BL (BH cleared): AH = (byte and 3) + 1 the track into [28c0], AL =
   byte >> 2 the theme into [28bf]; CF the last bit `shr al, 2` shifts out, ZF / SF of `inc ah`. */
static inline void race_from_list(void)
{
    SET_BH(0);
    SET_AL(d8((uint16_t)(R.bx + 0x043c)));
    SET_AH(AL & 3);
    R.cf = (AL >> 1) & 1;
    SET_AL(AL >> 2);
    d8w(0x28bf, AL);
    SET_AH(AH + 1);
    flags_zs8(AH);
    d8w(0x28c0, AH);
}
