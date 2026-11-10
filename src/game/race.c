/* The race loop (docs/re/race.md): the set-up, then game steps until Esc or the race's end, SMOOTHNESS
   ([263a]) steps per presented frame with the drawing on the last; the pause and its cheats; the
   per-vehicle step of the height and of the head-to-head points; the engine notes.

   Register-exact (docs/re/porting.md): R is the CPU's registers, the flags follow the original's last
   flag-setting instruction where a routine returns. Vehicle records are 164h bytes from 193c:124c,
   BX the record's offset (0, 164h, 2c8h, 42ch). */
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
static inline uint16_t negw(uint16_t a) { R.cf = a != 0; a = (uint16_t)-a; flags_zs16(a); return a; }
static inline uint16_t decw(uint16_t a) { a = (uint16_t)(a - 1); flags_zs16(a); return a; }   /* CF kept */
static inline uint16_t incw(uint16_t a) { a = (uint16_t)(a + 1); flags_zs16(a); return a; }   /* CF kept */

#define V(off) ((uint16_t)(R.bx + (off)))
#define VS(off) ((uint16_t)(R.si + (off)))

static void drv_ah_al(uint8_t ah, uint8_t al)
{
    SET_AH(ah);
    SET_AL(al);
    mach_drv();
}

/* Race_ClearRegs 1000:3030 */
void Race_ClearRegs(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    Sys_ClearRegs();
}

/* Race_Main 1000:3039
   The race. The set-up (1000:37fc; carry: no race), the engine notes, then the loop of game steps:
   Esc ([1096]) leaves; the input (1000:2d5b); Space (key mask [107c] bit 1) pauses; the vehicles'
   step (1000:4aee); once the race is over ([26c6] >= 2) it ends at once in head to head, else when
   the countdown [26cc] runs out; the drawing (1000:90c5, on the last step of a frame only); per
   vehicle the height and points (1000:7429), the state's step (1000:73e7) unless state 0, the hop
   (1000:51b2). After [263a] steps it waits until the frame tick count cs:4ade passes the byte
   [263c + smoothness], presents and restarts the count. The waits stay as loops with mach_frame() as
   their body: the timer handler moves cs:4ade at each frame boundary.

   At the end: the race-end effect if the vehicle in BX (as Car_StepAll left it) is visible, the engines
   silenced, 100 frames of the head-to-head banner (1000:855a) each after a tick, the driver stopped.
   Both ways out: the finishing order [2678].. fixed when the race was won by a cheat ([2635] = 1) and
   in head to head by the winner [2630], the fade out, and back to the parent directory ([261c]). */
