/* The rules of the race's end and of head to head (docs/re/race.md, "Rules"): the start's countdown
   blink, the head-to-head point (who scores, the respawn of both players), the banners slid or dropped
   over the race, theme 9's course done and time up.

   Data: [2656] = 2 in head to head; [2660]/[2662] the two players' record offsets; [26b4] the point
   bar (0..8, 4 the middle), [26b6] its value shown while flashing, [26b8] the scorer (1: none),
   [26ba] the pause after a point, [26be]/[26c0] the banner's position, [26c2] its phase, [26c4] the
   winner, [26c6] >= 2 the race over, [2621] the vehicle hidden behind the banner, [2630] the winner
   1/2 for the order, [2682] the first vehicle knocked out, [2911] the point's state, [2913] both
   players fell. Register-exact (docs/re/porting.md). */
#include "cpu.h"
#include "machine.h"
#include "mem.h"
#include "segs.h"
#include "routines.h"

static inline void cmpw(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a - b);
    R.cf = a < b, R.zf = r == 0, R.sf = (r & 0x8000) != 0;
}
static inline void cmpb(uint8_t a, uint8_t b)
{
    uint8_t r = (uint8_t)(a - b);
    R.cf = a < b, R.zf = r == 0, R.sf = (r & 0x80) != 0;
}
static inline uint16_t addw(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a + b);
    R.cf = r < a;
    flags_zs16(r);
    return r;
}
static inline uint16_t subw(uint16_t a, uint16_t b)
{
    uint16_t r = (uint16_t)(a - b);
    R.cf = a < b;
    flags_zs16(r);
    return r;
}
static inline uint16_t andw(uint16_t a, uint16_t b) { a &= b; R.cf = false; flags_zs16(a); return a; }

#define V(off) ((uint16_t)(R.bx + (off)))
#define VS(off) ((uint16_t)(R.si + (off)))

static void drv_ah_al(uint8_t ah, uint8_t al)
{
    SET_AH(ah);
    SET_AL(al);
    mach_drv();
}

/* Race_AwardPoint 1000:7759
   Head to head, when [2911] = 1 (from the race frame). The scorer (BX) and the other (SI): if a vehicle
   was knocked out ([2682]), that one and the other player (as the original); else, unless either
   player is in state 0Ch already (then nothing), both get their position saved in [12ba]/[12bc],
   [2911] = 2, and the one ahead in the race order [12ef] (smaller) scores unless it is not in state
   0, then the other. The other's entries at 193c:2684 + [138c] are cleared; [26b8] = the scorer, made
   active; outside theme 2's class 5 a hop (vertical speed 14h, bounce); [27b5] = its [124a]; [264e] =
   [2650] = 4; the other gets the scorer's checkpoint [12f1]/[12f3]; both stop, the scorer in state 0Bh
   (it spins), the other in 0Ch. The engines silenced, driver channels 0..8 stopped, a tick's wait, and
   effect 0Ah if the scorer is visible. */
