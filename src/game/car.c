/* The vehicles' control and movement (docs/re/vehicles.md): once per game step Car_StepAll turns each
   car's input byte into heading and speed, then for the four racers Car_Velocity turns heading and speed
   into a wanted velocity and lets the actual velocity follow it within the grip, Coll_CarsAll pushes
   touching cars apart, and Car_Physics moves each car: a candidate position, the track under it
   (Car_MoveProbe, Track_TileAt, Track_SurfaceAt), the terrain's handler (Terrain_Dispatch), a bounce off
   solid cells, the commit. Last the camera follows the leader (or the middle of the two players).

   The routines work on R directly (docs/re/porting.md): the callees not ported yet and the sound driver
   see the registers the original has at each call, and their results flow on as in the original. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

static inline uint16_t rd(uint16_t a) { return mw(R.ds, a); }
static inline void wr(uint16_t a, uint16_t v) { mww(R.ds, a, v); }
static inline uint8_t rd8(uint16_t a) { return mb(R.ds, a); }
#define V(o) ((uint16_t)(R.bx + (o)))
#define SV(o) ((int16_t)rd(V(o)))
#define THEME rd8(0x28bf)

/* cmp a, b: CF, ZF and SF as the instruction leaves them */
static inline void cmp16(uint16_t a, uint16_t b)
{
    uint16_t d = (uint16_t)(a - b);
    R.cf = a < b, R.zf = d == 0, R.sf = (d & 0x8000) != 0;
}
static inline void cmp8(uint8_t a, uint8_t b)
{
    uint8_t d = (uint8_t)(a - b);
    R.cf = a < b, R.zf = d == 0, R.sf = (d & 0x80) != 0;
}
/* cmp word [a], b */
static inline uint16_t cmpm(uint16_t a, uint16_t b)
{
    uint16_t v = rd(a);
    cmp16(v, b);
    return v;
}

/* The candidate position (as in Coll_CarPair): 125e/126a = 125c/1268 + the integer part of
   velocity + fraction, 125a/1266 = the new fraction, wrapped into 0..bffh. */
static void candidate(void)
{
    R.ax = (uint16_t)(rd(V(0x1272)) + rd(V(0x1258)));
    R.dx = R.ax;
    R.ax = (uint16_t)(int16_t)(int8_t)AH;
    R.ax = (uint16_t)(R.ax + rd(V(0x125c)));
    wr(V(0x125e), R.ax);
    SET_DH(0);
    wr(V(0x125a), R.dx);
    R.ax = (uint16_t)(rd(V(0x1276)) + rd(V(0x1264)));
    R.dx = R.ax;
    R.ax = (uint16_t)(int16_t)(int8_t)AH;
    R.ax = (uint16_t)(R.ax + rd(V(0x1268)));
    wr(V(0x126a), R.ax);
    SET_DH(0);
    wr(V(0x1266), R.dx);
    for (uint16_t o = 0x125e;; o = 0x126a) {
        if (SV(o) <= -1) wr(V(o), (uint16_t)(rd(V(o)) + 0xc00));
        cmp16(rd(V(o)), 0xc00);
        if (SV(o) >= 0xc00) wr(V(o), (uint16_t)(rd(V(o)) - 0xc00));
        if (o == 0x126a) break;
    }
}

/* The "is a computer car racing the leading human" test of Car_StepAll and Car_Velocity, into
   193c:262f: CL = 1 when BX is a computer car (not record 0) and the first entry of the order list
   193c:2678 is record 0. The scan compares SI (a pointer) with the word at 267e (a record offset),
   signed, so it never goes past the first entry, as the original. */
static void catch_up_flag(void)
{
    SET_CL(0);
    if (R.bx != 0 && rd(V(0x1250)) == 0) {
        R.si = 0x2678;
        for (;;) {
            R.ax = rd(R.si);   /* lodsw */
            R.si = (uint16_t)(R.si + 2);
            if (R.ax == R.bx) break;
            if (R.ax == 0) {
                SET_CL(CL + 1);
                break;
            }
            if ((int16_t)R.si > (int16_t)rd(0x267e)) break;
        }
    }
    mbw(R.ds, 0x262f, CL);
}

/* The control step, per record (docs/re/vehicles.md, "Control"):
   - Theme 9: a record whose 12ed is 2 sets [26ca] = 1 and, stopped (127a = 0), goes to state 0fh,
     else only drags; the others go straight to the controls (no catch-up flag, no finish handling).
   - 262f (catch_up_flag). A finished car (12ed <= 0, no laps left): in a two-player game
     ([2656] = 2) the point's end (26b4 = 4: the banner 26c2/26be/26c0 sliding; else the order list
     2678.. re-seeded, 2670..2676 = 7d00h, 26c2 = c8h, 26bc = 0); otherwise it brakes to a stop by
     12a4, and once stopped [26c6] = the number of finished, stopped records, 2 at once when player
     1's car is finished: [26c6] >= 2 ends the race.
   - The controls (state 0, active, 1380 cleared once 137e is set): a finished computer car only
     drags. Input byte 137b: fire (08h; from the keys, the mouse or a computer, not a joystick) goes to
     4f03. Not steering: the heading 1278 snaps to its sixteenth (low nibble <= 4 down, >= 0ch up,
     else the half). Left/right: by Car_TurnRate (left wraps to 0..ffh at once, right on the next
     step's mask); a crawling car (|127a| <= ffh) on themes up to 6 turning gets speed ffh. Race over
     or finished: drag only. Both pedals: the shot (4f17). Neither: drag 12a6 towards 0 (none while
     airborne, 12d6 > 0). Accelerate: + 12a2 (six times when catching up) up to 129c; brake:
     - 12a4 down to 12a0, else fire -> 4f03.
   - 4f03: fire with the race over or [2915] = 1 coasts; else the shot, 4f17: only with [2919] = 1 or
     on theme 7 (the tanks), the race not over, no shell in flight (13a4 = 0), a human car: effect
     0eh, 1394 = 1, 1396 = 0, 13a4 = 60, 139a = 139e = 10; the shell's start x = 125c + sin / 8,
     y = 1268 + sin(heading - 40h) / 8; its step words 13a0/13a2 = the sine byte, and in the high
     byte's low nibble the car's own whole-pixel step this frame + 8.
   The camera: target 2646/2648 = the followed record ([27b7 + 2 * [27b5]]; 1 = the midpoint of the
   two players, [2911] = 1 when they are more than e8h x / b0h y apart unless it is 2) less the
   look-ahead 1262/126e; the camera 264a/264c moves towards it by at most 264e/2650, which become 32h
   after a jump of more than 3e8h or on arrival: when both have arrived the records' 137e = 1. */