void Race_Main(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.es = R.ax;
    Race_ClearRegs();
    Race_Setup();
    if (R.cf) goto out;
    Snd_InitEngines();
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.es = R.ax;
    d16w(0x2621, 0xffff);
    c8w(0x4ade, 0);
    R.ax = d16(0x263a);
    d16w(0x2638, R.ax);
    for (;;) {
        cmpb(d8(0x1096), 1);
        if (d8(0x1096) == 1) goto out;
        Input_Read();
        R.cf = false;
        flags_zs8(d8(0x107c) & 2);
        if (d8(0x107c) & 2) Race_Pause();
        Car_StepAll();
        cmpw(d16(0x26c6), 2);
        if ((int16_t)d16(0x26c6) >= 2) {
            cmpw(d16(0x2656), 2);
            if (d16(0x2656) == 2) break;
            d16w(0x26cc, decw(d16(0x26cc)));
            if (d16(0x26cc) == 0) break;
        }
        Race_DrawFrame();
        R.bx = 0;
        for (;;) {
            uint16_t bx = R.bx;
            Car_StepAirAndPoints();
            cmpw(d16(V(0x12ae)), 0);
            if (d16(V(0x12ae)) != 0) Car_SinkStep();
            Car_HopStep();
            R.bx = bx;
            R.bx = addw(R.bx, 0x164);
            cmpw(R.bx, 0x42c);
            if ((int16_t)R.bx > 0x42c) break;
        }
        d16w(0x2638, decw(d16(0x2638)));
        if (d16(0x2638) != 0) continue;
        R.ax = d16(0x263a);
        R.bx = 0x263c;
        R.bx = addw(R.bx, R.ax);
        SET_AL(d8(R.bx));
        for (;;) {
            cmpb(AL, c8(0x4ade));
            if ((int8_t)AL < (int8_t)c8(0x4ade)) break;
            mach_frame();
        }
        Gfx_PresentScrolled();
        c8w(0x4ade, 0);
        R.ax = d16(0x263a);
        d16w(0x2638, R.ax);
    }
    /* the race is over */
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 0x10);
    Snd_SilenceEngines();
    R.cx = 100;
    do {
        uint16_t cx = R.cx;
        Sys_WaitTick();
        Race_StateLostPoint();
        Gfx_PresentScrolled();
        R.cx = cx;
        R.cx--;
    } while (R.cx != 0);
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    R.ax = 0;
out:
    cmpw(d16(0x2635), 1);
    if (d16(0x2635) == 1) {
        R.bx = 0;
        d16w(0x26c4, R.bx);
        d16w(0x2678, R.bx);
        R.bx = 0x2c8;
        d16w(0x267a, R.bx);
        R.bx = 0x164;
        d16w(0x267c, R.bx);
        R.bx = 0x42c;
        d16w(0x267e, R.bx);
    }
    cmpw(d16(0x2630), 0);
    if (d16(0x2630) != 0) {
        R.bx = 0;
        R.cx = 0x2c8;
        cmpw(d16(0x2630), 1);
        if (d16(0x2630) != 1) {
            uint16_t t = R.bx;
            R.bx = R.cx;
            R.cx = t;
        }
        d16w(0x2678, R.bx);
        d16w(0x267a, R.cx);
    }
    Pal_FadeOut();
    SET_AH(0x3b);
    R.dx = 0x261c;
    mach_int(0x21);
}

/* The pause (Race_Pause, below).
   Space in a race. The driver stopped (AH = 8, 6); then the cheats: 12-byte records from 193c:1bdb to
   1fcb (theme, track, x, y, effect, argument; words); the first whose theme [28bf] and track [28c0]
   match and whose x and y are within 24 of player 1's vehicle ([2660]; signed differences) applies its
   effect and flashes the play area white (256 x 200 at A000:0020, straight on the screen):
     0  [0406] (a byte) less 1          5  [2915] = 1
     1  the race won: [26c6] = 4,       6  [2917] = 1
        [2635] = 1, the order 0, 2c8h,  7  the vehicle's [12f9] = 0
        164h, 42ch, [2670].. = 7d00h    8  the vehicle's [129c] = 800h
     2, 3, 4  the vehicle's [127c],     9  [2915] = [2919] = 1, [291b] = 4
              [127e], [12a2] = argument
   An effect number above 9 matches but does nothing. Then the pause: [2633] = 1, the engines
   silenced, the PAUSE banner 193c:9d63 drawn plain into 6d78 at (80h, 3Ch), presented; it waits for a
   key ([107e]) or 8Ch ticks ([261f]); [2633] = 2, the frame redrawn (1000:90c5 with [2638] = 1), the
   driver stopped again, and it waits for a key. With the debug flag (193c:0f69, read as cs:a329) set
   and not in theme 9: F12 saves the screen (1000:35bf); else until 8Ch ticks have passed the key mask
   [107c] = 0600h applies effect 9 and 0300h effect 1, and the pause begins again (no cheat search).
   The waits are loops on memory the interrupt handlers move: mach_frame() as their body. */