void Race_AwardPoint(void)
{
    cmpw(d16(0x2682), 0xffff);
    if (d16(0x2682) != 0xffff) {
        R.bx = d16(0x2682);
        d16w(0x2682, 0xffff);
        R.si = d16(0x2660);
        cmpw(R.bx, R.si);
        if (R.bx == R.si) R.si = d16(0x2662);
    } else {
        R.bx = d16(0x2660);
        cmpw(d16(V(0x12ae)), 0xc);
        if (d16(V(0x12ae)) == 0xc) return;
        R.ax = d16(V(0x125c));
        d16w(V(0x12ba), R.ax);
        R.ax = d16(V(0x1268));
        d16w(V(0x12bc), R.ax);
        R.cx = d16(V(0x12ef));
        R.bx = d16(0x2662);
        cmpw(d16(V(0x12ae)), 0xc);
        if (d16(V(0x12ae)) == 0xc) return;
        R.ax = d16(V(0x125c));
        d16w(V(0x12ba), R.ax);
        R.ax = d16(V(0x1268));
        d16w(V(0x12bc), R.ax);
        d16w(0x2911, 2);
        R.dx = d16(V(0x12ef));
        R.si = d16(0x2660);
        R.bx = d16(0x2662);
        cmpw(R.cx, R.dx);
        if ((int16_t)R.cx <= (int16_t)R.dx) {
            uint16_t t = R.bx;
            R.bx = R.si;
            R.si = t;
        }
        cmpw(d16(V(0x12ae)), 0);
        if (d16(V(0x12ae)) != 0) {
            uint16_t t = R.bx;
            R.bx = R.si;
            R.si = t;
        }
    }
    R.di = d16(V(0x138c));
    d16w((uint16_t)(R.di + 0x268a), 0);
    d16w((uint16_t)(R.di + 0x2684), 0);
    d16w((uint16_t)(R.di + 0x2686), 0);
    d16w((uint16_t)(R.di + 0x2688), 0);
    d16w(0x26b8, R.bx);
    d16w(V(0x124c), 1);
    d16w(0x2682, 0xffff);
    cmpb(d8(0x28bf), 2);
    bool hop = d8(0x28bf) != 2;
    if (!hop) {
        cmpw(d16(V(0x12d0)), 5);
        hop = d16(V(0x12d0)) != 5;
    }
    if (hop) {
        d16w(V(0x12d4), 0x14);
        d16w(V(0x12d6), 0);
        d16w(V(0x12d8), 1);
    }
    R.ax = d16(V(0x124a));
    d16w(0x27b5, R.ax);
    d16w(0x264e, 4);
    d16w(0x2650, 4);
    R.cx = d16(V(0x12f1));
    R.dx = d16(V(0x12f3));
    d16w(V(0x127a), 0);
    d16w(V(0x1272), 0);
    d16w(V(0x1276), 0);
    d16w(V(0x1274), 0);
    d16w(V(0x1270), 0);
    d16w(V(0x12ae), 0xb);
    d16w(VS(0x12f1), R.cx);
    d16w(VS(0x12f3), R.dx);
    d16w(VS(0x127a), 0);
    d16w(VS(0x1272), 0);
    d16w(VS(0x1276), 0);
    d16w(VS(0x1274), 0);
    d16w(VS(0x1270), 0);
    d16w(VS(0x12ae), 0xc);
    Snd_SilenceEngines();
    for (uint8_t ch = 0; ch <= 8; ch++) drv_ah_al(8, ch);
    Sys_WaitTick();
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 0xa);
}

/* Race_RespawnPlayers 1000:78f8
   When both players fell at once ([2913], from the terrain) and after a head-to-head point. If a
   player is in state 1 (fell) or 5, the other is switched off ([124c] = 0) unless it is in the same
   state, and nothing else happens (player 1 tested first, state 1 before 5). Otherwise the player further back in the race
   order [12ef] gets the other's checkpoint [12f1]/[12f3] and layer [1388], both are made active in
   state 7 (respawn), and [2913] = [2911] = 0. */