/* Car_StepAll 1000:4aee
   The game step's vehicle part: the control step of the four records (BX = 0, 164h, 2c8h, 42ch), then
   for the racers in player order (193c:2660..2666) the velocity, the collisions and the moves, and
   the camera. 4f03 (Car_StepAll_Fire) and 4f17 (Car_StepAll_Hop) are labels inside it. DX kept. */
void Car_StepAll(void)
{
    uint16_t dx_saved = R.dx;
    R.bx = 0;
next_record:
    if (THEME == 9) {
        if (cmpm(V(0x12ed), 2) != 2) goto controls;
        wr(0x26ca, 1);
        if (cmpm(V(0x127a), 0) != 0) goto drag;
        wr(V(0x12ae), 0xf);
        goto loop_end;
    }
    catch_up_flag();
    if ((int16_t)cmpm(V(0x12ed), 0) > 0) goto controls;
    if (cmpm(0x2656, 2) == 2) goto two_player_end;
    if ((int16_t)cmpm(V(0x127a), 0) > 0) {
        wr(V(0x127a), (uint16_t)(rd(V(0x127a)) - rd(V(0x12a4))));
        R.ax = rd(V(0x12a4));
        if ((int16_t)cmpm(V(0x127a), 0) < 0) wr(V(0x127a), 0);
        goto controls;
    }
    if (cmpm(V(0x127a), 0) != 0) goto controls;
    /* the count of finished, stopped records (fixed addresses: the four records) */
    wr(0x26c6, 0);
    if (cmpm(0x12ed, 0) == 0 && cmpm(0x127a, 0) == 0) wr(0x26c6, (uint16_t)(rd(0x26c6) + 1));
    if (cmpm(0x1451, 0) == 0 && cmpm(0x13de, 0) == 0) wr(0x26c6, (uint16_t)(rd(0x26c6) + 1));
    if (cmpm(0x15b5, 0) == 0 && cmpm(0x1542, 0) == 0) wr(0x26c6, (uint16_t)(rd(0x26c6) + 1));
    if (cmpm(0x1719, 0) == 0 && cmpm(0x16a6, 0) == 0) wr(0x26c6, (uint16_t)(rd(0x26c6) + 1));
    R.si = rd(0x2660);
    if (cmpm((uint16_t)(R.si + 0x12ed), 0) == 0) wr(0x26c6, 2);
    goto loop_end;

two_player_end: {
    /* the point is over: 26b4 4 = the banner, else the order list re-seeded for the next point */
    if (cmpm(0x26b4, 4) == 4) goto banner;
    uint16_t bx = R.bx;
    bool p1 = (int16_t)cmpm(0x26b4, 4) >= 4;
    if (cmpm(0x26c4, 1) == 1) {
        R.bx = rd(p1 ? 0x2660 : 0x2662);
        wr(0x26c4, R.bx);
        wr(0x26b8, R.bx);
        R.bx = p1 ? 0 : 0x2c8;
        wr(0x26c4, R.bx);
        wr(0x2678, R.bx);
        R.bx = p1 ? 0x2c8 : 0;
        wr(0x267a, R.bx);
        R.bx = 0x164;
        wr(0x267c, R.bx);
        R.bx = 0x42c;
        wr(0x267e, R.bx);
        wr(0x2670, 0x7d00);
        wr(0x2672, 0x7d00);
        wr(0x2674, 0x7d00);
        wr(0x2676, 0x7d00);
        wr(0x26c2, 0xc8);
        wr(0x26bc, 0);
    }
    R.bx = bx;
    goto loop_end;
}

banner:
    if (cmpm(0x26c2, 2) == 2) goto controls;
    if (cmpm(0x26c2, 0) == 0) {
        wr(0x26c2, 3);
        wr(0x26be, 0x158);
        wr(0x26c0, 0x7c);
    }
    if (cmpm(0x26be, 0x80) == 0x80) {
        wr(0x26c2, (uint16_t)(rd(0x26c2) + 1));
        if (cmpm(0x26c2, 0x64) != 0x64) goto controls;
        wr(0x26c2, 3);
        wr(0x26be, (uint16_t)(rd(0x26be) - 8));
        goto controls;
    }
    if ((int16_t)cmpm(0x26be, 0xffa8) <= -0x58) {
        wr(0x26c2, 0xc8);
        goto controls;
    }
    wr(0x26be, (uint16_t)(rd(0x26be) - 8));

controls:
    if (cmpm(V(0x12ae), 0) != 0) goto loop_end;
    if (cmpm(V(0x1380), 0) != 0) {
        if (cmpm(V(0x137e), 1) != 1) goto loop_end;
        wr(V(0x1380), 0);
    }
    if (cmpm(V(0x124c), 0) == 0) goto loop_end;
    if ((int16_t)cmpm(V(0x12ed), 0) <= 0 && cmpm(0x2656, 2) != 2 && cmpm(V(0x12eb), 1) == 1) goto drag;
    SET_DL(rd8(V(0x137b)));
    if (cmpm(V(0x12eb), 1) != 1) {
        uint16_t si = R.si;
        R.si = (uint16_t)((rd(V(0x124a)) - 1) * 2 + 0x2658);
        R.ax = rd(R.si);
        R.si = si;
        if (R.ax != 1 && R.ax != 2 && (DL & 8)) goto fire;
    }
    if (!(DL & 0xc0)) {
        /* not steering: centre the heading on its sixteenth */
        R.ax = rd(V(0x1278)) & 0xf;
        if ((int16_t)R.ax <= 4)
            wr(V(0x1278), rd(V(0x1278)) & 0xf0);
        else if ((int16_t)R.ax >= 0xc)
            wr(V(0x1278), (uint16_t)((rd(V(0x1278)) & 0xf0) + 0x10));
        else
            wr(V(0x1278), (uint16_t)((rd(V(0x1278)) & 0xf0) | 8));
    }
    wr(V(0x1278), rd(V(0x1278)) & 0xff);
    if (DL & 0x80) {
        Car_TurnRate();
        wr(V(0x1278), (uint16_t)((rd(V(0x1278)) - R.cx) & 0xff));
        if ((int16_t)rd(V(0x127a)) <= 0xff && (int16_t)rd(V(0x127a)) >= -0xff && THEME <= 6)
            wr(V(0x127a), 0xff);
    }
    if (DL & 0x40) {
        Car_TurnRate();
        wr(V(0x1278), (uint16_t)(rd(V(0x1278)) + R.cx));   /* not masked here: the next step does */
        if ((int16_t)rd(V(0x127a)) <= 0xff && (int16_t)rd(V(0x127a)) >= -0xff && THEME <= 6)
            wr(V(0x127a), 0xff);
    }
    if (cmpm(0x2656, 2) != 2) {
        if ((int16_t)cmpm(0x26c6, 2) >= 2) goto drag;
        if ((int16_t)cmpm(V(0x12ed), 0) <= 0) goto drag;
    }
    if ((DL & 0x30) == 0x30) goto shot;
    if (!(DL & 0x30)) goto coast;
    if (cmpm(0x2656, 2) != 2) {
        if ((int16_t)cmpm(0x26c6, 2) >= 2) goto loop_end;
        if ((int16_t)cmpm(V(0x12ed), 0) <= 0) goto loop_end;
    }
    if (DL & 0x20) {
        R.ax = rd(V(0x12a2));
        int times = rd8(0x262f) == 1 ? 6 : 1;
        for (int i = 0; i < times; i++) wr(V(0x127a), (uint16_t)(rd(V(0x127a)) + R.ax));
        R.cx = rd(V(0x129c));
        if ((int16_t)rd(V(0x127a)) >= (int16_t)R.cx) wr(V(0x127a), R.cx);
    }
    if (DL & 0x10) {
        R.ax = rd(V(0x12a4));
        wr(V(0x127a), (uint16_t)(rd(V(0x127a)) - R.ax));
        R.cx = rd(V(0x12a0));
        if ((int16_t)rd(V(0x127a)) <= (int16_t)R.cx) wr(V(0x127a), R.cx);
        goto loop_end;
    }
    if (!(DL & 8)) goto loop_end;
    /* Car_StepAll_Fire 1000:4f03 (a label): fire before the start, or with [2915] = 1, coasts */
fire:
    if (cmpm(0x26c6, 2) == 2) goto coast;
    if (cmpm(0x2915, 1) == 1) goto coast;
    /* Car_StepAll_Hop 1000:4f17 (a label): the shot */
shot:
    if (cmpm(0x26c6, 2) == 2) goto loop_end;
    if (cmpm(0x2919, 1) != 1 && THEME != 7) goto loop_end;
    if (cmpm(V(0x13a4), 0) != 0) goto loop_end;
    if (cmpm(V(0x1250), 0) == 0) goto loop_end;
    {
        uint16_t dx = R.dx;
        if (cmpm(V(0x1250), 0) != 0) {
            SET_AH(5);
            SET_AL(0xe);
            mach_drv();
        }
        wr(V(0x1394), 1);
        wr(V(0x1396), 0);
        wr(V(0x13a4), 0x3c);
        wr(V(0x139a), 0xa);
        wr(V(0x139e), 0xa);
        R.si = rd(V(0x1278)) & 0xf8;
        for (int axis = 0; axis < 2; axis++) {
            SET_AL(rd8((uint16_t)(R.si + 0x10a0)));
            R.dx = (uint16_t)(rd(V(axis ? 0x1276 : 0x1272)) + rd(V(axis ? 0x1264 : 0x1258)));
            SET_AH((DH + 8) & 0xf);
            wr(V(axis ? 0x13a2 : 0x13a0), R.ax);
            SET_AH(0);
            R.ax = (uint16_t)(int16_t)(int8_t)AL;
            R.ax = (uint16_t)((int16_t)R.ax >> 3);
            R.ax = (uint16_t)(R.ax + rd(V(axis ? 0x1268 : 0x125c)));
            wr(V(axis ? 0x139c : 0x1398), R.ax);
            if (!axis) R.si = (uint16_t)((R.si - 0x40) & 0xf8);
        }
        R.dx = dx;
        goto loop_end;
    }

coast:
    if ((int16_t)cmpm(V(0x12d6), 0) > 0) goto loop_end;
drag:
    /* towards 0 by the drag 12a6, not past it */
    R.ax = rd(V(0x12a6));
    R.cx = rd(V(0x127a));
    if ((int16_t)R.cx >= -1) {
        wr(V(0x127a), (uint16_t)(rd(V(0x127a)) - R.ax));
        if ((int16_t)rd(V(0x127a)) < 0) wr(V(0x127a), 0);
    } else {
        wr(V(0x127a), (uint16_t)(rd(V(0x127a)) + R.ax));
        if ((int16_t)rd(V(0x127a)) >= 0) wr(V(0x127a), 0);
    }

loop_end:
    if (R.bx != 0x42c) {
        R.bx = (uint16_t)(R.bx + 0x164);
        goto next_record;
    }

    R.bx = rd(0x2660);
    Car_Velocity();
    R.bx = rd(0x2662);
    Car_Velocity();
    R.bx = rd(0x2664);
    Car_Velocity();
    R.bx = rd(0x2666);
    Car_Velocity();
    Coll_CarsAll();
    R.bx = rd(0x2660);
    Car_Physics();
    R.bx = rd(0x2662);
    Car_Physics();
    R.bx = rd(0x2664);
    Car_Physics();
    R.bx = rd(0x2666);
    Car_Physics();

    /* the camera's target */
    R.di = (uint16_t)((rd(0x27b5) << 1) + 0x27b7);
    R.bx = rd(R.di);
    if (R.bx != 1) {
        R.ax = (uint16_t)(rd(V(0x125c)) - rd(V(0x1262)));
        if ((int16_t)R.ax <= -1) R.ax = (uint16_t)(R.ax + 0xc00);
        wr(0x2646, R.ax);
        R.ax = (uint16_t)(rd(V(0x1268)) - rd(V(0x126e)));
        if ((int16_t)R.ax <= -1) R.ax = (uint16_t)(R.ax + 0xc00);
        wr(0x2648, R.ax);
    } else {
        R.bx = rd(0x2660);
        R.ax = rd(V(0x125c));
        R.bx = rd(0x2662);
        R.cx = rd(V(0x125c));
        R.ax = (uint16_t)(R.ax - R.cx);
        if ((int16_t)R.ax >= 0xb18) R.ax = (uint16_t)(R.ax - 0xc00);
        if ((int16_t)R.ax <= (int16_t)0xf4e8) R.ax = (uint16_t)(R.ax + 0xc00);
        if ((int16_t)R.ax > 0xe8 || (int16_t)R.ax < (int16_t)0xff18) {
            if (rd(0x2911) != 2) wr(0x2911, 1);
            goto camera_move;
        }
        R.ax = (uint16_t)((uint16_t)((int16_t)R.ax >> 1) + R.cx - 0x80);
        R.bx = rd(0x2660);
        R.dx = rd(V(0x1268));
        R.bx = rd(0x2662);
        R.cx = rd(V(0x1268));
        R.dx = (uint16_t)(R.dx - R.cx);
        if ((int16_t)R.dx >= 0xb50) R.dx = (uint16_t)(R.dx - 0xc00);
        if ((int16_t)R.dx <= (int16_t)0xf4b0) R.dx = (uint16_t)(R.dx + 0xc00);
        if ((int16_t)R.dx > 0xb0 || (int16_t)R.dx < (int16_t)0xff50) {
            if (rd(0x2911) != 2) wr(0x2911, 1);
            goto camera_move;
        }
        R.dx = (uint16_t)((uint16_t)((int16_t)R.dx >> 1) + R.cx - 0x64);
        if ((int16_t)R.ax <= -1) R.ax = (uint16_t)(R.ax + 0xc00);
        if ((int16_t)R.ax >= 0xc00) R.ax = (uint16_t)(R.ax - 0xc00);
        if ((int16_t)R.dx <= -1) R.dx = (uint16_t)(R.dx + 0xc00);
        if ((int16_t)R.dx >= 0xc00) R.dx = (uint16_t)(R.dx - 0xc00);
        wr(0x2646, R.ax);
        wr(0x2648, R.dx);
    }
camera_move:
    wr(0x137e, 0);
    wr(0x14e2, 0);
    wr(0x1646, 0);
    wr(0x17aa, 0);
    for (int axis = 0; axis < 2; axis++) {
        uint16_t speed = axis ? 0x2650 : 0x264e;
        uint16_t cam = axis ? 0x264c : 0x264a;
        R.cx = rd(speed);
        R.ax = (uint16_t)(rd(axis ? 0x2648 : 0x2646) - rd(cam));
        R.dx = R.ax;
        if ((int16_t)R.ax <= -1) R.ax = (uint16_t)-R.ax;
        if ((int16_t)R.ax > 0x3e8 || (int16_t)R.ax <= (int16_t)R.cx) {
            wr(speed, 0x32);   /* a jump (or arrived): move all the way */
        } else {
            R.ax = R.cx;
            if ((int16_t)R.dx <= -1) R.ax = (uint16_t)-R.ax;
            R.dx = R.ax;
        }
        wr(cam, (uint16_t)(rd(cam) + R.dx));
    }
    if (cmpm(0x2650, 0x32) == 0x32 && cmpm(0x264e, 0x32) == 0x32) {
        wr(0x137e, 1);
        wr(0x14e2, 1);
        wr(0x1646, 1);
        wr(0x17aa, 1);
    }
    R.dx = dx_saved;
}

