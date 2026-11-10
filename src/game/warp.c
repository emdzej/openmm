/* The warp holes, the respawn and the slide into a hole (docs/re/terrain.md).
   Car_Warp is theme 3's class 4 (the dispatch, a vehicle in state 0) and vehicle state 0eh (the state
   table 193c:278f): a vehicle that drives into a warp hole of the list 193c:22e1 waits in its group's
   queue, travels to the exit and comes out there. Car_Respawn is vehicle state 7 (and theme 9's class
   15): back on the track at the last safe point. Car_SinkStep moves a vehicle sliding into a hole.

   Register-exact as the original (docs/re/porting.md); BX = the vehicle's record offset. */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

static uint8_t db(uint16_t off) { return mb(R.ds, off); }
static uint16_t dw(uint16_t off) { return mw(R.ds, off); }
static void dww(uint16_t off, uint16_t v) { mww(R.ds, off, v); }
static uint16_t vw(uint16_t f) { return mw(R.ds, (uint16_t)(R.bx + f)); }
static int16_t vs(uint16_t f) { return (int16_t)vw(f); }
static void vww(uint16_t f, uint16_t v) { mww(R.ds, (uint16_t)(R.bx + f), v); }
static uint8_t theme(void) { return db(0x28bf); }

static void f_cmp16(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
}
static void f_cmp8(uint8_t a, uint8_t b)
{
    R.cf = a < b;
    flags_zs8((uint8_t)(a - b));
}
static uint16_t f_add16(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a + b);
    R.cf = r < a;
    flags_zs16(r);
    return r;
}
static uint16_t f_sub16(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a - b);
    R.cf = a < b;
    flags_zs16(r);
    return r;
}
static uint16_t f_logic16(uint16_t r)
{
    R.cf = false;
    flags_zs16(r);
    return r;
}
static uint16_t f_neg16(uint16_t v)
{
    uint16_t r = (uint16_t)-v;
    R.cf = v != 0;
    flags_zs16(r);
    return r;
}
static uint16_t f_sar16(uint16_t v, int n)
{
    int16_t s = (int16_t)v;
    R.cf = (s >> (n - 1)) & 1;
    uint16_t r = (uint16_t)(s >> n);
    flags_zs16(r);
    return r;
}
/* cmp r, -1 / jg / neg r: the absolute value (of a signed word) */
static uint16_t f_abs16(uint16_t v)
{
    f_cmp16(v, 0xffff);
    return (int16_t)v <= -1 ? f_neg16(v) : v;
}
/* A world coordinate wrapped into 0..0bffh once each way (cmp -1 / jg / add; cmp 0c00h / jl / sub). */
static uint16_t f_wrap(uint16_t v)
{
    f_cmp16(v, 0xffff);
    if ((int16_t)v <= -1) v = f_add16(v, 0xc00);
    f_cmp16(v, 0xc00);
    if ((int16_t)v >= 0xc00) v = f_sub16(v, 0xc00);
    return v;
}

/* lodsb from the sine table at SI, cbw, imul CX, then the middle bytes of DX:AX doubled: AX =
   2 * sin * CX / 256 (al = ah, ah = dl, shl ax, 1). SI moves past the byte. */
static void sine_times_cx(void)
{
    SET_AL(db(R.si));
    R.si++;
    R.ax = (uint16_t)(int16_t)(int8_t)AL;
    int32_t p = (int16_t)R.ax * (int16_t)R.cx;
    R.dx = (uint16_t)((uint32_t)p >> 16);
    R.ax = (uint16_t)p;
    SET_AL(AH);
    SET_AH(DL);
    R.cf = (R.ax >> 15) & 1;
    R.ax = (uint16_t)(R.ax << 1);
    flags_zs16(R.ax);
}

/* sub si, 41h / cmp si, 109fh / jg / add si, 100h: from just past the sine of an angle to its cosine
   (angle - 40h), wrapped within the 256-byte table at 193c:10a0. */
