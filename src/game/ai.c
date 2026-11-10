/* The computer driver (docs/re/vehicles.md, "The computer driver"). A computer car has no device: its
   input slot (Input_None) asks Ai_Drive for the same byte a joystick or the keys would give, made from
   the track itself. Every tile's surface byte carries a direction (its low nibble, one of sixteen,
   through a table of headings), optionally re-mapped by the lane the car is in and turned round by the
   tile's orientation; the car steers towards it. The track's second map layer numbers the checkpoints,
   and a per-checkpoint byte of the level says how to take that stretch: flat out, at a speed limit, or
   with a lower top speed. The fire button is always held. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

static inline uint16_t rd(uint16_t a) { return mw(R.ds, a); }
static inline void wr(uint16_t a, uint16_t v) { mww(R.ds, a, v); }
static inline uint8_t rd8(uint16_t a) { return mb(R.ds, a); }
static inline void wr8(uint16_t a, uint8_t v) { mbw(R.ds, a, v); }
#define V(o) ((uint16_t)(R.bx + (o)))

/* Input_None 1000:2ded
   The input slot of a vehicle with no device: AL = 0, but while a race runs ([1082] != 0) the
   computer drives it (BX = the vehicle record). */
void Input_None(void)
{
    SET_AL(0);
    R.cf = false, R.zf = rd8(0x1082) == 0, R.sf = (rd8(0x1082) & 0x80) != 0;   /* cmp byte [1082], 0 */
    if (rd8(0x1082) == 0) return;
    Ai_Drive();
}

/* Ai_Drive 1000:5429
   Vehicle BX's input byte, into 137b and AL: 80h left, 40h right, 20h accelerate, 10h brake, 08h fire.
   - The wanted heading: d = the low nibble of the surface byte 12da (only 0..7 on theme 2); if the
     tile's flags 12de have bit 1, d goes through the lane table 193c:191b (16 bytes per lane, the lane
     = bits 5-6 of 12e0); the heading = the word 193c:18fb + 2d, with 80h flipped (turned round) if
     12de bit 0. The difference (heading - 1278) as a signed byte: below 0 steer left, 3 or more steer
     right, else straight on (so the car settles 0..2 steps to the left of the line, as the original).
   - The stretch's rule: the byte at [28bb] + checkpoint 12e3, high nibble
       0: full throttle, the top speed 129c back to its base 129e;
       1: a speed limit (low nibble * 128 + 380h; 50h more on race 17h; on theme 7 d0h less, and 20h
          less again from race 13h): brake above it, accelerate up to it, 129c back to 129e;
       2: full throttle with 129c lowered to low nibble * 64 + 600h if it is above that;
       other: accelerate unless steering (no 129c change).
   Out: AL = 137b, AH as the rule left it, DS = 193c, CF = 0 (the final or). */
void Ai_Drive(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    wr8(V(0x137b), 0);
    R.ax = rd(V(0x12da));
    R.dx = rd(V(0x12de));
    SET_CL(rd8(V(0x12e0)));
    SET_CH(0);
    if (rd8(0x28bf) == 2) R.ax &= 7;
    R.ax &= 0xf;
    if (R.dx & 2) {
        R.cx = (uint16_t)((R.cx >> 5) & 3);
        R.si = (uint16_t)((R.cx << 4) + 0x191b + R.ax);
        SET_AL(rd8(R.si));   /* lodsb */
        R.si++;
        SET_AH(0);
    }
    R.si = (uint16_t)(R.ax * 2 + 0x18fb);
    R.ax = rd(R.si);
    if (R.dx & 1) R.ax ^= 0x80;
    R.cx = rd(V(0x1278));
    SET_AL(AL - CL);
    if ((int8_t)AL < 0)
        wr8(V(0x137b), rd8(V(0x137b)) | 0x80);
    else if ((int8_t)AL >= 3)
        wr8(V(0x137b), rd8(V(0x137b)) | 0x40);

    R.si = (uint16_t)(rd(0x28bb) + rd(V(0x12e3)));
    SET_AL(rd8(R.si));   /* lodsb */
    R.si++;
    SET_AH(AL);
    SET_AL(AL >> 4);
    bool limit = false;
    if (AL == 0) {
        goto full;
    } else if (AL == 1) {
        limit = true;
    } else if (AL == 2) {
        R.ax = (uint16_t)(((R.ax & 0xf00) >> 2) + 0x600);
        wr8(V(0x137b), rd8(V(0x137b)) | 0x20);
        if ((int16_t)rd(V(0x129c)) <= (int16_t)R.ax) wr(V(0x129c), R.ax);
        goto fire;
    } else {
        SET_AL(rd8(V(0x137b)) & 0xc0);
        if (AL != 0) limit = true;   /* steering: the speed-limit code, as the original jumps there */
        else goto full;
    }
    if (limit) {
        R.ax = (uint16_t)(((R.ax & 0xf00) >> 1) + 0x380);
        if (rd8(0x28c1) == 0x17) R.ax = (uint16_t)(R.ax + 0x50);
        if (rd8(0x28bf) == 7) {
            if (rd8(0x28c1) >= 0x13) R.ax = (uint16_t)(R.ax - 0x20);
            R.ax = (uint16_t)(R.ax - 0xd0);
        }
        if ((int16_t)rd(V(0x127a)) > (int16_t)R.ax) {
            wr8(V(0x137b), rd8(V(0x137b)) | 0x10);
        } else {
            wr8(V(0x137b), rd8(V(0x137b)) | 0x20);
        }
        R.cx = rd(V(0x129e));
        wr(V(0x129c), R.cx);
        goto fire;
    }
full:
    wr8(V(0x137b), rd8(V(0x137b)) | 0x20);
    R.cx = rd(V(0x129e));
    wr(V(0x129c), R.cx);
fire:
    wr8(V(0x137b), rd8(V(0x137b)) | 8);
    R.cf = false, R.zf = false, R.sf = (rd8(V(0x137b)) & 0x80) != 0;
    SET_AL(rd8(V(0x137b)));
}