/* Car_TurnRate 1000:4edc
   CX = vehicle BX's turn step 129a (set per theme by the race set-up); on theme 7 a computer car turns
   one step faster, a human's half as fast at a speed of 320h and up (signed). CF as the last compare
   (or the shift) leaves it. */
void Car_TurnRate(void)
{
    R.cx = rd(V(0x129a));
    cmp8(THEME, 7);
    if (THEME != 7) return;
    if (cmpm(V(0x12eb), 1) == 1) {
        R.cx++;
        R.zf = R.cx == 0, R.sf = (R.cx & 0x8000) != 0;
        return;
    }
    if ((int16_t)cmpm(V(0x127a), 0x320) < 0x320) return;
    R.cf = R.cx & 1;
    R.cx = (uint16_t)((int16_t)R.cx >> 1);
    R.zf = R.cx == 0, R.sf = (R.cx & 0x8000) != 0;
}

/* Car_HopStep 1000:51b2
   Vehicle BX's shell (named a hop before the shot was understood; started at 1000:4f17): the timer
   13a4 counts down from 60; while it is 28h or more the shell flies: the step 1396 counts up to 5
   (the drawing's frame), the position moves by six sub-steps per axis (Car_ShellStepFrac: the sine
   byte of 13a0/13a2 added to a 4-bit fraction in the step word's top nibble, carrying into the
   position), then by the car's own step at the shot (the step word's second nibble less 8);
   139a/139e count down to 0 (139e follows 139a, the height of the arc as drawn). CF = 1 when the
   timer is below 28h, else 0. */