static void si_to_cosine(void)
{
    R.si = f_sub16(R.si, 0x41);
    f_cmp16(R.si, 0x109f);
    if ((int16_t)R.si <= 0x109f) R.si = f_add16(R.si, 0x100);
}

/* The slide to the centre of the 16-pixel cell (as in Terrain_Hole): target [125e]/[126a], a quarter
   of the way per step (the shorter way round the world) in [12be]/[12c0], four steps. */
static void slide_axis(uint16_t target, uint16_t now, uint16_t step)
{
    vww(target, f_logic16(vw(target) & 0xfff0));
    vww(target, f_add16(vw(target), 8));
    R.ax = f_sub16(vw(target), vw(now));
    f_cmp16(R.ax, 0xbe0);
    if ((int16_t)R.ax >= 0xbe0) R.ax = f_sub16(R.ax, 0xc00);
    f_cmp16(R.ax, 0xf420);
    if ((int16_t)R.ax <= (int16_t)0xf420) R.ax = f_add16(R.ax, 0xc00);
    R.ax = f_sar16(R.ax, 2);
    vww(step, R.ax);
}

/* rep stosw of ffffh over the two rings (the trail at [bx+12fd], 30h words; the ripples at
   [bx+135d], 0fh words) with ES = 193c, as the original: AX = ffffh, CX = 0, DI past the second. */
static void clear_rings(void)
{
    R.ax = SEG_DATA;
    uint16_t es = R.ax;
    R.ax = 0xffff;
    R.di = (uint16_t)(R.bx + 0x12fd);
    for (R.cx = 0x30; R.cx; R.cx--, R.di = (uint16_t)(R.di + 2)) mww(es, R.di, R.ax);
    R.di = (uint16_t)(R.bx + 0x135d);
    for (R.cx = 0x0f; R.cx; R.cx--, R.di = (uint16_t)(R.di + 2)) mww(es, R.di, R.ax);
}

/* The warp list 193c:22e1: entries of seven words (tile x, tile y, the lowest lap-list cursor [12e7]
   that may use it, exit x, exit y, exit heading, group), ended by a tile y of ffffh (read while
   searching only after a tile x that does not match). Searches the entry within one tile of the
   vehicle's tile on both axes; on the first track of a theme only the first entry is looked at (as the
   original). True with SI past the entry's first two words. */
static bool warp_find(void)
{
    SET_CL(0x60);
    R.ax = vw(0x125c);
    {
        uint16_t n = R.ax;
        SET_AL((uint8_t)(n / CL));
        SET_AH((uint8_t)(n % CL));
    }
    R.ax = f_logic16(AL);
    R.dx = R.ax;
    R.ax = vw(0x1268);
    {
        uint16_t n = R.ax;
        SET_AL((uint8_t)(n / CL));
        SET_AH((uint8_t)(n % CL));
    }
    R.ax = f_logic16(AL);
    R.cx = R.ax;
    R.si = 0x22e1;
    for (;;) {
        R.ax = dw(R.si);
        R.si = (uint16_t)(R.si + 2);
        R.ax = f_abs16(f_sub16(R.ax, R.dx));
        f_cmp16(R.ax, 1);
        if ((int16_t)R.ax <= 1) {
            R.ax = dw(R.si);
            R.si = (uint16_t)(R.si + 2);
            R.ax = f_abs16(f_sub16(R.ax, R.cx));
            f_cmp16(R.ax, 1);
            if ((int16_t)R.ax <= 1) return true;
        } else {
            R.ax = dw(R.si);
            R.si = (uint16_t)(R.si + 2);
        }
        f_cmp8(db(0x28c0), 1);
        if (db(0x28c0) == 1) return false;
        R.si = f_add16(R.si, 0x0a);
        f_cmp16(R.ax, 0xffff);
        if (R.ax == 0xffff) return false;
    }
}