/* Race_Pause 1000:35f0 */
void Race_Pause(void)
{
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    R.si = 0x1bdb;
    for (;;) {
        cmpw(R.si, 0x1fcb);
        if ((int16_t)R.si > 0x1fcb) goto pause;
        R.ax = d16(R.si);
        R.si += 2;
        cmpb(AL, d8(0x28bf));
        if (AL != d8(0x28bf)) {
            R.si = addw(R.si, 0xa);
            continue;
        }
        R.ax = d16(R.si);
        R.si += 2;
        cmpb(AL, d8(0x28c0));
        if (AL != d8(0x28c0)) {
            R.si = addw(R.si, 8);
            continue;
        }
        R.bx = d16(0x2660);
        R.ax = d16(R.si);
        R.si += 2;
        R.ax = subw(R.ax, d16(V(0x125c)));
        cmpw(R.ax, 0xffff);
        if ((int16_t)R.ax <= -1) R.ax = negw(R.ax);
        cmpw(R.ax, 0x18);
        if ((int16_t)R.ax >= 0x18) {
            R.si = addw(R.si, 6);
            continue;
        }
        R.ax = d16(R.si);
        R.si += 2;
        R.ax = subw(R.ax, d16(V(0x1268)));
        cmpw(R.ax, 0xffff);
        if ((int16_t)R.ax <= -1) R.ax = negw(R.ax);
        cmpw(R.ax, 0x18);
        if ((int16_t)R.ax >= 0x18) {
            R.si = addw(R.si, 4);
            continue;
        }
        R.ax = d16(R.si);
        R.si += 2;
        uint16_t e = R.ax;
        for (uint16_t k = 0; k <= 9; k++) {
            cmpw(e, k);
            if (e == k) break;
        }
        switch (e) {
        case 0: d8w(0x406, (uint8_t)(d8(0x406) - 1)); flags_zs8(d8(0x406)); goto flash;
        case 1: goto won;
        case 2: R.ax = d16(R.si); R.si += 2; d16w(V(0x127c), R.ax); goto flash;
        case 3: R.ax = d16(R.si); R.si += 2; d16w(V(0x127e), R.ax); goto flash;
        case 4: R.ax = d16(R.si); R.si += 2; d16w(V(0x12a2), R.ax); goto flash;
        case 5: d16w(0x2915, 1); goto flash;
        case 6: d16w(0x2917, 1); goto flash;
        case 7: d16w(V(0x12f9), 0); goto flash;
        case 8: d16w(V(0x129c), 0x800); goto flash;
        case 9: goto boost;
        default: goto pause;
        }
    }
boost:
    d16w(0x2915, 1);
    d16w(0x2919, 1);
    d16w(0x291b, 4);
    goto flash;
won:
    d16w(0x26c6, 4);
    d16w(0x2635, 1);
    R.bx = 0;
    d16w(0x26c4, R.bx);
    d16w(0x2678, R.bx);
    R.bx = 0x2c8;
    d16w(0x267a, R.bx);
    R.bx = 0x164;
    d16w(0x267c, R.bx);
    R.bx = 0x42c;
    d16w(0x267e, R.bx);
    d16w(0x2670, 0x7d00);
    d16w(0x2672, 0x7d00);
    d16w(0x2674, 0x7d00);
    d16w(0x2676, 0x7d00);
flash: {
        uint16_t es = R.es;
        R.ax = SEG_VGA;
        R.es = R.ax;
        R.di = 0x20;
        R.ax = 0xffff;
        SET_BL(0xc8);
        do {
            for (R.cx = 0x80; R.cx != 0; R.cx--) {
                mww(R.es, R.di, R.ax);
                R.di = (uint16_t)(R.di + 2);
            }
            R.si = addw(R.si, 0x10);   /* (SI moves too: unused, as the original) */
            R.di = addw(R.di, 0x40);
            SET_BL(BL - 1);
            flags_zs8(BL);
        } while (BL != 0);
        R.es = es;
    }
pause:
    d16w(0x2633, 1);
    Snd_SilenceEngines();
    {
        uint16_t es = R.es, ds = R.ds;
        R.ax = SEG_BACK;
        R.es = R.ax;
        R.di = 0x80;
        R.ax = 0x3c;
        R.si = 0x9d63;
        d8w(0x26cf, 0);
        Gfx_DrawBanner();
        Gfx_PresentScrolled();
        R.ds = ds;
        R.es = es;
    }
    d16w(0x261f, 0);
    d8w(0x107e, 0);
    d8w(0x107f, 0);
    for (;;) {
        cmpb(d8(0x107e), 0);
        if (d8(0x107e) != 0) break;
        cmpw(d16(0x261f), 0x8c);
        if (d16(0x261f) == 0x8c) break;
        mach_frame();
    }
    d16w(0x2633, 2);
    d16w(0x2638, 1);
    Race_DrawFrame();
    Snd_SilenceEngines();
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    for (;;) {
        SET_AL(d8(0x107e));
        R.cf = false;
        flags_zs8(AL);
        if (AL != 0) break;
        mach_frame();
    }
    cmpb(c8(0xa329), 1);
    if (c8(0xa329) != 1) goto done;
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) goto done;
    cmpb(AL, 0x58);
    if (AL == 0x58) {
        File_SaveScreen();
        goto done;
    }
    for (;;) {
        cmpw(d16(0x107c), 0x600);
        if (d16(0x107c) == 0x600) goto boost;
        cmpw(d16(0x107c), 0x300);
        if (d16(0x107c) == 0x300) goto won;
        cmpw(d16(0x261f), 0x8c);
        if ((int16_t)d16(0x261f) >= 0x8c) break;
        mach_frame();
    }