void Car_HopStep(void)
{
    if (cmpm(V(0x13a4), 0) == 0) return;
    wr(V(0x13a4), (uint16_t)(rd(V(0x13a4)) - 1));
    if (cmpm(V(0x13a4), 0x28) < 0x28) return;
    R.cx = (uint16_t)(rd(V(0x1396)) >> 2);   /* unused: overwritten below, as the original */
    if (cmpm(V(0x1396), 5) < 5) wr(V(0x1396), (uint16_t)(rd(V(0x1396)) + 1));
    for (int axis = 0; axis < 2; axis++) {
        uint16_t step = axis ? 0x13a2 : 0x13a0, pos = axis ? 0x139c : 0x1398;
        R.ax = rd(V(step));
        R.cx = rd(V(pos));
        for (int i = 0; i < 6; i++) Car_ShellStepFrac();
        R.dx = rd(V(step)) & 0xf00;
        R.ax = (uint16_t)((R.ax & 0xf0ff) | R.dx);
        wr(V(step), R.ax);
        SET_AL((AH & 0xf) - 8);
        R.ax = (uint16_t)(int16_t)(int8_t)AL;
        R.cf = (uint32_t)R.cx + R.ax > 0xffff;   /* add cx, ax: the next call sees its carry */
        R.cx = (uint16_t)(R.cx + R.ax);
        R.zf = R.cx == 0, R.sf = (R.cx & 0x8000) != 0;
        wr(V(pos), R.cx);
    }
    R.ax = rd(V(0x139a));
    R.cf = false, R.zf = R.ax == 0, R.sf = (R.ax & 0x8000) != 0;   /* or ax, ax */
    if (R.ax == 0) return;
    R.ax--;
    wr(V(0x139a), R.ax);
    wr(V(0x139e), (uint16_t)(rd(V(0x139e)) - 1));
    R.zf = rd(V(0x139e)) == 0, R.sf = (rd(V(0x139e)) & 0x8000) != 0;
}