void Race_RespawnPlayers(void)
{
    R.si = d16(0x2660);
    cmpw(d16(VS(0x12ae)), 1);
    if (d16(VS(0x12ae)) == 1) {
        R.si = d16(0x2662);
        cmpw(d16(VS(0x12ae)), 1);
        if (d16(VS(0x12ae)) != 1) d16w(VS(0x124c), 0);
        return;
    }
    cmpw(d16(VS(0x12ae)), 5);
    if (d16(VS(0x12ae)) == 5) {
        R.si = d16(0x2662);
        cmpw(d16(VS(0x12ae)), 5);
        if (d16(VS(0x12ae)) != 5) d16w(VS(0x124c), 0);
        return;
    }
    R.si = d16(0x2662);
    cmpw(d16(VS(0x12ae)), 1);
    if (d16(VS(0x12ae)) == 1) {
        R.si = d16(0x2660);
        cmpw(d16(VS(0x12ae)), 1);
        if (d16(VS(0x12ae)) != 1) d16w(VS(0x124c), 0);
        return;
    }
    cmpw(d16(VS(0x12ae)), 5);
    if (d16(VS(0x12ae)) == 5) {
        R.si = d16(0x2660);
        cmpw(d16(VS(0x12ae)), 5);
        if (d16(VS(0x12ae)) != 5) d16w(VS(0x124c), 0);
        return;
    }
    R.si = d16(0x2660);
    R.ax = d16(VS(0x12ef));
    R.bx = d16(0x2662);
    R.bx = d16(V(0x12ef));
    cmpw(R.ax, R.bx);
    uint16_t from, to;
    if ((int16_t)R.ax > (int16_t)R.bx) from = d16(0x2662), to = d16(0x2660);
    else from = d16(0x2660), to = d16(0x2662);
    R.bx = from;
    R.dx = d16(V(0x12f1));
    R.cx = d16(V(0x12f3));
    R.ax = d16(V(0x1388));
    R.si = to;
    d16w(VS(0x12f1), R.dx);
    d16w(VS(0x12f3), R.cx);
    d16w(VS(0x1388), R.ax);
    R.si = d16(0x2660);
    d16w(VS(0x124c), 1);
    d16w(VS(0x12ae), 7);
    R.si = d16(0x2662);
    d16w(VS(0x124c), 1);
    d16w(VS(0x12ae), 7);
    d16w(0x2913, 0);
    d16w(0x2911, 0);
}

/* Race_StateStart 1000:849b
   State 0Ah, the start's countdown (set by the track set-up). [26ca] = 1. For the player (in head to
   head both players) the count [26d5] grows by the steps per frame [263a] (player 1's only); below 60h
   the vehicle blinks (not drawn while the blink flag [26cf] is 1) and player 1's beep is effect 9; at
   60h all four leave state 0Ah, [264e] = [2650] = 32h, [26ca] = 0. The computer's vehicles stay in
   state 0Ah, drawn, while vehicle 0 is. */
void Race_StateStart(void)
{
    d16w(0x26ca, 1);
    cmpw(d16(0x2656), 2);
    if (d16(0x2656) != 2) {
        cmpw(R.bx, d16(0x2660));
        if (R.bx != d16(0x2660)) {
            cmpw(d16(0x12ae), 0xa);
            if (d16(0x12ae) == 0xa) goto draw;
            d16w(V(0x12ae), 0);
            return;
        }
    }
    R.cx = d16(0x263a);
    cmpw(R.bx, d16(0x2660));
    if (R.bx == d16(0x2660)) d16w(0x26d5, addw(d16(0x26d5), R.cx));
    cmpw(d16(0x26d5), 0x60);
    if ((int16_t)d16(0x26d5) >= 0x60) {
        d16w(0x12ae, 0);
        d16w(0x1412, 0);
        d16w(0x1576, 0);
        d16w(0x16da, 0);
        d16w(0x264e, 0x32);
        d16w(0x2650, 0x32);
        d16w(0x26ca, 0);
        goto draw;
    }
    cmpb(d8(0x26cf), 1);
    if (d8(0x26cf) == 1) return;
draw:
    Car_Draw();
    cmpw(R.bx, d16(0x2660));
    if (R.bx != d16(0x2660)) return;
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 9);
}

/* Race_StateScored 1000:851f
   Head to head, state 0Bh (the scorer) and from the race frame: the engines silenced; the scorer
   [26b8] spins by 8 a frame (not in theme 9) and, while the bar [26b4] is 2..6 and it has laps to go,
   the banner 193c:7423 is shown at the screen's centre (80h, 7Ch). */
