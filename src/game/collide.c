/* Vehicles against each other (docs/re/vehicles.md, "Collisions"): the four racers taken in pairs, a
   17 x 17 table of contact angles indexed by the two cars' offset, and an impulse along that angle
   from their relative velocity; and the shells the tanks fire (1000:4f17) hitting another vehicle.

   Registers as the original leaves them (docs/re/porting.md): the routines work on R directly, because
   the sound driver's calls in the middle see (and may change) the registers. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* DS-relative words and bytes (DS = 193c in the race), the vehicle record's field o (bx = the record) */
static inline uint16_t rd(uint16_t a) { return mw(R.ds, a); }
static inline void wr(uint16_t a, uint16_t v) { mww(R.ds, a, v); }
static inline uint8_t rd8(uint16_t a) { return mb(R.ds, a); }
#define V(o) ((uint16_t)(R.bx + (o)))
#define SV(o) ((int16_t)rd(V(o)))

/* cmp a, b: CF, ZF and SF as the instruction leaves them */
static inline void cmp16(uint16_t a, uint16_t b)
{
    uint16_t d = (uint16_t)(a - b);
    R.cf = a < b, R.zf = d == 0, R.sf = (d & 0x8000) != 0;
}

/* The candidate position for this step, as Car_MoveProbe computes it: velocity 1272/1276 (8.8) plus
   the fraction 1258/1264; the integer part (the high byte, signed) moves the position 125c/1268 into
   125e/126a, the low byte is the new fraction 125a/1266; both wrapped into the 0..bffh world. */
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

/* (dx:ax = ax * src, signed) then ax = the product's bits 7..22: a sine-table product (the table's
   unit is 128) back to the velocity's scale */
static void imul_scale(uint16_t src)
{
    int32_t p = (int32_t)(int16_t)R.ax * (int16_t)src;
    R.ax = (uint16_t)p;
    R.dx = (uint16_t)((uint32_t)p >> 16);
    uint16_t t = (uint16_t)((R.dx & 0xff) << 8 | AH);
    R.cf = (t & 0x8000) != 0;
    R.ax = (uint16_t)(t << 1);
}

/* Coll_CarsAll 1000:5921
   The six pairs of the racers in player order (records 193c:2660, 2662, 2664, 2666), each through
   Coll_CarPair with the pair in 193c:27b1/27b3: (1,2) (1,3) (1,4) (2,4) (2,3) (4,3), the order the
   original writes them. BX kept (pushed). */
void Coll_CarsAll(void)
{
    uint16_t bx = R.bx;
    R.ax = rd(0x2660);
    wr(0x27b1, R.ax);
    R.ax = rd(0x2662);
    wr(0x27b3, R.ax);
    Coll_CarPair();
    R.ax = rd(0x2664);
    wr(0x27b3, R.ax);
    Coll_CarPair();
    R.ax = rd(0x2666);
    wr(0x27b3, R.ax);
    Coll_CarPair();
    R.ax = rd(0x2662);
    wr(0x27b1, R.ax);
    Coll_CarPair();
    R.ax = rd(0x2664);
    wr(0x27b3, R.ax);
    Coll_CarPair();
    R.ax = rd(0x2666);
    wr(0x27b1, R.ax);
    Coll_CarPair();
    R.bx = bx;
}

/* Coll_CarPair 1000:5960
   First recomputes the candidate position of vehicle BX (whichever record BX holds: Coll_CarsAll's
   caller's, as the original), then tests the pair A = [27b1], B = [27b3]. Both active, A in state 0 or
   2 and B too (state 2 = just put back on the track); their candidate offset A - B, wrapped to the
   nearest copy across the world's edge, within 16 px on both axes. The offset halved (0..10h each way)
   picks a byte of the 17 x 17 table at 193c:17da: 0 = no contact (the corners), else the direction of
   the contact on the 256-step circle.
   - Either car in state 2: no physics, the two are pushed apart diagonally, A by +40h and B by -40h
     on both axes (whatever the angle), and both get the impulse flag 12ac.
   - Else the relative velocity (A - B) is projected on the contact direction: s = sin(angle),
     c = sin(angle - 40h) from the table at 193c:10a0, j = (dvx * s - dvy * c) / 128 (each product
     scaled separately), at least 1f4h. A loses j * s / 128 on x and j * c / 128 on y, B gains them;
     both get 12ac. Sound effect 3 if B is a human's. On theme 6 a hit harder than 1f4h knocks each
     human-driven car of the two off the track (state 0dh, its position kept in 12ba/12bc, effect 1).
   BX is left at B (or wherever the tests stopped); the caller saves it. */