/* DS = ES = 193c inside (DS, ES, SI, DI saved and restored). Sub-state [1382]:
   0  entering: the hole for the vehicle's tile (warp_find) if the lap cursor has reached its
      minimum; state 4, sub-state 1, the vehicle joins its group's queue (193c:2684 + group: count,
      2686 head, 2688 tail, 268a a timer, 268c a ring of four vehicles; head and tail step by 2 & 6)
      and slides into the hole's centre. No such hole: the vehicle falls in (below).
   1  in the hole: [2682] notes the first vehicle in (if none), [2680] = ffffh; the hole searched
      again, the exit position [1260]/[126c] and heading [1278] taken, the velocity the maximum speed
      along that heading, the motion state cleared, the rings cleared, the vehicle hidden ([124c] = 0),
      sub-state 2, state 0eh. Not found: the queue entry is given back and the vehicle falls in.
   2  head to head: if both cars are not in the group, the lone traveller waits 28h steps and comes
      out at once where it is (state 0, [2911] = 1); if both are in, waits while the other player is
      still entering (state 4). Otherwise on to 3.
   3  travelling: moves 8 * [263a] pixels a step toward the exit on each axis (x first; y only once x
      arrived... each axis snaps when within the step); at the exit, if the vehicle is at the head of
      its group's queue and the exit is free ([2680]: none, or in head to head not airborne, else not
      driving), it takes the exit: [2680] = it, sub-state 4, visible, a hop (height 1, vertical speed 8).
   4  coming out: drawn (Car_Draw, ES = the back buffer); within 32h pixels of the exit on x it leaves
      the queue, state 0, sub-state 0, the group's timer cleared.
   Falling in (a vehicle that can fall, [12f9]): state 5 (the hole), the slide to the cell's centre,
   the skid and grip timers cleared, sub-state 0. */
/* Car_Warp 1000:6ae5 */
void Car_Warp(void)
{
    uint16_t ds0 = R.ds, es0 = R.es, si0 = R.si, di0 = R.di;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.es = R.ax;
    uint16_t sub = vw(0x1382);
    f_cmp16(sub, 1);
    if (sub == 1) goto in_hole;
    f_cmp16(sub, 2);
    if (sub == 2) goto waiting;
    f_cmp16(sub, 3);
    if (sub == 3) goto travel;
    f_cmp16(sub, 4);
    if (sub == 4) goto coming_out;

    /* 0: entering */
    if (!warp_find()) goto fall_in;
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    f_cmp16(vw(0x12e7), R.ax);
    if (vs(0x12e7) < (int16_t)R.ax) goto fall_in;
    vww(0x12ae, 4);
    vww(0x1382, 1);
    R.si = f_add16(R.si, 6);
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    vww(0x138c, R.ax);
    R.di = R.ax;
    R.si = dw((uint16_t)(R.di + 0x2688));
    dww((uint16_t)(R.di + 0x2688), (uint16_t)(dw((uint16_t)(R.di + 0x2688)) + 2));
    dww((uint16_t)(R.di + 0x2684), (uint16_t)(dw((uint16_t)(R.di + 0x2684)) + 1));
    dww((uint16_t)(R.di + 0x2688), f_logic16(dw((uint16_t)(R.di + 0x2688)) & 6));
    R.si = f_add16(R.si, 0x268c);
    R.si = f_add16(R.si, R.di);
    dww(R.si, R.bx);
    slide_axis(0x125e, 0x125c, 0x12be);
    slide_axis(0x126a, 0x1268, 0x12c0);
    vww(0x12c2, 4);
    goto done;

in_hole:
    f_cmp16(dw(0x2682), 0xffff);
    if (dw(0x2682) == 0xffff) dww(0x2682, R.bx);
    dww(0x2680, 0xffff);
    if (!warp_find()) goto give_back;
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    f_cmp16(vw(0x12e7), R.ax);
    if (vs(0x12e7) < (int16_t)R.ax) goto give_back;
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    vww(0x1260, R.ax);
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    vww(0x126c, R.ax);
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    vww(0x1278, R.ax);
    R.ax = vw(0x129c);
    vww(0x127a, R.ax);
    R.cx = vw(0x127a);
    R.si = f_logic16(vw(0x1278) & 0xf8);
    R.si = f_add16(R.si, 0x10a0);
    sine_times_cx();
    vww(0x1272, R.ax);
    R.si = f_logic16(vw(0x1278) & 0xf8);
    R.si = f_sub16(R.si, 0x40);
    f_cmp16(R.si, 0xffff);
    if ((int16_t)R.si <= -1) R.si = f_add16(R.si, 0x100);
    R.si = f_add16(R.si, 0x10a0);
    sine_times_cx();
    vww(0x1276, R.ax);
    {
        static const uint16_t zero[] = {0x125a, 0x1258, 0x1266, 0x1264, 0x12d0, 0x12d2, 0x12da, 0x12dc, 0x12b6,
                                        0x12b8, 0x12b0, 0x1282, 0x1284, 0x1286, 0x128c, 0x1288, 0x128a};
        for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++) vww(zero[i], 0);
    }
    vww(0x128e, 0xffe2);
    vww(0x1290, 0x14);
    vww(0x1292, 0x1e);
    vww(0x1294, 0xffec);
    vww(0x1296, 0);
    vww(0x1298, 0);
    vww(0x12b2, 0);
    vww(0x12b4, 0);
    clear_rings();
    vww(0x1382, 2);
    vww(0x124c, 0);
    vww(0x12ae, 0x0e);
    goto done;

