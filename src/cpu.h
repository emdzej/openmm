/* The registers as the original's routines hand them to each other (docs/re/porting.md). The game is
   hand-written assembly: a routine takes its arguments in registers, returns results in registers and
   in the carry flag, and its callers often rely on what it leaves in the others (a count run down to
   zero, a pointer moved past a buffer). The port keeps those hand-overs exact: every ported routine
   leaves R as the original leaves the registers on its return, so routines can be ported in any order
   and each checked alone against the original (tools/emu/capture.py, mm_fncheck). Inside a routine the
   work is ordinary C on locals.

   The stack is not modelled: push and pop within a routine become locals, calls become C calls. SP and
   SS are kept only where the original reads or saves them. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t ax, bx, cx, dx, si, di, bp, sp;
    uint16_t ds, es, ss;
    bool cf, zf, sf, df;
} Cpu;

extern Cpu R;

/* The byte halves. */
static inline uint8_t lo8(uint16_t r) { return (uint8_t)r; }
static inline uint8_t hi8(uint16_t r) { return (uint8_t)(r >> 8); }
static inline void set_lo8(uint16_t *r, uint8_t v) { *r = (uint16_t)((*r & 0xff00) | v); }
static inline void set_hi8(uint16_t *r, uint8_t v) { *r = (uint16_t)((*r & 0x00ff) | (uint16_t)v << 8); }
#define AL lo8(R.ax)
#define AH hi8(R.ax)
#define BL lo8(R.bx)
#define BH hi8(R.bx)
#define CL lo8(R.cx)
#define CH hi8(R.cx)
#define DL lo8(R.dx)
#define DH hi8(R.dx)
#define SET_AL(v) set_lo8(&R.ax, (uint8_t)(v))
#define SET_AH(v) set_hi8(&R.ax, (uint8_t)(v))
#define SET_BL(v) set_lo8(&R.bx, (uint8_t)(v))
#define SET_BH(v) set_hi8(&R.bx, (uint8_t)(v))
#define SET_CL(v) set_lo8(&R.cx, (uint8_t)(v))
#define SET_CH(v) set_hi8(&R.cx, (uint8_t)(v))
#define SET_DL(v) set_lo8(&R.dx, (uint8_t)(v))
#define SET_DH(v) set_hi8(&R.dx, (uint8_t)(v))

/* Flags as an arithmetic result leaves them, where a caller tests them after the return. */
static inline void flags_zs16(uint16_t v) { R.zf = v == 0, R.sf = (v & 0x8000) != 0; }
static inline void flags_zs8(uint8_t v) { R.zf = v == 0, R.sf = (v & 0x80) != 0; }