void Race_StateScored(void)
{
    Snd_SilenceEngines();
    R.bx = d16(0x26b8);
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) return;
    d16w(V(0x1278), addw(d16(V(0x1278)), 8));
    d16w(V(0x1278), andw(d16(V(0x1278)), 0xff));
    cmpw(d16(0x26b4), 7);
    if ((int16_t)d16(0x26b4) >= 7) return;
    cmpw(d16(0x26b4), 1);
    if ((int16_t)d16(0x26b4) <= 1) return;
    cmpw(d16(V(0x12ed)), 0);
    if ((int16_t)d16(V(0x12ed)) <= 0) return;
    R.di = 0x80;
    R.ax = 0x7c;
    R.si = 0x7423;
    Gfx_DrawBanner();
}

/* Race_StateLostPoint 1000:855a
   Head to head, state 0Ch, from the race frame and the race's end: the engines silenced; for the
   scorer only (BX = [26b8]; not in theme 9): theme 8 spins it; when the bar and its flashing value add
   to 0Fh or 1 (the bar at an end) or the banner phase [26c2] is 2 or C8h, the winner's banner
   193c:7c63 slides down from the top (y from -18h, 8 a frame, to 7Ch) with effect 10h if the driver
   allows, the other player hidden ([2621]) and the winner 1/2 in [2630]; otherwise the banner
   193c:7423. AX DI SI kept. */
void Race_StateLostPoint(void)
{
    uint16_t ax = R.ax, di = R.di, si = R.si;
    Snd_SilenceEngines();
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) goto out;
    cmpw(R.bx, d16(0x26b8));
    if (R.bx != d16(0x26b8)) goto out;
    cmpb(d8(0x28bf), 8);
    if (d8(0x28bf) == 8) {
        d16w(V(0x1278), addw(d16(V(0x1278)), 8));
        d16w(V(0x1278), andw(d16(V(0x1278)), 0xff));
    }
    R.ax = d16(0x26b4);
    R.ax = addw(R.ax, d16(0x26b6));
    cmpw(R.ax, 0xf);
    if (R.ax == 0xf) goto win;
    R.ax = d16(0x26b4);
    R.ax = addw(R.ax, d16(0x26b6));
    cmpw(R.ax, 1);
    if (R.ax == 1) goto win;
    cmpw(d16(0x26c2), 0xc8);
    if (d16(0x26c2) == 0xc8) goto start;
    cmpw(d16(0x26c2), 2);
    if (d16(0x26c2) == 2) goto win;
    R.di = 0x80;
    R.ax = 0x7c;
    R.si = 0x7423;
    Gfx_DrawBanner();
    goto out;
win:
    cmpw(d16(0x26c2), 0);
    if (d16(0x26c2) != 0) goto slide;
start:
    d16w(0x26be, 0x80);
    d16w(0x26c0, 0xffe8);
    d16w(0x26c2, 2);
slide:
    d16w(0x26be, 0x80);
    d16w(0x26c0, addw(d16(0x26c0), 8));
    cmpw(d16(0x26c0), 0x7c);
    if ((int16_t)d16(0x26c0) >= 0x7c) d16w(0x26c0, 0x7c);
    {
        uint16_t bx = R.bx;
        drv_ah_al(0xa, 0x10);
        R.cf = false;
        flags_zs8(AL);
        if (AL != 0) drv_ah_al(5, 0x10);
        R.bx = bx;
    }
    Snd_SilenceEngines();
    R.bx = d16(0x26b8);
    R.ax = 1;
    R.si = d16(0x2662);
    cmpw(R.bx, d16(0x2660));
    if (R.bx != d16(0x2660)) {
        R.si = d16(0x2660);
        R.ax++;
        flags_zs16(R.ax);
    }
    d16w(0x2621, R.si);
    d16w(0x2630, R.ax);
    R.di = d16(0x26be);
    R.ax = d16(0x26c0);
    R.si = 0x7c63;
    Gfx_DrawBanner();