waiting:
    f_cmp16(dw(0x2656), 2);
    if (dw(0x2656) != 2) goto travel;
    R.di = vw(0x138c);
    f_cmp16(dw((uint16_t)(R.di + 0x2684)), 2);
    if (dw((uint16_t)(R.di + 0x2684)) != 2) {
        uint16_t t = (uint16_t)(dw((uint16_t)(R.di + 0x268a)) + 1);
        dww((uint16_t)(R.di + 0x268a), t);
        flags_zs16(t);
        f_cmp16(t, 0x28);
        if ((int16_t)t < 0x28) goto done;
        dww(0x2911, 1);
        vww(0x12ae, 0);
        vww(0x124c, 1);
        vww(0x1382, 0);
        goto done;
    }
    f_cmp16(R.bx, dw(0x2660));
    R.si = R.bx == dw(0x2660) ? dw(0x2662) : dw(0x2660);
    f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 4);
    if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) == 4) goto done;

travel:
    vww(0x1382, 3);
    R.ax = 8;
    R.cx = dw(0x263a);
    {
        uint32_t p = (uint32_t)R.ax * R.cx;
        R.ax = (uint16_t)p;
        R.dx = (uint16_t)(p >> 16);
        R.cf = R.dx != 0;
    }
    R.cx = R.ax;
    R.ax = f_abs16(f_sub16(vw(0x1260), vw(0x125c)));
    f_cmp16(R.ax, R.cx);
    if ((int16_t)R.ax >= (int16_t)R.cx) {
        R.ax = vw(0x1260);
        f_cmp16(vw(0x125c), R.ax);
        if (vs(0x125c) >= (int16_t)R.ax) vww(0x125c, f_sub16(vw(0x125c), R.cx));
        else vww(0x125c, f_add16(vw(0x125c), R.cx));
    } else {
        R.ax = vw(0x1260);
        vww(0x125c, R.ax);
    }
    R.ax = f_abs16(f_sub16(vw(0x126c), vw(0x1268)));
    f_cmp16(R.ax, R.cx);
    if ((int16_t)R.ax >= (int16_t)R.cx) {
        R.ax = vw(0x126c);
        f_cmp16(vw(0x1268), R.ax);
        if (vs(0x1268) >= (int16_t)R.ax) vww(0x1268, f_sub16(vw(0x1268), R.cx));
        else vww(0x1268, f_add16(vw(0x1268), R.cx));
        goto done;
    }
    R.ax = vw(0x126c);
    vww(0x1268, R.ax);
    R.ax = f_abs16(f_sub16(vw(0x1260), vw(0x125c)));
    f_cmp16(R.ax, R.cx);
    if ((int16_t)R.ax >= (int16_t)R.cx) goto done;
    dww(0x2682, 0xffff);
    R.di = vw(0x138c);
    R.si = dw((uint16_t)(R.di + 0x2686));
    R.si = f_add16(R.si, 0x268c);
    R.si = f_add16(R.si, R.di);
    R.ax = dw(R.si);
    f_cmp16(R.ax, R.bx);
    if (R.ax != R.bx) goto done;
    f_cmp16(dw(0x2656), 2);
    if (dw(0x2656) == 2) {
        f_cmp16(dw(0x2680), 0xffff);
        if (dw(0x2680) != 0xffff) {
            R.si = dw(0x2680);
            f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12d6)), 0);
            if (mw(R.ds, (uint16_t)(R.si + 0x12d6)) != 0) goto done;
            f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12d4)), 0);
            if (mw(R.ds, (uint16_t)(R.si + 0x12d4)) != 0) goto done;
        }
    } else {
        f_cmp16(dw(0x2680), 0xffff);
        if (dw(0x2680) != 0xffff) {
            R.si = dw(0x2680);
            f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 0);
            if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) != 0) goto done;
        }
    }
    dww(0x2680, R.bx);
    vww(0x1382, 4);
    vww(0x124c, 1);
    vww(0x12d6, 1);
    vww(0x12d4, 8);