done:
    d16w(0x2633, 0);
}

/* The landing's effect, only with sound device 1 ([0f64]) and for a visible vehicle: 4, 7 in theme 2. */
static void land_effect(void)
{
    cmpw(d16(0xf64), 1);
    if (d16(0xf64) != 1) return;
    uint8_t al = 4;
    cmpb(d8(0x28bf), 5);
    if (d8(0x28bf) != 5) {
        cmpb(d8(0x28bf), 2);
        if (d8(0x28bf) == 2) al = 7;
    }
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, al);
}

/* The height and the head-to-head points (Car_StepAirAndPoints, below).
   Once per vehicle per game step. Theme 9: the time limit [26c8] counts down while [26ca] is 0; at 0
   effect 0Fh, [26ca] = 1 and vehicle 0 to state 10h (time up). The trail and ripple timers [12b2],
   [12b4] count down to 0. For an active vehicle the height [12d6]: in the air it moves by the vertical
   speed [12d4] / 4 (arithmetic shift), which falls by 1 a step; on landing (height <= 0) theme 2 asks
   for a ripple, a pending hit [1384] becomes state 0Dh at the landing point, and with a bounce left
   ([12d8]) the vertical speed is turned round less the theme's gravity (word table 193c:24ff), else the
   height, its speed and [12d8] are reset; the landing effect. On the ground a positive vertical speed
   is a take-off (speed / 4, logical shift). The damped speed the bounce computes in DX is never used
   (as the original).

   Head to head ([2656] = 2), for the vehicle that just scored ([26b8], not 1): the first step starts a
   40h-step pause (both players in state 0Ch, [1382] cleared in themes 2 and 8, [26b6] = the bar [26b4]
   one towards the scorer); every 8 steps the bar and [26b6] are swapped (the leader's flashing); at its
   end the banner set up ([26c2] = 1 while player 1 has laps to go, [26be]/[26c0] = 80h/7Ch, [27b5] = 0),
   the players respawned (1000:78f8) in state 0Dh, [2911] = 2; and the bar moves: at 8 or 0, or when a
   player has no laps left, the winner [26c4] and the race over ([26c6] = 2, effect 10h). When [26c4]
   is not 1 the race is over at the first point (as the original). */
