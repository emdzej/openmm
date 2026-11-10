/* The string instructions and flag results the drawing routines are made of (docs/re/drawing.md).
   The original's blits are loops of lodsb/stosb/movsw over DS:SI and ES:DI; these helpers do one step
   each, on R, with the 64 KB offset wrap and the direction flag as on the CPU, so that a ported loop
   leaves SI, DI and CX exactly where the original leaves them. */
#pragma once
#include "cpu.h"
#include "mem.h"

#include "segs.h"

static inline uint16_t str_step(uint16_t n) { return R.df ? (uint16_t)-n : n; }

static inline uint8_t lodsb(void)
{
    uint8_t v = mb(R.ds, R.si);
    R.si = (uint16_t)(R.si + str_step(1));
    return v;
}
static inline void stosb(uint8_t v)
{
    mbw(R.es, R.di, v);
    R.di = (uint16_t)(R.di + str_step(1));
}
static inline void stosw(uint16_t v)
{
    mww(R.es, R.di, v);
    R.di = (uint16_t)(R.di + str_step(2));
}
static inline void movsb(void)
{
    mbw(R.es, R.di, mb(R.ds, R.si));
    R.si = (uint16_t)(R.si + str_step(1));
    R.di = (uint16_t)(R.di + str_step(1));
}
static inline void movsw(void)
{
    mww(R.es, R.di, mw(R.ds, R.si));
    R.si = (uint16_t)(R.si + str_step(2));
    R.di = (uint16_t)(R.di + str_step(2));
}
/* movsd with 16-bit addresses (66 a5): the doubleword is read whole, then written */
static inline void movsd(void)
{
    uint32_t v = md(R.ds, R.si);
    mdw(R.es, R.di, v);
    R.si = (uint16_t)(R.si + str_step(4));
    R.di = (uint16_t)(R.di + str_step(4));
}
static inline void rep_movsb(void) { for (; R.cx; R.cx--) movsb(); }
static inline void rep_movsw(void) { for (; R.cx; R.cx--) movsw(); }
static inline void rep_stosb(uint8_t v) { for (; R.cx; R.cx--) stosb(v); }
static inline void rep_stosw(uint16_t v) { for (; R.cx; R.cx--) stosw(v); }

/* `add r16, v`: the result, with CF as the addition leaves it */
static inline uint16_t add16(uint16_t a, uint16_t b)
{
    R.cf = (uint32_t)a + b > 0xffff;
    uint16_t r = (uint16_t)(a + b);
    flags_zs16(r);
    return r;
}
/* `sub r16, v` */
static inline uint16_t sub16(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    uint16_t r = (uint16_t)(a - b);
    flags_zs16(r);
    return r;
}
/* `y * 110h` as the routines compute a back-buffer row (y << 4 plus the low byte of y << 8) */
static inline uint16_t row_110(uint16_t y) { return (uint16_t)((uint16_t)(y << 4) + (uint16_t)((y & 0xff) << 8)); }