coming_out:
    R.ax = c16(0x6efb);   /* mov ax, the back buffer's segment (the relocated immediate) */
    R.es = R.ax;
    Car_Draw();
    R.ax = SEG_DATA;
    R.es = R.ax;
    R.ax = f_abs16(f_sub16(vw(0x1260), vw(0x125c)));
    f_cmp16(R.ax, 0x32);
    if ((int16_t)R.ax > 0x32) goto done;
    R.di = vw(0x138c);
    dww((uint16_t)(R.di + 0x2686), (uint16_t)(dw((uint16_t)(R.di + 0x2686)) + 2));
    dww((uint16_t)(R.di + 0x2684), (uint16_t)(dw((uint16_t)(R.di + 0x2684)) - 1));
    dww((uint16_t)(R.di + 0x2686), f_logic16(dw((uint16_t)(R.di + 0x2686)) & 6));
    vww(0x1382, 0);
    vww(0x12ae, 0);
    dww((uint16_t)(R.di + 0x268a), 0);
    goto done;

give_back:
    R.di = vw(0x138c);
    dww((uint16_t)(R.di + 0x2684), (uint16_t)(dw((uint16_t)(R.di + 0x2684)) - 1));
    dww((uint16_t)(R.di + 0x2688), (uint16_t)(dw((uint16_t)(R.di + 0x2688)) - 2));
    flags_zs16(dw((uint16_t)(R.di + 0x2688)));

fall_in:
    f_cmp16(vw(0x12f9), 0);
    if (vw(0x12f9) == 0) goto done;
    vww(0x12ae, 5);
    vww(0x12b0, 0);
    slide_axis(0x125e, 0x125c, 0x12be);
    slide_axis(0x126a, 0x1268, 0x12c0);
    vww(0x12c2, 4);
    vww(0x1282, 0);
    vww(0x1284, 0);
    vww(0x1286, 0);
    vww(0x1288, 0);
    vww(0x1382, 0);

done:
    R.di = di0;
    R.si = si0;
    R.es = es0;
    R.ds = ds0;
}