/* Car_StepAirAndPoints 1000:7429 */
void Car_StepAirAndPoints(void)
{
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) {
        cmpw(d16(0x26ca), 0);
        if (d16(0x26ca) == 0) {
            d16w(0x26c8, decw(d16(0x26c8)));
            if (d16(0x26c8) == 0) {
                cmpw(d16(V(0x1250)), 0);
                if (d16(V(0x1250)) != 0) drv_ah_al(5, 0xf);
                d16w(0x26ca, 1);
                d16w(0x12ae, 0x10);
            }
        }
    }
    cmpw(d16(V(0x12b2)), 0);
    if ((int16_t)d16(V(0x12b2)) > 0) d16w(V(0x12b2), decw(d16(V(0x12b2))));
    cmpw(d16(V(0x12b4)), 0);
    if ((int16_t)d16(V(0x12b4)) > 0) d16w(V(0x12b4), decw(d16(V(0x12b4))));
    cmpw(d16(V(0x124c)), 0);
    if (d16(V(0x124c)) == 0) return;
    cmpw(d16(V(0x12d6)), 0);
    if ((int16_t)d16(V(0x12d6)) > 0) {
        R.ax = d16(V(0x12d4));
        R.ax = (uint16_t)((int16_t)R.ax >> 2);
        d16w(V(0x12d6), addw(d16(V(0x12d6)), R.ax));
        d16w(V(0x12d4), decw(d16(V(0x12d4))));
        cmpw(d16(V(0x12d6)), 0);
        if ((int16_t)d16(V(0x12d6)) > 0) return;
        cmpb(d8(0x28bf), 2);
        if (d8(0x28bf) == 2) d16w(V(0x128c), 1);
        cmpw(d16(V(0x1384)), 0);
        if (d16(V(0x1384)) != 0) {
            R.ax = d16(V(0x125c));
            d16w(V(0x12ba), R.ax);
            R.ax = d16(V(0x1268));
            d16w(V(0x12bc), R.ax);
            d16w(V(0x12ae), 0xd);
            d16w(V(0x1384), 0);
        }
        cmpw(d16(V(0x12d8)), 0);
        if (d16(V(0x12d8)) != 0) {
            R.ax = d16(V(0x12d4));
            R.dx = R.ax;
            R.ax = (uint16_t)((int16_t)R.ax >> 2);
            R.dx = subw(R.dx, R.ax);
            R.dx = negw(R.dx);
            d16w(V(0x12d4), negw(d16(V(0x12d4))));
            R.si = 0x24ff;
            SET_AL(d8(0x28bf));
            SET_AH(0);
            R.ax = addw(R.ax, R.ax);
            R.si = addw(R.si, R.ax);
            R.ax = d16(R.si);
            d16w(V(0x12d4), subw(d16(V(0x12d4)), R.ax));
        } else {
            d16w(V(0x12d4), 0);
            d16w(V(0x12d6), 0);
            d16w(V(0x12d8), 1);
        }
        land_effect();
        return;
    }
    cmpw(d16(V(0x12d4)), 0);
    if ((int16_t)d16(V(0x12d4)) > 0) {
        R.ax = d16(V(0x12d4));
        R.ax >>= 2;
        d16w(V(0x12d6), addw(d16(V(0x12d6)), R.ax));
        d16w(V(0x12d4), decw(d16(V(0x12d4))));
        land_effect();
        return;
    }
    d16w(V(0x12d6), 0);
    cmpw(d16(0x2656), 2);
    if (d16(0x2656) != 2) return;
    cmpw(d16(0x26b8), 1);
    if (d16(0x26b8) == 1) return;
    cmpw(d16(0x26b8), R.bx);
    if (d16(0x26b8) != R.bx) return;
    cmpw(d16(0x26ba), 0);
    if (d16(0x26ba) == 0) {
        d16w(0x26ba, 0x40);
        R.bx = d16(0x2660);
        R.si = d16(0x2662);
        d16w(V(0x12ae), 0xc);
        d16w(VS(0x12ae), 0xc);
        cmpb(d8(0x28bf), 2);
        bool clr = d8(0x28bf) == 2;
        if (!clr) {
            cmpb(d8(0x28bf), 8);
            clr = d8(0x28bf) == 8;
        }
        if (clr) {
            d16w(V(0x1382), 0);
            d16w(VS(0x1382), 0);
        }
        R.ax = d16(0x26b4);
        cmpw(d16(0x26b8), R.bx);
        if (d16(0x26b8) == R.bx) R.ax = incw(R.ax);
        else R.ax = decw(R.ax);
        d16w(0x26b6, R.ax);
        return;
    }
    R.bx = d16(0x2660);
    R.ax = d16(0x26b6);
    R.cf = false;
    flags_zs16(d16(0x26ba) & 7);
    if ((d16(0x26ba) & 7) == 0) {
        R.ax = d16(0x26b4);
        R.bx = d16(0x26b6);
        d16w(0x26b4, R.bx);
        d16w(0x26b6, R.ax);
    }
    d16w(0x26ba, decw(d16(0x26ba)));
    cmpw(d16(0x26ba), 0);
    if (d16(0x26ba) != 0) return;
    R.si = d16(0x2660);
    cmpw(d16(VS(0x12ed)), 0);
    if ((int16_t)d16(VS(0x12ed)) > 0) d16w(0x26c2, 1);
    d16w(0x26be, 0x80);
    d16w(0x26c0, 0x7c);
    d16w(0x27b5, 0);
    Race_RespawnPlayers();
    d16w(0x2911, 2);
    R.bx = d16(0x2660);
    R.si = d16(0x2662);
    d16w(V(0x124c), 1);
    d16w(V(0x12ae), 0xd);
    d16w(V(0x12b8), 0);
    d16w(V(0x12b0), 0);
    d16w(VS(0x124c), 1);
    d16w(VS(0x12ae), 0xd);
    d16w(VS(0x12b8), 0);
    d16w(VS(0x12b0), 0);
    cmpw(d16(0x26c4), 1);
    if (d16(0x26c4) != 1) {
        d16w(0x26c6, 2);
        cmpw(d16(V(0x1250)), 0);
        if (d16(V(0x1250)) != 0) drv_ah_al(5, 0x10);
        return;
    }
    cmpw(d16(0x26b8), R.bx);
    if (d16(0x26b8) == R.bx) {
        R.bx = d16(0x2660);
        cmpw(d16(V(0x12ed)), 0);
        if ((int16_t)d16(V(0x12ed)) > 0) {
            d16w(0x26b8, 1);
            d16w(0x26b4, incw(d16(0x26b4)));
            cmpw(d16(0x26b4), 8);
            if (d16(0x26b4) != 8) return;
        }
    } else {
        R.bx = d16(0x2662);
        cmpw(d16(V(0x12ed)), 0);
        if ((int16_t)d16(V(0x12ed)) > 0) {
            d16w(0x26b8, 1);
            d16w(0x26b4, decw(d16(0x26b4)));
            cmpw(d16(0x26b4), 0);
            if (d16(0x26b4) != 0) return;
        }
    }
    d16w(0x26c4, R.bx);
    d16w(0x26c6, 2);
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 0x10);
}