/* (ax = signed byte AL) * CX, then ax = the product's bits 7..22: speed times sine back to 8.8 */
static void velocity_component(void)
{
    R.ax = (uint16_t)(int16_t)(int8_t)AL;
    int32_t p = (int32_t)(int16_t)R.ax * (int16_t)R.cx;
    R.ax = (uint16_t)p;
    R.dx = (uint16_t)((uint32_t)p >> 16);
    R.ax = (uint16_t)(((R.dx & 0xff) << 8 | AH) << 1);
}

/* Car_Velocity 1000:525e
   Vehicle BX (active, state 0, not in the air: 12d6 <= 0, not waiting for the camera: 1380 = 0):
   - the wanted velocity 1270/1274 = speed 127a times sin(heading) / sin(heading - 40h) (the signed
     bytes of 193c:10a0, every eighth: 32 directions), / 128;
   - the grip: the step CX = 127e and the slip threshold DX = 127c, both half as much again for a
     catching-up computer car (262f); while a slide timer runs (1286 first, else 1284, each counted
     down) the theme's slide values [28c4]/[28c2] instead;
   - per axis, if the car can slide (1280) and the wanted velocity differs from the actual 1272/1276
     by more than DX, the actual one moves towards it by CX and the car is sliding (1282 = 1); else it
     takes the wanted one (1282 = 0; 1282 ends as the y axis's);
   - the camera's look-ahead 1262 = 80h, 126e = 64h.
   Then, for any car with 1282 = 1 (even an inactive one, as the original): not on themes 2 and 8;
   on theme 6 at most every 50 calls ([28fb]); the skid effect 5 when sound is on ([0f64] = 1) for
   record 0 if it is active, human and [26b8] = 1. */
void Car_Velocity(void)
{
    if (cmpm(V(0x124c), 0) == 0) goto skid;
    if (cmpm(V(0x12ae), 0) != 0) goto skid;
    if ((int16_t)cmpm(V(0x12d6), 0) > 0) goto skid;
    if (cmpm(V(0x1380), 0) != 0) goto skid;
    catch_up_flag();
    SET_AL(CL);
    R.cx = rd(V(0x127a));
    R.si = (uint16_t)((rd(V(0x1278)) & 0xf8) + 0x10a0);
    SET_AL(rd8(R.si));   /* lodsb */
    R.si++;
    velocity_component();
    wr(V(0x1270), R.ax);
    R.si = (uint16_t)(rd(V(0x1278)) - 0x40);
    if ((int16_t)R.si <= -1) R.si = (uint16_t)(R.si + 0x100);
    R.si = (uint16_t)((R.si & 0xf8) + 0x10a0);
    SET_AL(rd8(R.si));
    R.si++;
    velocity_component();
    wr(V(0x1274), R.ax);
    R.cx = rd(V(0x127e));
    R.dx = rd(V(0x127c));
    if (rd8(0x262f) != 0) {
        R.cx = (uint16_t)(R.cx + (R.cx >> 1));
        R.dx = (uint16_t)(R.dx + (R.dx >> 1));
    }
    if (rd(V(0x1286)) != 0) {
        R.cx = rd(0x28c4);
        R.dx = rd(0x28c2);
        wr(V(0x1286), (uint16_t)(rd(V(0x1286)) - 1));
    } else if (rd(V(0x1284)) != 0) {
        R.cx = rd(0x28c4);
        R.dx = rd(0x28c2);
        wr(V(0x1284), (uint16_t)(rd(V(0x1284)) - 1));
    }
    for (int axis = 0; axis < 2; axis++) {
        uint16_t want = axis ? 0x1274 : 0x1270, act = axis ? 0x1276 : 0x1272;
        bool slide = false;
        if (rd(V(0x1280)) != 0) {
            R.ax = (uint16_t)(rd(V(want)) - rd(V(act)));
            if ((int16_t)R.ax <= -1) R.ax = (uint16_t)-R.ax;
            slide = (int16_t)R.ax > (int16_t)R.dx;
        }
        if (!slide) {
            wr(V(0x1282), 0);
            R.ax = rd(V(want));
        } else {
            wr(V(0x1282), 1);
            R.ax = rd(V(act));
            if ((int16_t)R.ax < (int16_t)rd(V(want)))
                R.ax = (uint16_t)(R.ax + R.cx);
            else
                R.ax = (uint16_t)(R.ax - R.cx);
        }
        wr(V(act), R.ax);
    }
    wr(V(0x1262), 0x80);
    wr(V(0x126e), 0x64);
skid:
    if (cmpm(V(0x1282), 1) != 1) return;
    cmp8(THEME, 2);
    if (THEME == 2) return;
    cmp8(THEME, 8);
    if (THEME == 8) return;
    cmp8(THEME, 6);
    if (THEME == 6) {
        R.ax = rd(0x28fb);
        R.cf = false, R.zf = R.ax == 0, R.sf = (R.ax & 0x8000) != 0;   /* or ax, ax */
        if (R.ax != 0) {
            R.ax--;
            wr(0x28fb, R.ax);
            R.zf = R.ax == 0, R.sf = (R.ax & 0x8000) != 0;
            return;
        }
    }
    wr(0x28fb, 0x32);
    if (cmpm(0x0f64, 1) != 1) return;
    cmp16(R.bx, 0);
    if (R.bx != 0) return;
    if (cmpm(V(0x124c), 1) != 1) return;
    if (cmpm(V(0x1250), 0) == 0) return;
    if (cmpm(0x26b8, 1) != 1) return;
    if (cmpm(V(0x1250), 0) == 0) return;
    SET_AH(5);
    SET_AL(5);
    mach_drv();
}