/* Vehicle state 7 (after a fall, a hole, a short cut) and theme 9's class 15. On themes 2 and 8 the
   timer [1382] runs down first (a step per call). Then the vehicle is put back at its last safe point
   [12f1]/[12f3] (track 16h has its tile-4 safe point moved a tile up first):
   - head to head: the warp queue of its group cleared;
   - speeds and the motion state cleared, the maximum speed back to its default ([129e]);
   - the position: the centre of the safe point's 96-pixel tile, then the offset of the tile's
     respawn byte (193c:[28b9] + tile; bits 2-4 index the x/y word pairs at 193c:1fcb), wrapped;
   - the tile there (Track_TileAt): checkpoint [12e3] = [12e1], tile [12cc], the lap-list cursor
     [12e7] = the first entry whose range starts past the checkpoint, orientation [12de], cell [12ce];
     tile 1ah resets the upper layer ([1388] = 0);
   - the heading from the respawn byte's bits 5-6 (40h, 80h, 60h, a0h), turned half round per
     orientation flag 2 and 1; the vehicle moved 24 pixels to the side of it (the first player to the
     right, the others to the left: 2 * 0ch * sine / 256 each axis), wrapped;
   - state 2 (the splash at the new place [12ba]/[12bc]), [1380] = 1, the animation, height, timers
     reset, the rings cleared, sound 9 for a player, the camera's catch-up [264e] = [2650] = 8, the
     surface read twice (Track_SurfaceAt, now and before); on themes 4 and 5 the level from the class
     (class - 4; 13 -> -1; 14 -> 4);
   - head to head: both cars' laps to go set to the smaller of the two. */