/* Car_ShellStepFrac 1000:87f3
   One sub-pixel step of a shell: AL a signed step added to the fraction in AH's high nibble (the low
   nibble cleared first), a carry or borrow moving CX by one. */
void Car_ShellStepFrac(void)
{
    SET_AH(AH & 0xf0);
    if (AL & 0x80) {
        uint8_t m = (uint8_t)-AL;
        bool borrow = AH < m;
        SET_AH(AH - m);
        if (borrow) R.cx--;
        /* neg al twice: AL back, CF set (it was not 0), ZF/SF of AL */
        R.cf = true;
        flags_zs8(AL);
        return;
    }
    uint16_t s = (uint16_t)(AH + AL);
    SET_AH(s);
    R.cf = s > 0xff;
    flags_zs8(AH);
    if (R.cf) {
        R.cx++;
        flags_zs16(R.cx);
    }
}

/* Snd_InitEngines 1000:7a97
   At the race's start. Sound devices other than 1 ([0f64]): the engine tones of driver channels 0 and
   1 (AH = 0Eh, CX = 32h). Device 1: the four engine channel blocks in the driver's segment written
   directly (2424:0008 + 10h n: +2, +9, +0Ah, +0Bh), with values by theme: 70h, 95h, 00h, 3 in theme 8,
   70h, 95h, 00h, 0 in theme 2, 71h, 95h, 00h, 0 otherwise. */
void Snd_InitEngines(void)
{
    cmpw(d16(0xf64), 1);
    if (d16(0xf64) != 1) {
        SET_AH(0xe);
        SET_AL(0);
        R.cx = 0x32;
        mach_drv();
        SET_AH(0xe);
        SET_AL(1);
        R.cx = 0x32;
        mach_drv();
        return;
    }
    uint16_t ds = R.ds;
    SET_AL(d8(0x28bf));
    R.bx = SEG_OVL;
    R.ds = R.bx;
    SET_BL(0x70);
    R.dx = 0x9500;
    SET_BH(3);
    SET_AH(2);
    cmpb(AL, 8);
    if (AL != 8) {
        R.dx = 0x9500;
        SET_BH(0);
        SET_AH(1);
        SET_BL(0x70);
        cmpb(AL, 2);
        if (AL != 2) SET_BL(0x71);
    }
    R.cx = 4;
    R.di = 8;
    do {
        mbw(R.ds, (uint16_t)(R.di + 2), BL);
        mbw(R.ds, (uint16_t)(R.di + 9), DH);
        mbw(R.ds, (uint16_t)(R.di + 0xa), DL);
        mbw(R.ds, (uint16_t)(R.di + 0xb), BH);
        R.di = addw(R.di, 0x10);
        R.cx--;
    } while (R.cx != 0);
    R.ds = ds;
}