/* Car_MoveProbe's test before taking a checkpoint: on theme 3 (the bridges) a tile marked 1 in
   193c:25cc counts only on the upper level (1388 = 1). Leaves SI and AL as the lodsb does. */
static bool lower_bridge(void)
{
    cmp8(THEME, 3);
    if (THEME != 3) return false;
    if (cmpm(V(0x1388), 1) == 1) return false;
    R.si = (uint16_t)(0x25cc + rd(V(0x12cc)));
    SET_AL(rd8(R.si));   /* lodsb */
    R.si++;
    cmp8(AL, 1);
    return AL == 1;
}

/* Car_MoveProbe in detail (docs/re/vehicles.md, "The move probe"). A solid cell (CF from Track_TileAt)
   blocks unless the surface lets the car through: theme 7 classes 1..7, themes 4 and 5 classes 5 and
   up (the levels the terrain handles), theme 3 surfaces with all of e0h. Blocked: the checkpoint is
   still taken; with 1390 (a boost stage) the car goes off the track after 50 blocked steps (13ac);
   else effect 6 (theme 2: 4) for a human car, and the points 8 px left, right, up and down of the
   committed position are probed, each solid one setting 12c4/12c6/12c8/12ca (none: 12c8 and 12c4),
   and the bounce request 12a8 = 1. The "down" probe wraps x instead of y (cmp ax at 56fc, as the
   original), so y + 8 past the world's edge reads map row 32: the checkpoint layer.
   Theme 3 off the solid cells: with the edge flag 10h on the previous surface and class 0 now, the
   edge handler (Terrain_Edge); with 10h on the new one and none of a0h the previous surface is kept,
   the checkpoint taken, 138a = 1 and the speed capped at 100h, then the blocked path (a wall's
   side); with a0h too the new surface is also kept as the previous. */

/* Car_MoveProbe 1000:5532
   Vehicle BX (active, state 0): the candidate position, the tile under it (Track_TileAt: 12cc the tile,
   12de its orientation, 12ce the 8-px cell) and its surface byte (Track_SurfaceAt: 12da, the previous
   in 12dc). The checkpoint number from the map's second layer (CX) is taken when not 0 (12e3 -> 12e1,
   CX -> 12e3, 12e5 = 1), except a tile marked in 193c:25cc on theme 3 while below the bridge (1388 not
   1); checkpoint ffh is off the track (state 0dh, the position kept in 12ba/12bc). DX, ES kept. */