void Coll_CarPair(void)
{
    candidate();
    R.bx = rd(0x27b1);
    cmp16(rd(V(0x124c)), 0);
    if (rd(V(0x124c)) == 0) return;
    R.dx = rd(V(0x125e));
    R.di = rd(V(0x126a));
    cmp16(rd(V(0x12ae)), 2);
    if (rd(V(0x12ae)) != 2) {
        cmp16(rd(V(0x12ae)), 0);
        if (rd(V(0x12ae)) != 0) return;
        R.bx = rd(0x27b3);
        cmp16(rd(V(0x124c)), 0);
        if (rd(V(0x124c)) == 0) return;
        cmp16(rd(V(0x12ae)), 2);
        if (rd(V(0x12ae)) != 2) {
            cmp16(rd(V(0x12ae)), 0);
            if (rd(V(0x12ae)) != 0) return;
        }
    }
    R.bx = rd(0x27b3);
    R.dx = (uint16_t)(R.dx - rd(V(0x125e)));
    R.di = (uint16_t)(R.di - rd(V(0x126a)));
    /* the nearer copy across the edge (the world wraps at c00h) */
    cmp16(R.dx, 0xbf0);
    if ((int16_t)R.dx >= 0xbf0) R.dx = (uint16_t)(R.dx - 0xc00);
    cmp16(R.dx, 0xf410);
    if ((int16_t)R.dx <= (int16_t)0xf410) R.dx = (uint16_t)(R.dx + 0xc00);
    cmp16(R.di, 0xbf0);
    if ((int16_t)R.di >= 0xbf0) R.di = (uint16_t)(R.di - 0xc00);
    cmp16(R.di, 0xf410);
    if ((int16_t)R.di <= (int16_t)0xf410) R.di = (uint16_t)(R.di + 0xc00);
    cmp16(R.dx, 0x10);
    if ((int16_t)R.dx > 0x10) return;
    cmp16(R.dx, 0xfff0);
    if ((int16_t)R.dx < -0x10) return;
    cmp16(R.di, 0x10);
    if ((int16_t)R.di > 0x10) return;
    cmp16(R.di, 0xfff0);
    if ((int16_t)R.di < -0x10) return;
    R.dx = (uint16_t)((uint16_t)(R.dx + 0x10) >> 1);
    R.di = (uint16_t)((uint16_t)(R.di + 0x10) >> 1);
    R.ax = R.di;
    SET_CL(4);
    R.di = (uint16_t)((R.di << 4) + R.ax + R.dx + 0x17da);
    cmp16(R.di, 0x18fb);
    if ((int16_t)R.di >= 0x18fb) return;   /* cannot happen: the offsets are 0..10h */
    R.ax = rd8(R.di);
    R.si = R.ax;
    cmp16(R.si, 0);
    if (R.si == 0) return;

    R.bx = rd(0x27b1);
    cmp16(rd(V(0x12ae)), 2);
    bool respawned = rd(V(0x12ae)) == 2;
    if (!respawned) {
        R.bx = rd(0x27b3);
        cmp16(rd(V(0x12ae)), 2);
        respawned = rd(V(0x12ae)) == 2;
    }
    if (respawned) {
        R.bx = rd(0x27b1);
        wr(V(0x12ac), 1);
        wr(V(0x1272), 0x40);
        wr(V(0x1276), 0x40);
        R.bx = rd(0x27b3);
        wr(V(0x12ac), 1);
        wr(V(0x1272), 0xffc0);
        wr(V(0x1276), 0xffc0);
        return;
    }

    R.bx = rd(0x27b1);
    R.cx = rd(V(0x1272));
    R.di = rd(V(0x1276));
    R.bx = rd(0x27b3);
    R.cx = (uint16_t)(R.cx - rd(V(0x1272)));
    R.di = (uint16_t)(R.di - rd(V(0x1276)));
    R.si = (uint16_t)(R.si + 0x10a0);
    R.ax = (uint16_t)(int16_t)(int8_t)rd8(R.si);   /* lodsb, cbw: s */
    R.si++;
    R.bx = R.ax;
    R.si = (uint16_t)(R.si - 0x41);                /* 40h steps back: c */
    cmp16(R.si, 0x109f);
    if ((int16_t)R.si <= 0x109f) R.si = (uint16_t)(R.si + 0x100);
    R.ax = (uint16_t)(int16_t)(int8_t)rd8(R.si);
    R.si++;
    uint16_t t = R.cx;   /* xchg cx, ax: cx = c, ax = dvx */
    R.cx = R.ax;
    R.ax = t;
    imul_scale(R.bx);
    R.si = R.ax;
    R.ax = R.di;
    imul_scale(R.cx);
    R.si = (uint16_t)(R.si - R.ax);
    cmp16(R.si, 0x1f4);
    if ((int16_t)R.si <= 0x1f4) R.si = 0x1f4;
    cmp16(rd8(0x28bf), 6);
    if (rd8(0x28bf) == 6) {
        cmp16(R.si, 0x1f4);
        if ((int16_t)R.si > 0x1f4) {
            uint16_t s = R.bx;
            R.bx = rd(0x27b1);
            cmp16(rd(V(0x1250)), 0);
            if (rd(V(0x1250)) != 0) {
                R.ax = rd(V(0x125c));
                wr(V(0x12ba), R.ax);
                R.ax = rd(V(0x1268));
                wr(V(0x12bc), R.ax);
                wr(V(0x12ae), 0xd);
                cmp16(rd(V(0x1250)), 0);
                if (rd(V(0x1250)) != 0) {
                    SET_AH(5);
                    SET_AL(1);
                    mach_drv();
                }
                R.bx = rd(0x27b3);
                cmp16(rd(V(0x1250)), 0);
                if (rd(V(0x1250)) != 0) {
                    R.ax = rd(V(0x125c));
                    wr(V(0x12ba), R.ax);
                    R.ax = rd(V(0x1268));
                    wr(V(0x12bc), R.ax);
                    wr(V(0x12ae), 0xd);
                    cmp16(rd(V(0x1250)), 0);
                    if (rd(V(0x1250)) != 0) {
                        SET_AH(5);
                        SET_AL(1);
                        mach_drv();
                    }
                }
            }
            R.bx = s;
        }
    }
    R.ax = R.si;
    imul_scale(R.bx);
    R.bx = R.ax;
    R.ax = R.si;
    imul_scale(R.cx);
    R.cx = R.bx;
    R.bx = rd(0x27b1);
    wr(V(0x12ac), 1);
    wr(V(0x1272), (uint16_t)(rd(V(0x1272)) - R.cx));
    wr(V(0x1276), (uint16_t)(rd(V(0x1276)) - R.ax));
    R.bx = rd(0x27b3);
    wr(V(0x12ac), 1);
    wr(V(0x1272), (uint16_t)(rd(V(0x1272)) + R.cx));
    wr(V(0x1276), (uint16_t)(rd(V(0x1276)) + R.ax));
    cmp16(rd(V(0x1250)), 0);
    if (rd(V(0x1250)) != 0) {
        SET_AH(5);
        SET_AL(3);
        mach_drv();
    }
}