/* Car_Respawn 1000:6feb */
void Car_Respawn(void)
{
    uint16_t es0 = R.es;
    f_cmp8(theme(), 2);
    bool countdown = theme() == 2;
    if (!countdown) {
        f_cmp8(theme(), 8);
        countdown = theme() == 8;
    }
    if (countdown) {
        vww(0x1382, (uint16_t)(vw(0x1382) - 1));
        f_cmp16(vw(0x1382), 0);
        if (vs(0x1382) > 0) {
            R.es = es0;
            return;
        }
    }
    f_cmp8(db(0x28c1), 0x16);
    if (db(0x28c1) == 0x16) {
        f_cmp16(vw(0x12cc), 4);
        if (vw(0x12cc) == 4) vww(0x12f3, f_sub16(vw(0x12f3), 0x60));
    }
    f_cmp16(dw(0x2656), 2);
    if (dw(0x2656) == 2) {
        R.di = vw(0x138c);
        dww((uint16_t)(R.di + 0x268a), 0);
        dww((uint16_t)(R.di + 0x2686), 0);
        dww((uint16_t)(R.di + 0x2688), 0);
        dww((uint16_t)(R.di + 0x2684), 0);
    }
    R.ax = vw(0x129e);
    vww(0x129c, R.ax);
    {
        static const uint16_t zero[] = {0x127a, 0x1272, 0x1270, 0x125a, 0x1258, 0x1276, 0x1274,
                                        0x1266, 0x1264, 0x12d0, 0x12d2, 0x12da, 0x12dc};
        for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++) vww(zero[i], 0);
    }
    /* the centre of the safe point's tile: (v / 60h) * 60h + 30h (div, mul, add) */
    R.cx = 0x60;
    for (int axis = 0; axis < 2; axis++) {
        uint16_t n = vw(axis == 0 ? 0x12f1 : 0x12f3);
        uint32_t p = (uint32_t)(n / R.cx) * R.cx;
        R.ax = (uint16_t)p;
        R.dx = (uint16_t)(p >> 16);
        R.ax = f_add16(R.ax, 0x30);
        vww(axis == 0 ? 0x125c : 0x1268, R.ax);
    }
    R.ax = vw(0x125c);
    R.dx = vw(0x1268);
    {
        uint16_t es = R.es;
        Track_TileAt();
        R.es = es;
    }
    R.si = f_add16(R.si, dw(0x28b9));
    SET_CL(db(R.si));
    SET_CH(0);
    R.cf = false;
    R.si = R.cx;
    R.si = f_logic16(R.si & 0x1c);
    R.cf = (R.cx >> 4) & 1;
    R.cx = (uint16_t)(R.cx >> 5);
    R.cx = f_logic16(R.cx & 3);
    R.si = f_add16(R.si, 0x1fcb);
    R.ax = f_wrap(f_add16(dw(R.si), vw(0x125c)));
    vww(0x125c, R.ax);
    vww(0x125e, R.ax);
    R.dx = f_wrap(f_add16(dw((uint16_t)(R.si + 2)), vw(0x1268)));
    vww(0x1268, R.dx);
    vww(0x126a, R.dx);
    uint16_t side = R.cx;
    uint16_t es1 = R.es;
    Track_TileAt();
    R.ax = R.es;
    vww(0x12e3, R.ax);
    vww(0x12e1, R.ax);
    vww(0x12cc, R.si);
    {
        uint16_t si = R.si, dx = R.dx, cx = R.cx;
        R.dx = vw(0x12e3);
        /* the lap list, as Terrain_Dispatch finds it */
        R.si = 0x1feb;
        SET_AL((uint8_t)(theme() - 1));
        R.ax = f_logic16(AL);
        R.cf = (R.ax >> 13) & 1;
        R.ax = (uint16_t)(R.ax << 3);
        R.si = f_add16(R.si, R.ax);
        SET_AL((uint8_t)(db(0x28c0) - 1));
        R.cf = (AL >> 7) & 1;
        SET_AL((uint8_t)(AL << 1));
        R.si = f_add16(R.si, R.ax);
        R.si = dw(R.si);
        R.cx = 0;
        for (;;) {
            R.ax = dw(R.si);
            R.si = (uint16_t)(R.si + 2);
            f_cmp8(DL, AL);
            if (DL < AL) break;
            R.cx = f_add16(R.cx, 2);
        }
        vww(0x12e7, R.cx);
        R.cx = cx;
        R.dx = dx;
        R.si = si;
    }
    f_cmp16(R.si, 0x1a);
    if (R.si == 0x1a) vww(0x1388, 0);
    vww(0x12de, R.di);
    vww(0x12ce, R.cx);
    R.es = es1;
    R.cx = side;
    static const uint16_t heading[4] = {0x40, 0x80, 0x60, 0xa0};
    for (uint16_t i = 0; i < 4; i++) {
        f_cmp16(R.cx, i);
        if (R.cx == i) {
            R.ax = heading[i];
            break;
        }
    }
    f_logic16(R.di & 2);
    if (R.di & 2) R.ax = f_logic16(R.ax ^ 0x80);
    f_logic16(R.di & 1);
    if (R.di & 1) R.ax = f_logic16(R.ax ^ 0x80);
    vww(0x1278, R.ax);
    f_cmp16(R.bx, dw(0x2660));
    if (R.bx == dw(0x2660)) {
        R.si = f_logic16(vw(0x1278) & 0xf8);
        R.si = f_add16(R.si, 0x40);
        f_cmp16(R.si, 0x100);
        if ((int16_t)R.si >= 0x100) R.si = f_sub16(R.si, 0x100);
    } else {
        R.si = f_logic16(vw(0x1278) & 0xf8);
        R.si = f_sub16(R.si, 0x40);
        f_cmp16(R.si, 0xffff);
        if ((int16_t)R.si <= -1) R.si = f_add16(R.si, 0x100);
    }
    R.cx = 0x0c;
    R.si = f_add16(R.si, 0x10a0);
    sine_times_cx();
    uint16_t x = f_add16(R.ax, vw(0x125c));
    si_to_cosine();
    sine_times_cx();
    R.dx = R.ax;
    R.dx = f_add16(R.dx, vw(0x1268));
    R.ax = f_wrap(x);
    vww(0x125c, R.ax);
    vww(0x125e, R.ax);
    R.dx = f_wrap(R.dx);
    vww(0x1268, R.dx);
    vww(0x126a, R.dx);

    vww(0x12ae, 2);
    R.ax = vw(0x125c);
    vww(0x12ba, R.ax);
    R.ax = vw(0x1268);
    vww(0x12bc, R.ax);
    vww(0x1380, 1);
    vww(0x137e, 0);
    {
        static const struct { uint16_t f, v; } set[] = {
            {0x12b6, 0}, {0x12b8, 0}, {0x12b0, 0}, {0x12d6, 0}, {0x12d4, 0}, {0x12d8, 1}, {0x1282, 0},
            {0x1284, 0}, {0x1286, 0}, {0x128c, 0}, {0x1288, 0}, {0x128a, 0}, {0x128e, 0xffe2},
            {0x1290, 0x14}, {0x1292, 0x1e}, {0x1294, 0xffec}, {0x1270, 0}, {0x1274, 0}, {0x1272, 0},
            {0x1276, 0}, {0x138a, 1}};
        for (unsigned i = 0; i < sizeof set / sizeof set[0]; i++) vww(set[i].f, set[i].v);
    }
    R.di = vw(0x138c);
    dww((uint16_t)(R.di + 0x268a), 0);
    vww(0x1382, 0);
    dww(0x26bc, 0);
    dww(0x26c2, 0);
    dww(0x26be, 0);
    dww(0x26c0, 0);
    f_cmp16(vw(0x1250), 0);
    if (vw(0x1250) != 0) {
        SET_AH(5);
        SET_AL(9);
        mach_drv();
    }
    vww(0x1296, 0);
    vww(0x1298, 0);
    vww(0x1384, 0);
    {
        uint16_t es = R.es;
        vww(0x12b2, 0);
        vww(0x12b4, 0);
        clear_rings();
        R.es = es;
    }
    dww(0x264e, 8);
    dww(0x2650, 8);
    Track_SurfaceAt();
    Track_SurfaceAt();
    f_cmp8(theme(), 4);
    bool levels = theme() == 4;
    if (!levels) {
        f_cmp8(theme(), 5);
        levels = theme() == 5;
    }
    if (levels) {
        R.ax = vw(0x12da);
        R.cf = (R.ax >> 3) & 1;
        R.ax = (uint16_t)(R.ax >> 4);
        f_cmp16(R.ax, 5);
        if ((int16_t)R.ax >= 5) {
            f_cmp16(R.ax, 0x0d);
            if (R.ax == 0x0d) {
                vww(0x138e, 0xffff);
            } else {
                f_cmp16(R.ax, 0x0e);
                if (R.ax == 0x0e) {
                    vww(0x138e, 4);
                } else {
                    R.ax = f_sub16(R.ax, 4);
                    vww(0x138e, R.ax);
                }
            }
        }
    }
    f_cmp16(dw(0x2656), 2);
    if (dw(0x2656) == 2) {
        uint16_t bx = R.bx;
        R.si = dw(0x2660);
        R.bx = dw(0x2662);
        R.ax = mw(R.ds, (uint16_t)(R.si + 0x12ed));
        R.cx = mw(R.ds, (uint16_t)(R.bx + 0x12ed));
        f_cmp16(R.ax, R.cx);
        uint16_t laps = (int16_t)R.ax < (int16_t)R.cx ? R.ax : R.cx;
        mww(R.ds, (uint16_t)(R.si + 0x12ed), laps);
        mww(R.ds, (uint16_t)(R.bx + 0x12ed), laps);
        R.bx = bx;
    }
    R.es = es0;
}