void Car_MoveProbe(void)
{
    uint16_t dx = R.dx, es = R.es;
    if (cmpm(V(0x124c), 0) == 0) goto done;
    if (cmpm(V(0x12ae), 0) != 0) goto done;
    candidate();
    cmp8(THEME, 3);
    if (THEME == 3) goto theme3;

    R.ax = rd(V(0x125e));
    R.dx = rd(V(0x126a));
    Track_TileAt();
    wr(V(0x12cc), R.si);
    wr(V(0x12de), R.di);
    wr(V(0x12ce), R.cx);
    wr(V(0x12e5), 0);
    R.cx = R.es;
    Track_SurfaceAt();
    if (!R.cf) goto clear;
    cmp8(THEME, 7);
    if (THEME == 7) {
        R.ax = rd(V(0x12da)) >> 4;
        cmp16(R.ax, 8);
        if ((int16_t)R.ax >= 8) goto solid;
        cmp16(R.ax, 1);
        if ((int16_t)R.ax >= 1) goto clear;
    }
    cmp8(THEME, 4);
    if (THEME == 4 || (cmp8(THEME, 5), THEME == 5)) {
        R.ax = rd(V(0x12da)) >> 4;
        cmp16(R.ax, 5);
        if ((int16_t)R.ax >= 5) goto clear;
    }
solid:
    cmp16(R.cx, 0);
    if (R.cx != 0 && !lower_bridge()) {
        R.ax = rd(V(0x12e3));
        wr(V(0x12e1), R.ax);
        wr(V(0x12e3), R.cx);
        wr(V(0x12e5), 1);
        if (cmpm(V(0x12e3), 0xff) == 0xff) goto off_track;
    }
blocked:
    if (cmpm(V(0x1390), 0) != 0) {
        wr(V(0x13ac), (uint16_t)(rd(V(0x13ac)) + 1));
        if ((int16_t)cmpm(V(0x13ac), 0x32) <= 0x32) goto probe;
        wr(V(0x13ac), 0);
        goto off_track;
    }
    wr(V(0x13ac), 0);
    cmp8(THEME, 2);
    if (THEME != 2) {
        if (cmpm(V(0x1250), 0) != 0) {
            SET_AH(5);
            SET_AL(6);
            mach_drv();
        }
    } else if (cmpm(V(0x1250), 0) != 0) {
        SET_AH(5);
        SET_AL(4);
        mach_drv();
    }
probe:
    R.ax = (uint16_t)(rd(V(0x125c)) - 8);
    R.dx = rd(V(0x1268));
    if ((int16_t)R.ax <= -1) R.ax = (uint16_t)(R.ax + 0xc00);
    Track_TileAt();
    if (R.cf) wr(V(0x12c4), 1);
    R.ax = (uint16_t)(R.ax + 0x10);
    if ((int16_t)R.ax >= 0xc00) R.ax = (uint16_t)(R.ax - 0xc00);
    Track_TileAt();
    if (R.cf) wr(V(0x12c6), 1);
    R.dx = (uint16_t)(R.dx - 8);
    if ((int16_t)R.dx <= -1) R.dx = (uint16_t)(R.dx + 0xc00);
    R.ax = (uint16_t)(R.ax - 8);
    if ((int16_t)R.ax <= -1) R.ax = (uint16_t)(R.ax + 0xc00);
    Track_TileAt();
    if (R.cf) wr(V(0x12c8), 1);
    R.dx = (uint16_t)(R.dx + 0x10);
    if ((int16_t)R.ax >= 0xc00) R.ax = (uint16_t)(R.ax - 0xc00);   /* ax, not dx: as the original */
    Track_TileAt();
    if (R.cf) wr(V(0x12ca), 1);
    if (cmpm(V(0x12c8), 1) != 1 && cmpm(V(0x12ca), 1) != 1 && cmpm(V(0x12c4), 1) != 1 &&
        cmpm(V(0x12c6), 1) != 1) {
        wr(V(0x12c8), 1);
        wr(V(0x12c4), 1);
    }
    wr(V(0x12a8), 1);
    goto done;

theme3:
    R.ax = rd(V(0x125e));
    R.dx = rd(V(0x126a));
    Track_TileAt();
    wr(V(0x12cc), R.si);
    wr(V(0x12de), R.di);
    wr(V(0x12ce), R.cx);
    wr(V(0x12e5), 0);
    R.cx = R.es;
    Track_SurfaceAt();
    if (R.cf) {
        R.ax = rd(V(0x12da)) & 0xe0;
        cmp16(R.ax, 0xe0);
        if (R.ax == 0xe0) goto clear;
        goto solid;
    }
    R.cf = false;
    if (rd(V(0x12dc)) & 0x10) {
        R.zf = (rd(V(0x12da)) & 0xf0) == 0, R.sf = false;   /* test [12da], f0h */
        if (!R.zf) goto clear;
        Terrain_Edge();
        goto clear;
    }
    if (!(rd(V(0x12da)) & 0x10)) goto clear;
    R.ax = rd(V(0x12da)) & 0xa0;
    if (R.ax != 0) {
        R.ax = rd(V(0x12da));
        wr(V(0x12dc), R.ax);
        goto clear;
    }
    R.ax = rd(V(0x12dc));
    wr(V(0x12da), R.ax);
    cmp16(R.cx, 0);
    if (R.cx != 0) {
        bool take = cmpm(V(0x1388), 1) == 1;
        if (!take) {
            R.si = (uint16_t)(0x25cc + rd(V(0x12cc)));
            SET_AL(rd8(R.si));
            R.si++;
            cmp8(AL, 1);
            take = AL != 1;
        }
        if (take) {
            R.ax = rd(V(0x12e3));
            wr(V(0x12e1), R.ax);
            wr(V(0x12e3), R.cx);
            wr(V(0x12e5), 1);
            if (cmpm(V(0x12e3), 0xff) == 0xff) goto off_track;
        }
    }
    wr(V(0x138a), 1);
    if ((int16_t)cmpm(V(0x127a), 0x100) >= 0x100) wr(V(0x127a), 0x100);
    goto blocked;

clear:
    wr(V(0x12a8), 0);
    wr(V(0x12e5), 0);
    cmp16(R.cx, 0);
    if (R.cx == 0 || lower_bridge()) goto done;
    R.ax = rd(V(0x12e3));
    wr(V(0x12e1), R.ax);
    wr(V(0x12e3), R.cx);
    wr(V(0x12e5), 1);
    if (cmpm(V(0x12e3), 0xff) != 0xff) goto done;
off_track:
    R.ax = rd(V(0x125c));
    wr(V(0x12ba), R.ax);
    R.ax = rd(V(0x1268));
    wr(V(0x12bc), R.ax);
    wr(V(0x12ae), 0xd);
done:
    R.es = es;
    R.dx = dx;
}

/* Track_SurfaceAt 1000:585b
   The surface byte of vehicle BX's spot: the tile's 6 x 6 grid of 16-px cells at 193c:35e3 + tile
   12cc * 24h (the tile's low byte), the cell from 12ce (CH = x / 8, CL = y / 8, halved). 12da -> 12dc,
   the byte -> 12da (high nibble the terrain class, low nibble a direction for the computer driver and
   flags). Out: AX = the byte, SI past it, DS = 193c; CX and the flags kept (pushf, popf). */
void Track_SurfaceAt(void)
{
    uint16_t cx = R.cx;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.ax = (uint16_t)((rd(V(0x12cc)) & 0xff) * 0x24);
    R.si = (uint16_t)(R.ax + 0x35e3);
    R.ax = rd(V(0x12ce));
    R.cx = R.ax;
    R.ax = (uint16_t)(AH >> 1);
    R.si = (uint16_t)(R.si + R.ax);
    R.cx = (uint16_t)((R.cx & 0xff) >> 1 << 1);
    R.si = (uint16_t)(R.si + 3 * R.cx);
    R.ax = rd8(R.si);
    R.si++;
    wr(V(0x12dc), rd(V(0x12da)));
    wr(V(0x12da), R.ax);
    R.cx = cx;
}

/* Track_TileAt 1000:589c
   AX = world x, DX = world y (0..bffh; 96-px tiles, 32 x 32): the map byte at 193c:2963 + (y / 96) * 32
   + x / 96 gives the tile (low 6 bits, out in SI) and its orientation (top 2 bits, DI); the second
   layer at 193c:2d63 the checkpoint (ES); CX = the 8-px cell in the tile (CH = x, CL = y, 0..11).
   CF = the cell's bit in the tile's 12 x 12 solidity mask (193c:3163 + tile * 18, bit 7 first);
   ZF = not solid. AX, BX, DX kept. A y of c00h or more (the probe at 1000:5704) reads row 32 and on:
   the checkpoint layer taken for map bytes, as the original. */