/* Coll_ShellHitsCar 1000:79fd
   Vehicle BX, if it can be hit at all (124e), against the shells of the four records (SI = 0, 164h,
   2c8h, 42ch, its own skipped): a shell in flight (1394 set, its timer 13a4 at 28h or more, unsigned)
   whose position 1398/139c is within 12 px of BX's on both axes (no wrap) ends (1394 = 0) and BX
   goes off (state 0dh, its position kept in 12ba/12bc), effect 1 if BX is a human's.
   Out: CX = 0, SI = 590h, CF = 0 (the last add). */
void Coll_ShellHitsCar(void)
{
    R.cx = 4;
    R.si = 0;
    do {
        uint16_t cx = R.cx;
        if (R.si != R.bx && rd((uint16_t)(R.si + 0x13a4)) >= 0x28 && rd((uint16_t)(R.si + 0x1394)) != 0 &&
            rd(V(0x124e)) != 0) {
            R.cx = (uint16_t)(rd(V(0x125c)) - rd((uint16_t)(R.si + 0x1398)));
            R.dx = rd(V(0x1268));
            if ((int16_t)R.cx <= 0xc && (int16_t)R.cx >= -0xc) {
                R.dx = (uint16_t)(R.dx - rd((uint16_t)(R.si + 0x139c)));
                if ((int16_t)R.dx <= 0xc && (int16_t)R.dx >= -0xc) {
                    wr((uint16_t)(R.si + 0x1394), 0);
                    R.ax = rd(V(0x125c));
                    wr(V(0x12ba), R.ax);
                    R.ax = rd(V(0x1268));
                    wr(V(0x12bc), R.ax);
                    wr(V(0x12ae), 0xd);
                    if (rd(V(0x1250)) != 0) {
                        R.cf = false, R.zf = false, R.sf = false;   /* cmp [1250], 0 */
                        SET_AH(5);
                        SET_AL(1);
                        mach_drv();
                    }
                }
            }
        }
        R.cx = cx;
        R.si = (uint16_t)(R.si + 0x164);
        R.cx--;
    } while (R.cx != 0);
    R.cf = false, R.zf = false, R.sf = false;
}