/* Car_SinkStep 1000:73e7
   Per vehicle not driving, from the race step (theme 9: only vehicle 0): the animation counter [12b0]
   counts; a vehicle sinking (state 1), entering a warp (4) or in a hole (5) slides by [12be]/[12c0]
   while its step count [12c2] lasts (not wrapped at the world's edge, as the original). */
void Car_SinkStep(void)
{
    f_cmp8(theme(), 9);
    if (theme() == 9) {
        f_cmp16(R.bx, 0);
        if (R.bx != 0) return;
    }
    vww(0x12b0, (uint16_t)(vw(0x12b0) + 1));
    flags_zs16(vw(0x12b0));
    uint16_t st = vw(0x12ae);
    f_cmp16(st, 1);
    if (st != 1) {
        f_cmp16(st, 4);
        if (st != 4) {
            f_cmp16(st, 5);
            if (st != 5) return;
        }
    }
    f_cmp16(vw(0x12c2), 0);
    if (vw(0x12c2) == 0) return;
    vww(0x12c2, (uint16_t)(vw(0x12c2) - 1));
    R.ax = vw(0x12be);
    vww(0x125c, f_add16(vw(0x125c), R.ax));
    R.ax = vw(0x12c0);
    vww(0x1268, f_add16(vw(0x1268), R.ax));
}