void Track_TileAt(void)
{
    uint16_t ax = R.ax, bx = R.bx, dx = R.dx;
    /* div cl by 96: a quotient over ffh would fault (a divide error); x and y are below c00h */
    uint8_t qx = (uint8_t)(ax / 0x60), rx = (uint8_t)(ax % 0x60);
    uint8_t qy = (uint8_t)(dx / 0x60), ry = (uint8_t)(dx % 0x60);
    uint8_t cellx = rx >> 3, celly = ry >> 3;
    uint16_t cell = (uint16_t)(qy << 5) + qx;
    uint8_t map = rd8((uint16_t)(cell + 0x2963));
    R.di = map >> 6;
    uint16_t tile = map & 0x3f;
    R.es = rd8((uint16_t)(cell + 0x2d63));
    uint8_t bit = (uint8_t)(celly * 8 + celly * 4 + cellx);
    R.si = (uint16_t)(tile * 18 + bit / 8 + 0x3163);
    uint8_t al = rd8(R.si) & (uint8_t)(0x80 >> (bit % 8));
    R.si++;
    R.cf = al != 0, R.zf = al == 0, R.sf = (al & 0x80) != 0;
    R.cx = (uint16_t)(cellx << 8 | celly);
    R.si = tile;
    R.ax = ax, R.bx = bx, R.dx = dx;
}

/* Car_Physics 1000:5be7
   Vehicle BX's move for the step (active only):
   - an impulse from a collision (12ac) recomputes the candidate position first (the flag cleared);
   - Car_MoveProbe and the terrain's handler (Terrain_Dispatch);
   - a blocked move (12fb set and 12a8): a computer car counts 12aa and at 20 goes off the track
     (state 0dh) - stuck against a wall; the velocity is reversed on the blocked axes (x for 12c4 or
     12c6, y for 12c8 or 12ca; both when none is set), halved (sar) when 138a; the flags cleared; the
     candidate position recomputed from the reversed velocity;
   - in states 0, 2 and 0eh the candidate position and fractions are committed (125c/1268,
     1258/1264);
   - then (any state that got here) the tile's respawn byte (the word pointer [28b9] + tile & 3fh) into
     12e0; unless its bit 7 is set (or on theme 3 the bridge layer doesn't match: 25cc + tile = 1
     while below, 0 while above) the position is a safe point: 12f1/12f3 -> 12f5/12f7, the position ->
     12f1/12f3;
   - 138a = 1; with [2919] = 1 or on theme 7 the shells' hits (Coll_ShellHitsCar). */
void Car_Physics(void)
{
    if (cmpm(V(0x124c), 0) == 0) goto shells;
    if (cmpm(V(0x12ac), 0) != 0) {
        wr(V(0x12ac), 0);
        candidate();
    }
    Car_MoveProbe();
    Terrain_Dispatch();
    if (cmpm(V(0x12fb), 0) == 0 || cmpm(V(0x12a8), 0) == 0) goto commit;
    if (cmpm(V(0x12eb), 0) != 0) {
        wr(V(0x12aa), (uint16_t)(rd(V(0x12aa)) + 1));
        if ((int16_t)cmpm(V(0x12aa), 0x14) >= 0x14) {
            R.ax = rd(V(0x125c));
            wr(V(0x12ba), R.ax);
            R.ax = rd(V(0x1268));
            wr(V(0x12bc), R.ax);
            wr(V(0x12ae), 0xd);
        }
    }
    if (rd(V(0x12c4)) == 0 && rd(V(0x12c6)) == 0 && rd(V(0x12c8)) == 0 && rd(V(0x12ca)) == 0) {
        wr(V(0x1272), (uint16_t)-rd(V(0x1272)));
        wr(V(0x1276), (uint16_t)-rd(V(0x1276)));
        if (rd(V(0x138a)) != 0) {
            wr(V(0x1272), (uint16_t)(SV(0x1272) >> 1));
            wr(V(0x1276), (uint16_t)(SV(0x1276) >> 1));
        }
    } else {
        if (rd(V(0x12c4)) == 1 || rd(V(0x12c6)) == 1) {
            wr(V(0x1272), (uint16_t)-rd(V(0x1272)));
            if (rd(V(0x138a)) != 0) wr(V(0x1272), (uint16_t)(SV(0x1272) >> 1));
        }
        if (rd(V(0x12c8)) == 1 || rd(V(0x12ca)) == 1) {
            wr(V(0x1276), (uint16_t)-rd(V(0x1276)));
            if (rd(V(0x138a)) != 0) wr(V(0x1276), (uint16_t)(SV(0x1276) >> 1));
        }
    }
    wr(V(0x12c4), 0);
    wr(V(0x12c6), 0);
    wr(V(0x12c8), 0);
    wr(V(0x12ca), 0);
    candidate();
commit:
    if (cmpm(V(0x12ae), 0xe) != 0xe && cmpm(V(0x12ae), 2) != 2 && cmpm(V(0x12ae), 0) != 0) goto shells;
    R.ax = rd(V(0x125e));
    wr(V(0x125c), R.ax);
    R.ax = rd(V(0x126a));
    wr(V(0x1268), R.ax);
    R.ax = rd(V(0x125a));
    wr(V(0x1258), R.ax);
    R.ax = rd(V(0x1266));
    wr(V(0x1264), R.ax);
    R.si = (uint16_t)((rd(V(0x12cc)) & 0x3f) + rd(0x28b9));
    SET_AL(rd8(R.si));
    mbw(R.ds, V(0x12e0), AL);
    if (AL & 0x80) goto shells;
    if (THEME == 3) {
        bool upper = rd(V(0x1388)) == 1;
        R.si = (uint16_t)(0x25cc + rd(V(0x12cc)));
        SET_AL(rd8(R.si));
        R.si++;
        if (upper ? AL == 0 : AL == 1) goto shells;
    }
    R.ax = rd(V(0x12f1));
    wr(V(0x12f5), R.ax);
    R.ax = rd(V(0x125c));
    wr(V(0x12f1), R.ax);
    R.ax = rd(V(0x12f3));
    wr(V(0x12f7), R.ax);
    R.ax = rd(V(0x1268));
    wr(V(0x12f3), R.ax);
shells:
    wr(V(0x138a), 1);
    if (cmpm(0x2919, 1) == 1) {
        Coll_ShellHitsCar();
        return;
    }
    cmp8(THEME, 7);
    if (THEME == 7) Coll_ShellHitsCar();
}