/* Snd_SilenceEngines 1000:7af8
   The engine notes off: devices other than 1, driver AH = 10h with CX = 0 on channels 0 and 1; device 1
   instead zeroes the four vehicles' speeds [127a] (its note is made from them), all registers kept. */
void Snd_SilenceEngines(void)
{
    cmpw(d16(0xf64), 1);
    if (d16(0xf64) != 1) {
        R.cx = 0;
        SET_AH(0x10);
        SET_AL(0);
        mach_drv();
        R.cx = 0;
        SET_AH(0x10);
        SET_AL(1);
        mach_drv();
        return;
    }
    d16w(0x0000 + 0x127a, 0);
    d16w(0x42c + 0x127a, 0);
    d16w(0x164 + 0x127a, 0);
    d16w(0x2c8 + 0x127a, 0);
}

/* Snd_UpdateEngines 1000:7b46
   Once per drawn frame (end of 1000:90c5). Nothing while any vehicle is in state 0Bh (the head-to-head
   scorer spinning). Device 1: per vehicle (four; two in head to head) a pitch abs(speed [127a]) (capped
   at 7FFh) / 10, +30h in the air, 14h with [1382], 0Ah when not visible, plus -2..1 from 1000:7cae (the
   sum's sign dropped), written into its channel block (2424:0008 + 10h n: +0Ah the pitch, +0Ch 98h
   sounding or 91h for a pitch of 0) and, when sounding, effect 40h + n started if the driver says it
   may (AH = 0Ah, then 5). cs:7af5 is the effect number as it counts, cs:7af6 the last pitch (written
   only). Some compares at the loop's head lead nowhere (as the original). Other devices: the tones of
   vehicles 0 and 1 on channels 0 and 1 (1000:7c5b; the second by falling into it). */
void Snd_UpdateEngines(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    static const uint16_t st[4] = {0x12ae, 0x1576, 0x1412, 0x16da};
    for (int i = 0; i < 4; i++) {
        cmpw(d16(st[i]), 0xb);
        if (d16(st[i]) == 0xb) return;
    }
    cmpw(d16(0xf64), 1);
    if (d16(0xf64) != 1) {
        SET_AL(0);
        R.bx = 0;
        Snd_SetEngineTone();
        SET_AL(1);
        R.bx = 0x164;
        Snd_SetEngineTone();
        return;
    }
    R.bx = 0;
    R.di = 8;
    c8w(0x7af5, 0x40);
    for (;;) {
        cmpw(d16(0x2633), 0);
        if (d16(0x2633) == 0) cmpw(d16(V(0x1250)), 0);
        cmpb(d8(0x28bf), 8);
        R.cx = d16(V(0x127a));
        R.cf = false;
        flags_zs16(R.cx);
        if (R.cx & 0x8000) R.cx = negw(R.cx);
        cmpw(R.cx, 0x800);
        if (R.cx >= 0x800) R.cx = 0x7ff;
        R.ax = R.cx;
        R.cx = 0xa;
        {
            uint16_t q = (uint16_t)(R.ax / CL), r = (uint16_t)(R.ax % CL);   /* div cl: no overflow below 800h */
            SET_AL(q);
            SET_AH(r);
        }
        SET_CL(AL);
        cmpw(d16(V(0x12d4)), 0);
        if (d16(V(0x12d4)) != 0) R.cx = addw(R.cx, 0x30);
        cmpw(d16(V(0x1382)), 0);
        if (d16(V(0x1382)) != 0) R.cx = 0x14;
        cmpw(d16(V(0x1250)), 0);
        if (d16(V(0x1250)) == 0) R.cx = 0xa;
        uint16_t ax = R.ax;
        Snd_Random();
        R.ax &= 3;
        R.ax = subw(R.ax, 2);
        R.cx = addw(R.cx, R.ax);
        if (R.cx & 0x8000) R.cx = negw(R.cx);
        R.ax = ax;
        c16w(0x7af6, R.cx);
        SET_DL(CL);
        SET_DH(0x91);
        R.cf = false;
        flags_zs8(DL);
        if (DL != 0) SET_DH(0x98);
        {
            uint16_t ds = R.ds;
            R.ax = SEG_OVL;
            R.ds = R.ax;
            mbw(R.ds, (uint16_t)(R.di + 0xa), DL);
            mbw(R.ds, (uint16_t)(R.di + 0xc), DH);
            R.ds = ds;
        }
        cmpb(DH, 0x91);
        if (DH != 0x91) {
            SET_AL(c8(0x7af5));
            SET_AH(0xa);
            mach_drv();
            R.cf = false;
            flags_zs8(AL);
            if (AL != 0) {
                SET_AL(c8(0x7af5));
                SET_AH(5);
                mach_drv();
            }
        }
        R.bx = addw(R.bx, 0x164);
        c8w(0x7af5, (uint8_t)(c8(0x7af5) + 1));
        flags_zs8(c8(0x7af5));
        R.di = addw(R.di, 0x10);
        cmpw(d16(0x2656), 2);
        if (d16(0x2656) != 2) {
            cmpb(c8(0x7af5), 0x43);
            if ((int8_t)c8(0x7af5) <= 0x43) continue;
        }
        cmpb(c8(0x7af5), 0x41);
        if ((int8_t)c8(0x7af5) > 0x41) return;
    }
}