out:
    R.si = si, R.di = di, R.ax = ax;
}

/* Race_DrawBannerOut 1000:8634
   After a head-to-head point, while [26c2] < 3: the banner 193c:7423 slides left 8 a frame from
   [26be]; with [26c2] from 3 to 63h the banner 193c:84a3 at [26be]/[26c0] instead. The reset that
   follows (1000:864f) is never reached. */
void Race_DrawBannerOut(void)
{
    cmpw(d16(0x26c2), 3);
    if ((int16_t)d16(0x26c2) < 3) {
        d16w(0x26be, subw(d16(0x26be), 8));
        R.di = d16(0x26be);
        R.ax = d16(0x26c0);
        R.si = 0x7423;
        Gfx_DrawBanner();
        return;
    }
    cmpw(d16(0x26c2), 0x64);
    if ((int16_t)d16(0x26c2) >= 0x64) return;
    R.di = d16(0x26be);
    R.ax = d16(0x26c0);
    R.si = 0x84a3;
    Gfx_DrawBanner();
}

/* Dead_BannerReset 1000:864f
   Unreachable, after 1000:8634's unconditional jump: when the banner had left the screen (x = -58h) it
   would reset its position and [26c2]; its last compare ([2623]) has no branch. */
void Dead_BannerReset(void)
{
    cmpw(d16(0x26be), 0xffa8);
    if (d16(0x26be) != 0xffa8) return;
    d16w(0x26be, 0);
    d16w(0x26c0, 0);
    d16w(0x26c2, 0);
    cmpb(d8(0x2623), 0);
}

/* Race_StateCourseDone 1000:8683
   Theme 9, state 0Fh (the course finished in time): effect 10h if the driver allows, the vehicle and
   the banner 193c:8ce3 dropped from the top (1000:86d2), [291d] = 1. */
void Race_StateCourseDone(void)
{
    uint16_t bx = R.bx;
    drv_ah_al(0xa, 0x10);
    R.cf = false;
    flags_zs8(AL);
    if (AL != 0) drv_ah_al(5, 0x10);
    R.bx = bx;
    R.si = 0x8ce3;
    Race_DropBanner();
    d16w(0x291d, 1);
}

/* Race_StateTimeUp 1000:86a8
   Theme 9, state 10h (the time ran out, 1000:7429): effect 0Fh if the driver allows, the banner
   193c:9523 dropped from the top, [291d] = 0, the engines silenced. */
void Race_StateTimeUp(void)
{
    uint16_t bx = R.bx;
    drv_ah_al(0xa, 0xf);
    R.cf = false;
    flags_zs8(AL);
    if (AL != 0) drv_ah_al(5, 0xf);
    R.bx = bx;
    R.ax = 0;
    R.si = 0x9523;
    Race_DropBanner();
    d16w(0x291d, 0);
    Snd_SilenceEngines();
}

/* Race_DropBanner 1000:86d2
   SI = banner: the vehicle drawn, then the banner at x = 80h, y [26c0] from 0 ([26c2] = 3E8h marks it
   running): while y is at most 7Ch the race is over ([26c6] = 2) and y moves down 8, so the race ends
   at the banner's first frame and the countdown in the race loop runs while it falls. */
void Race_DropBanner(void)
{
    uint16_t si = R.si;
    Car_Draw();
    R.si = si;
    cmpw(d16(0x26c2), 0);
    if (d16(0x26c2) == 0) {
        d16w(0x26be, 0x80);
        d16w(0x26c0, 0);
        d16w(0x26c2, 0x3e8);
    }
    d16w(0x26be, 0x80);
    R.di = d16(0x26be);
    R.ax = d16(0x26c0);
    cmpw(R.ax, 0x7c);
    if (R.ax <= 0x7c) {
        d16w(0x26c6, 2);
        d16w(0x26c0, addw(d16(0x26c0), 8));
    }
    Gfx_DrawBanner();
}