/* Snd_SetEngineTone 1000:7c5b
   AL = driver channel, BX = vehicle: the tone 2000h - (2 abs(speed [127a]) capped at 7FFh before the
   doubling, + 7F7h in the air; 100h with [1382] or when not visible) with -8..7 from 1000:7cae (the
   sum's sign dropped); driver AH = 0Eh, CX = the tone. */
void Snd_SetEngineTone(void)
{
    R.cx = d16(V(0x127a));
    R.cf = false;
    flags_zs16(R.cx);
    if (R.cx & 0x8000) R.cx = negw(R.cx);
    cmpw(R.cx, 0x800);
    if (R.cx >= 0x800) R.cx = 0x7ff;
    R.cx = addw(R.cx, R.cx);
    cmpw(d16(V(0x12d4)), 0);
    if (d16(V(0x12d4)) != 0) R.cx = addw(R.cx, 0x7f7);
    cmpw(d16(V(0x1382)), 0);
    if (d16(V(0x1382)) != 0) R.cx = 0x100;
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) == 0) R.cx = 0x100;
    uint16_t ax = R.ax;
    Snd_Random();
    R.ax &= 0xf;
    R.ax = subw(R.ax, 8);
    R.cx = addw(R.cx, R.ax);
    if (R.cx & 0x8000) R.cx = negw(R.cx);
    R.ax = ax;
    R.cx = negw(R.cx);
    R.cx = addw(R.cx, 0x2000);
    SET_AH(0xe);
    mach_drv();
}

/* Snd_Random 1000:7cae
   A pseudo-random AX from four bytes in the code segment, cs:7cdb..7cde, read and written as three
   overlapping words: [7cdb] = ([7cdb] xor 234h) + 2244h; [7cdc] = ([7cdc] + [7cdb] + carry) xor
   22ABh; [7cdd] = ([7cdd] + 233h + carry) xor 5345h, AX the last. The xor before the last add clears
   the carry it adds (as the original). */
void Snd_Random(void)
{
    R.ax = c16(0x7cdb);
    R.ax ^= 0x234;
    R.ax = addw(R.ax, 0x2244);
    c16w(0x7cdb, R.ax);
    R.ax = c16(0x7cdc);
    {
        uint32_t s = (uint32_t)R.ax + c16(0x7cdb) + (R.cf ? 1u : 0u);
        R.ax = (uint16_t)s;
    }
    R.ax ^= 0x22ab;
    R.cf = false;
    c16w(0x7cdc, R.ax);
    R.ax = c16(0x7cdd);
    R.ax = (uint16_t)(R.ax + 0x233);   /* adc with the carry the xor cleared */
    R.ax ^= 0x5345;
    R.cf = false;
    flags_zs16(R.ax);
    c16w(0x7cdd, R.ax);
}
