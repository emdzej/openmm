/* The race picture (docs/re/race.md, "The drawn frame"): the map of tiles into the back buffer, the
   vehicles, their shadows, trails, ripples and shells over it, each vehicle's state handler, the tiles
   with a foreground drawn again over the vehicles, then the HUD and the banners.

   The back buffer is segment 6d78, rows of 110h bytes with a border of 16 rows and 16 columns; the
   world is C00h pixels square and wraps, so every screen position is the world position less the
   camera ([264a], [264c]) with C00h added once when that falls left of (or above) the sprite's margin.
   Vehicle records are 164h bytes from 193c:124c, BX the record's offset (0, 164h, 2c8h, 42ch).

   All routines here are register-exact (docs/re/porting.md): R is the CPU's registers, the flags
   follow the original's last flag-setting instruction where a routine returns. */
#include "cpu.h"
#include "fntab.h"
#include "machine.h"
#include "mem.h"
#include "segs.h"
#include "pending.h"
#include "routines.h"

/* ---- flags as the original's instructions leave them ---- */
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
static inline uint16_t decw(uint16_t a) { a = (uint16_t)(a - 1); flags_zs16(a); return a; }   /* CF kept */
static inline uint16_t incw(uint16_t a) { a = (uint16_t)(a + 1); flags_zs16(a); return a; }   /* CF kept */
static inline uint16_t andw(uint16_t a, uint16_t b) { a &= b; R.cf = false; flags_zs16(a); return a; }

/* A field of the vehicle record at BX (or at another register's record). */
#define V(off) ((uint16_t)(R.bx + (off)))
#define S(off) ((uint16_t)(R.si + (off)))

/* sub reg, camera / cmp reg, -m / jg / add reg, C00h: the world position on screen */
static uint16_t on_screen(uint16_t v, uint16_t cam, int16_t m)
{
    v = subw(v, cam);
    cmpw(v, (uint16_t)m);
    if ((int16_t)v <= m) v = addw(v, 0xc00);
    return v;
}

/* popa: the general registers back (SP's slot skipped, as the CPU does) */
static void popa(const Cpu *s)
{
    R.ax = s->ax, R.bx = s->bx, R.cx = s->cx, R.dx = s->dx;
    R.si = s->si, R.di = s->di, R.bp = s->bp;
}

static void drv_ah_al(uint8_t ah, uint8_t al)
{
    SET_AH(ah);
    SET_AL(al);
    mach_drv();
}

/* The race frame (Race_DrawFrame, below).
   Only on the last game step of a presented frame ([2638] = 1). The tile animations of themes 1, 3, 5
   (backdrop scroll), 2 (water) and 8; then the map: 17 columns x 14 rows of 16 x 16 tiles from the
   camera's tile, the map being words at 3b78:0000 (192 x 192 tiles, rows 180h bytes, so DS moves 18h
   paragraphs a row and wraps by 1200h; a row that crosses column C0h steps DS back a row to wrap left).
   Bits 0-14 of a map word are the tile, at 1b78:0000 + tile * 10h paragraphs; bit 15 marks a tile with
   a foreground. The low two bits of the scroll go to cs:8994 (the blits and the present take them off),
   the buffer offset is kept aligned to 4.

   The original keeps the list of foreground tiles on its stack: SP saved in cs:90c3, a push of the
   buffer offset and the tile's segment for each, and after the vehicles they are popped (last first)
   and redrawn through their foreground graphics (C00h further), colour 0 transparent, until SP is back
   at cs:90c3. Nothing reads those stack bytes as memory, so the port keeps the list in a local array
   in the same order; at most 17 x 14 = 238 tiles (952 bytes against the 1000-byte stack the original
   has: a screen full of foreground tiles would come close to overflowing it; not seen). cs:90c3 is
   still written with the SP the original has there, and BP is left holding it, as in the original.

   Then the head-to-head respawn or point (1000:78f8 / 1000:7759), the vehicles (1000:7ce0), the
   foreground, the HUD (1000:8dfc), the head-to-head banners and the engine sounds (1000:7b46). */
/* Race_DrawFrame 1000:90c5 */
void Race_DrawFrame(void)
{
    cmpw(d16(0x2638), 1);
    if (d16(0x2638) != 1) return;
    uint16_t ds0 = R.ds, es0 = R.es;
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.ax = SEG_BACK;
    R.es = R.ax;
    uint8_t th = mb(R.ds, 0x28bf);
    cmpb(th, 1);
    if (th == 1) goto anim;
    cmpb(th, 3);
    if (th == 3) goto anim;
    cmpb(th, 5);
    if (th == 5) goto anim;
    goto noanim;
anim:
    Track_ScrollBackdrop();
noanim:
    th = mb(R.ds, 0x28bf);
    cmpb(th, 2);
    if (th == 2) Track_AnimateWater2();
    th = mb(R.ds, 0x28bf);
    cmpb(th, 8);
    if (th == 8) Track_AnimateTiles8();

    uint16_t ds1 = R.ds, si1 = R.si;
    /* the camera's tile column (DL, DH = DL + 11h the end) and the fine scroll */
    uint16_t t = mw(R.ds, 0x264a);
    R.ax = (uint16_t)(t >> 4);
    R.si = R.ax;
    SET_DL(AL);
    SET_AL(AL + 0x11);
    SET_DH(AL);
    R.ax = (uint16_t)(t & 0xf);
    R.di = (uint16_t)(0x10 - R.ax);
    /* the tile row (CL, CH = CL + 0Eh) */
    t = mw(R.ds, 0x264c);
    R.ax = (uint16_t)(t >> 4);
    SET_CL(AL);
    SET_AL(AL + 0x0e);
    SET_CH(AL);
    R.ax = (uint16_t)(t & 0xf);
    R.bx = (uint16_t)(0x10 - R.ax);
    uint16_t rowpix = R.bx;
    /* the map word: row * C0h + column, as a paragraph and an offset */
    SET_BH(CL);
    SET_BL(0);
    R.bx >>= 1;
    R.ax = (uint16_t)(R.bx >> 1);
    R.ax = (uint16_t)(R.ax + R.bx);
    R.si = (uint16_t)(R.si + R.ax);
    R.ax = (uint16_t)(R.si >> 3);
    R.ax = (uint16_t)(R.ax + SEG_MAP);
    R.ds = R.ax;
    R.si = (uint16_t)(R.si << 1);
    R.si &= 0xf;
    /* the buffer offset: (16 - y fine) rows of 110h plus (16 - x fine) */
    R.ax = rowpix;
    SET_AH(AL);
    SET_AL(0);
    R.bx = (uint16_t)(R.ax >> 4);
    R.ax = (uint16_t)(R.ax + R.bx);
    R.di = (uint16_t)(R.di + R.ax);
    /* SP here: four words below the entry's (ds, es, ds, si pushed) */
    uint16_t sp_mark = (uint16_t)(R.sp - 8);
    c16w(0x90c3, sp_mark);
    c16w(0x8994, R.di);
    c16w(0x8994, (uint16_t)(c16(0x8994) & 3));
    R.di &= 0xfffc;
    R.bx = R.cx;

    uint16_t fg_off[17 * 14], fg_seg[17 * 14];
    int nfg = 0;
    for (;;) {
        if (DL == 0xc0) R.ds = (uint16_t)(R.ds - 0x18);
        R.ax = mw(R.ds, R.si);
        R.si = (uint16_t)(R.si + 2);
        bool fg = (R.ax & 0x8000) != 0;
        SET_AH(AH & 0x7f);
        R.ax = (uint16_t)(R.ax << 4);
        R.ax = (uint16_t)(R.ax + SEG_TILES);
        if (fg && nfg < 17 * 14) {
            fg_off[nfg] = R.di;
            fg_seg[nfg] = R.ax;
            nfg++;
        }
        /* the tile: 16 rows of 16 bytes (eight movsw, four movsd once 1000:7a75 has patched the loop:
           the same bytes either way) */
        uint16_t src = 0;
        for (int row = 0; row < 16; row++) {
            for (int i = 0; i < 16; i++) {
                mbw(R.es, R.di, mb(R.ax, src));
                R.di++;
                src++;
            }
            R.di = (uint16_t)(R.di + 0x100);
        }
        R.cx = 0;   /* (the loop's count run out; pop ds, pop si give the map pointer back) */
        R.di = (uint16_t)(R.di - 0x10f0);
        SET_DL(DL + 1);
        if (DL < DH) continue;
        if (DL >= 0xc1) R.ds = (uint16_t)(R.ds + 0x18);
        R.di = (uint16_t)(R.di + 0xff0);
        R.ax = (uint16_t)(R.ds + 0x18);
        R.ds = R.ax;
        R.si = (uint16_t)(R.si - 0x22);
        SET_DL(DL - 0x11);
        SET_BL(BL + 1);
        if (BL == 0xc0) {
            R.ax = (uint16_t)(R.ds - 0x1200);
            R.ds = R.ax;
        }
        cmpb(BL, BH);
        if (BL < BH) continue;
        break;
    }

    R.ax = SEG_DATA;
    R.ds = R.ax;
    cmpw(d16(0x2913), 0);
    if (d16(0x2913) != 0) {
        Race_RespawnPlayers();
    } else {
        cmpw(d16(0x2911), 1);
        if (d16(0x2911) == 1) Race_AwardPoint();
    }
    Car_DrawAndRunStates();

    /* the foreground tiles, last pushed first */
    R.bp = c16(0x90c3);
    while (nfg > 0) {
        nfg--;
        R.ds = fg_seg[nfg];
        R.di = fg_off[nfg];
        R.si = 0xc00;
        R.cx = 0x1010;
        while (CH != 0) {
            uint8_t px = mb(R.ds, R.si);
            R.si++;
            SET_AL(px);
            if (px != 0) mbw(R.es, R.di, px);
            R.di++;
            SET_CL(CL - 1);
            if (CL != 0) continue;
            SET_CL(0x10);
            R.di = (uint16_t)(R.di + 0x100);
            SET_CH(CH - 1);
        }
    }
    cmpw(R.sp, R.sp);   /* cmp sp, bp: equal, the list is empty */

    R.ax = SEG_DATA;
    R.ds = R.ax;
    Hud_DrawPositions();
    R.bx = d16(0x2660);
    cmpw(d16(V(0x12ae)), 0xc);
    if (d16(V(0x12ae)) == 0xc) {
        Race_StateLostPoint();
    } else {
        cmpw(d16(V(0x12ae)), 0xb);
        if (d16(V(0x12ae)) == 0xb) Race_StateScored();
    }
    R.bx = d16(0x2662);
    cmpw(d16(V(0x12ae)), 0xc);
    if (d16(V(0x12ae)) == 0xc) {
        Race_StateLostPoint();
    } else {
        cmpw(d16(V(0x12ae)), 0xb);
        if (d16(V(0x12ae)) == 0xb) Race_StateScored();
        cmpw(d16(0x26c2), 0);
        if (d16(0x26c2) != 0) Race_DrawBannerOut();
    }
    Snd_UpdateEngines();
    R.si = si1;
    R.ds = ds1;
    R.es = es0;
    R.ds = ds0;
}

/* Car_DrawAndRunStates 1000:7ce0
   Per drawn frame, between the map and the foreground: the shadows of the active vehicles in the air,
   the ripples and trails of all four, the shells in flight, then each vehicle's state handler (word
   table 193c:278f by [12ae]; state 0Eh runs even for an inactive vehicle), then the position label over
   a vehicle that has finished, or over all at the race end (not in head to head). In theme 9 the loop
   stops after the first vehicle whose handler ran (as the original: theme 9 has one racer). */
void Car_DrawAndRunStates(void)
{
    for (R.bx = 0;; ) {
        cmpw(d16(V(0x124c)), 0);
        if (d16(V(0x124c)) != 0) {
            cmpw(d16(V(0x12d6)), 0);
            if (d16(V(0x12d6)) != 0) Car_DrawShadow();
        }
        R.bx = addw(R.bx, 0x164);
        cmpw(R.bx, 0x42c);
        if ((int16_t)R.bx > 0x42c) break;
    }
    for (R.bx = 0;; ) {
        Car_DrawRipples();
        Car_DrawTrail();
        R.bx = addw(R.bx, 0x164);
        cmpw(R.bx, 0x42c);
        if ((int16_t)R.bx > 0x42c) break;
    }
    for (R.bx = 0;; ) {
        cmpw(d16(V(0x13a4)), 0);
        if (d16(V(0x13a4)) != 0) Car_DrawShell();
        R.bx = addw(R.bx, 0x164);
        cmpw(R.bx, 0x42c);
        if ((int16_t)R.bx > 0x42c) break;
    }
    for (R.bx = 0;; ) {
        cmpw(d16(V(0x12ae)), 0xe);
        bool run = d16(V(0x12ae)) == 0xe;
        if (!run) {
            cmpw(d16(V(0x124c)), 0);
            run = d16(V(0x124c)) != 0;
        }
        if (run) {
            uint16_t bx = R.bx, ds = R.ds;
            R.si = d16(V(0x12ae));
            R.si = (uint16_t)(R.si << 1);
            R.si = addw(R.si, 0x278f);
            uint16_t target = mw(R.ds, R.si);
            const FnEntry *f = fntab_find(0x10000000u | target);
            if (f) f->fn();
            else PENDING_NOTE(0x10000000u | target, "vehicle state handler not in names.tsv");
            R.ds = ds;
            R.bx = bx;
            cmpb(d8(0x28bf), 9);
            if (d8(0x28bf) == 9) return;
            cmpw(d16(0x2656), 2);
            if (d16(0x2656) != 2) {
                cmpw(d16(0x26c6), 2);
                bool label = (int16_t)d16(0x26c6) >= 2;
                if (!label) {
                    cmpw(d16(V(0x12ed)), 0);
                    label = d16(V(0x12ed)) == 0;
                }
                if (label) Hud_DrawCarPosition();
            }
        }
        R.bx = addw(R.bx, 0x164);
        cmpw(R.bx, 0x42c);
        if ((int16_t)R.bx > 0x42c) break;
    }
}

/* Car_Draw 1000:7d73
   Vehicle BX at its position, lifted by its height [12d6] (not in theme 8), its frame from the heading
   [1278] (32 directions, 8 heading steps each). [1250] says whether it was drawn: 0 for the vehicle
   hidden by the head-to-head banner ([2621]) and when clipped away. 24 x 24 frames from segment 4d78
   recoloured by [1252]; in theme 9 vehicle 0 is 40 x 40 and drawn plain, the others not at all. In
   theme 8 the rotor (1000:843d) over it unless it is in state 2 or 0Dh. Also the handler of states 0,
   0Bh and 0Ch. All registers kept. */
void Car_Draw(void)
{
    Cpu s = R;
    cmpw(d16(0x2621), R.bx);
    if (d16(0x2621) == R.bx) goto hidden;
    d16w(V(0x1250), 1);
    R.dx = 0;
    cmpb(d8(0x28bf), 8);
    if (d8(0x28bf) != 8) R.dx = d16(V(0x12d6));
    R.di = d16(V(0x125c));
    R.ax = d16(V(0x1268));
    R.di = subw(R.di, R.dx);
    R.ax = subw(R.ax, R.dx);
    R.si = d16(V(0x1278));
    R.si = andw(R.si, 0xf8);
    R.cx = R.si;
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) {
        cmpw(R.bx, 0);
        if (R.bx != 0) goto done;
        /* frame (heading / 8) * 640h: heading * 200 */
        R.dx = (uint16_t)(R.si << 7);
        R.si = (uint16_t)(R.si << 6);
        R.cx = (uint16_t)(R.cx << 3);
        R.si = addw(R.si, R.cx);
        R.si = addw(R.si, R.dx);
        R.di = on_screen(R.di, d16(0x264a), -4);
        R.ax = on_screen(R.ax, d16(0x264c), -4);
        R.ax = subw(R.ax, 0x14);
        R.di = subw(R.di, 0x14);
        R.cx = 0x28;
        R.dx = R.cx;
        Gfx_ClipSprite();
        if (R.cf) goto hidden;
        uint16_t ds = R.ds;
        R.bx = SEG_CARS;
        R.ds = R.bx;
        Gfx_BlitSprite();
        R.ds = ds;
        goto done;
    }
    /* frame (heading / 8) * 240h: heading * 72 */
    R.si = (uint16_t)(R.si << 6);
    R.cx = (uint16_t)(R.cx << 3);
    R.si = addw(R.si, R.cx);
    R.cx = 0x18;
    R.dx = R.cx;
    R.di = on_screen(R.di, d16(0x264a), -0xc);
    R.ax = on_screen(R.ax, d16(0x264c), -0xc);
    R.ax = subw(R.ax, 0xc);
    R.di = subw(R.di, 0xc);
    Gfx_ClipSprite();
    if (R.cf) goto hidden;
    R.ax = d16(V(0x1252));
    {
        uint16_t ds = R.ds, bx = R.bx;
        R.bx = SEG_CARS;
        R.ds = R.bx;
        Gfx_BlitRecolour();
        R.bx = bx;
        R.ds = ds;
    }
    cmpb(d8(0x28bf), 8);
    if (d8(0x28bf) != 8) goto done;
    cmpw(d16(V(0x12ae)), 0xd);
    if (d16(V(0x12ae)) == 0xd) goto done;
    cmpw(d16(V(0x12ae)), 2);
    if (d16(V(0x12ae)) == 2) goto done;
    Car_DrawOverlay8();
    goto done;
hidden:
    d16w(V(0x1250), 0);
done:
    popa(&s);
}

/* Car_DrawShadow 1000:7e5c
   Vehicle BX's shadow: its frame offset down and right by the height, drawn in colour 0 (1000:8c3c)
   from segment 4d78. None in theme 8; in theme 9 only vehicle 0's (40 x 40). All registers kept. */
void Car_DrawShadow(void)
{
    Cpu s = R;
    R.dx = d16(V(0x12d6));
    R.di = d16(V(0x125c));
    R.ax = d16(V(0x1268));
    R.di = addw(R.di, R.dx);
    R.ax = addw(R.ax, R.dx);
    R.si = d16(V(0x1278));
    R.si = andw(R.si, 0xf8);
    R.cx = R.si;
    cmpb(d8(0x28bf), 8);
    if (d8(0x28bf) == 8) goto done;
    cmpb(d8(0x28bf), 9);
    if (d8(0x28bf) == 9) {
        cmpw(R.bx, 0);
        if (R.bx != 0) goto done;
        R.dx = (uint16_t)(R.si << 7);
        R.si = (uint16_t)(R.si << 6);
        R.cx = (uint16_t)(R.cx << 3);
        R.si = addw(R.si, R.cx);
        R.si = addw(R.si, R.dx);
        R.di = on_screen(R.di, d16(0x264a), -4);
        R.ax = on_screen(R.ax, d16(0x264c), -4);
        R.ax = subw(R.ax, 0x14);
        R.di = subw(R.di, 0x14);
        R.cx = 0x28;
    } else {
        R.si = (uint16_t)(R.si << 6);
        R.cx = (uint16_t)(R.cx << 3);
        R.si = addw(R.si, R.cx);
        R.di = on_screen(R.di, d16(0x264a), -0xc);
        R.ax = on_screen(R.ax, d16(0x264c), -0xc);
        R.ax = subw(R.ax, 0xc);
        R.di = subw(R.di, 0xc);
        R.cx = 0x18;
    }
    R.dx = R.cx;
    Gfx_ClipSprite();
    if (R.cf) goto done;
    {
        uint16_t ds = R.ds;
        R.bx = SEG_CARS;
        R.ds = R.bx;
        Gfx_BlitSilhouette();
        R.ds = ds;
    }
done:
    popa(&s);
}

/* The frame sequences of states 4, 5 and 1: a table of step durations (words) and, a fixed distance
   further, the frame of each step (-2: none this step, -1: the end). The step [12b6] advances when the
   animation counter [12b0] has reached the step's duration. */

/* Car_StateAnim5 1000:7efa
   State 5: with [12c2] set just the vehicle; else the sequence at 193c:2855 (frames 12h further) drawn
   with 1000:7fe8, effect 8 when step 4 begins; at the end state 7 (respawn). */
void Car_StateAnim5(void)
{
    cmpw(d16(V(0x12c2)), 0);
    if (d16(V(0x12c2)) != 0) {
        Car_Draw();
        return;
    }
    R.si = d16(V(0x12b6));
    R.si = (uint16_t)(R.si << 1);
    R.si = addw(R.si, 0x2855);
    R.ax = d16(R.si);
    R.si = addw(R.si, 0x12);
    R.cx = d16(R.si);
    R.si = R.cx;
    cmpw(R.si, 0xfffe);
    if (R.si != 0xfffe) {
        cmpw(R.si, 0xffff);
        if (R.si == 0xffff) {
            d16w(V(0x12b6), 0);
            d16w(V(0x12ae), 7);
            d16w(V(0x12b0), 0);
            d16w(V(0x1382), 0);
            return;
        }
        Car_DrawEffect24();
    }
    cmpw(d16(V(0x12b0)), R.ax);
    if ((int16_t)d16(V(0x12b0)) < (int16_t)R.ax) return;
    d16w(V(0x12b6), addw(d16(V(0x12b6)), 1));
    cmpw(d16(V(0x12b6)), 4);
    if (d16(V(0x12b6)) != 4) return;
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 8);
}

/* Car_StateAnim4 1000:7f62
   State 4 (knocked out): the first vehicle to get here is noted in [2682] (the head-to-head point);
   with [12c2] set just the vehicle, else the sequence at 193c:2879 as 1000:7efa; at the end the
   vehicle is switched off: [124c] = 0, state 0Eh, [1382] = [1380] = 1, [137e] = 0. */
void Car_StateAnim4(void)
{
    cmpw(d16(0x2682), 0xffff);
    if (d16(0x2682) == 0xffff) d16w(0x2682, R.bx);
    cmpw(d16(V(0x12c2)), 0);
    if (d16(V(0x12c2)) != 0) {
        Car_Draw();
        return;
    }
    R.si = d16(V(0x12b6));
    R.si = (uint16_t)(R.si << 1);
    R.si = addw(R.si, 0x2879);
    R.ax = d16(R.si);
    R.si = addw(R.si, 0x12);
    R.cx = d16(R.si);
    R.si = R.cx;
    cmpw(R.si, 0xfffe);
    if (R.si != 0xfffe) {
        cmpw(R.si, 0xffff);
        if (R.si == 0xffff) {
            d16w(V(0x12b6), 0);
            d16w(V(0x12b0), 0);
            d16w(V(0x124c), 0);
            d16w(V(0x1382), 1);
            d16w(V(0x12ae), 0xe);
            d16w(V(0x1380), 1);
            d16w(V(0x137e), 0);
            return;
        }
        Car_DrawEffect24();
    }
    cmpw(d16(V(0x12b0)), R.ax);
    if ((int16_t)d16(V(0x12b0)) < (int16_t)R.ax) return;
    d16w(V(0x12b6), addw(d16(V(0x12b6)), 1));
    cmpw(d16(V(0x12b6)), 4);
    if (d16(V(0x12b6)) != 4) return;
    cmpw(d16(V(0x1250)), 0);
    if (d16(V(0x1250)) != 0) drv_ah_al(5, 8);
}

/* Car_DrawEffect24 1000:7fe8
   SI = frame: a 24 x 24 frame (240h bytes each) of the effect sprites in segment 5d78 centred on
   vehicle BX, recoloured by [1252]. All registers kept. */
void Car_DrawEffect24(void)
{
    Cpu s = R;
    R.di = d16(V(0x125c));
    R.ax = d16(V(0x1268));
    R.di = on_screen(R.di, d16(0x264a), -0xc);
    R.ax = on_screen(R.ax, d16(0x264c), -0xc);
    R.ax = subw(R.ax, 0xc);
    R.di = subw(R.di, 0xc);
    R.dx = R.si;
    R.si = (uint16_t)(R.si << 9);
    R.dx = (uint16_t)(R.dx << 6);
    R.si = addw(R.si, R.dx);
    R.dx = 0x18;
    R.cx = R.dx;
    Gfx_ClipSprite();
    if (!R.cf) {
        R.ax = d16(V(0x1252));
        uint16_t ds = R.ds;
        R.bx = SEG_EFFECTS;
        R.ds = R.bx;
        Gfx_BlitRecolour();
        R.ds = ds;
    }
    popa(&s);
}

/* Car_DrawEffect40 1000:8034
   SI = frame (5 taken off above 4): a 40 x 40 frame (640h bytes each) from segment 5d78 centred on
   vehicle BX, plain: theme 9's version of 1000:7fe8. All registers kept. */
void Car_DrawEffect40(void)
{
    Cpu s = R;
    cmpw(R.si, 4);
    if ((int16_t)R.si > 4) R.si = subw(R.si, 5);
    R.cx = 0x640;
    R.ax = R.si;
    uint32_t p = (uint32_t)R.ax * R.cx;   /* mul cx */
    R.ax = (uint16_t)p;
    R.dx = (uint16_t)(p >> 16);
    R.cf = R.dx != 0;
    R.si = R.ax;
    R.di = d16(V(0x125c));
    R.ax = d16(V(0x1268));
    R.di = on_screen(R.di, d16(0x264a), -4);
    R.ax = on_screen(R.ax, d16(0x264c), -4);
    R.ax = subw(R.ax, 0x14);
    R.di = subw(R.di, 0x14);
    R.dx = 0x28;
    R.cx = R.dx;
    Gfx_ClipSprite();
    if (!R.cf) {
        uint16_t ds = R.ds;
        R.bx = SEG_EFFECTS;
        R.ds = R.bx;
        Gfx_BlitSprite();
        R.ds = ds;
    }
    popa(&s);
}

/* One end of a new trail entry: beside the vehicle's tail, the heading turned by 80h (behind) and by
   the side angle already in SI, then the sine table 193c:10a0 (256 signed bytes, cos = sin + 40h)
   divided by 16 for the offset; the point wrapped into the world (y with -0Ch, as the original). */
static void trail_point(void)
{
    R.si = addw(R.si, 0x80);
    R.si = andw(R.si, 0xff);
    SET_AL(d8(S(0x10a0)));
    R.ax = (uint16_t)(int16_t)(int8_t)AL;   /* cbw */
    R.ax = (uint16_t)((int16_t)R.ax >> 4);
    R.di = addw(R.di, R.ax);
    R.si = subw(R.si, 0x40);
    R.si = andw(R.si, 0xff);
    SET_AL(d8(S(0x10a0)));
    R.ax = (uint16_t)(int16_t)(int8_t)AL;
    R.ax = (uint16_t)((int16_t)R.ax >> 4);
    R.dx = addw(R.dx, R.ax);
    cmpw(R.di, 0xffff);
    if ((int16_t)R.di <= -1) R.di = addw(R.di, 0xc00);
    cmpw(R.di, 0xc00);
    if ((int16_t)R.di >= 0xc00) R.di = subw(R.di, 0xc00);
    cmpw(R.dx, 0xfff4);
    if ((int16_t)R.dx <= -0xc) R.dx = addw(R.dx, 0xc00);
    cmpw(R.dx, 0xc00);
    if ((int16_t)R.dx >= 0xc00) R.dx = subw(R.dx, 0xc00);
}

/* Theme 2 sways a trail point: the angle (SI) moves by the sway [a], which moves by its speed [b] and
   turns back beyond +-1Eh. */
static void trail_sway(uint16_t a, uint16_t b)
{
    R.ax = d16(V(a));
    R.si = addw(R.si, R.ax);
    R.si = andw(R.si, 0xff);
    R.ax = addw(R.ax, d16(V(b)));
    d16w(V(a), R.ax);
    cmpw(R.ax, 0xffff);
    if ((int16_t)R.ax <= -1) R.ax = subw(0, R.ax);
    cmpw(R.ax, 0x1e);
    if ((int16_t)R.ax >= 0x1e) d16w(V(b), subw(0, d16(V(b))));
}

/* Car_DrawTrail 1000:8083
   Vehicle BX's skid marks or wake: a ring of eight 12-byte entries at [12fd] (x1, y1, x2, y2, frame,
   sheet) with the cursor [1296]. While the cursor is above 0 the live entries (frame != -1) are drawn,
   both points as 8 x 8 frames (1000:8cd0), and aged by a frame each time the timer [12b2] has run out
   (frame 8 = gone). Then, when [12b2] has run out (it is reset to 3), a new entry when skidding ([1284]
   with a speed [1272]/[1276]: sheet 43e3), or on request [1288] (41e3) or [128a] (3fe3), at two points
   beside the tail (+-1Eh around the heading; theme 2 sways them). The new entry's first point and frame
   go at [bx + cursor + 12fd], then the cursor steps by 0Ch, from 60h back to 0Ch: entry 0 is used only
   once (as the original). AX BX CX DX SI DI DS kept, BP not. */
void Car_DrawTrail(void)
{
    uint16_t ax = R.ax, bx = R.bx, cx = R.cx, dx = R.dx, si = R.si, di = R.di, ds = R.ds;
    cmpw(d16(V(0x1296)), 0);
    if ((int16_t)d16(V(0x1296)) > 0) {
        R.dx = 0;
        for (;;) {
            R.bp = R.dx;
            R.bp = addw(R.bp, R.bx);
            R.cx = d16((uint16_t)(R.bp + 0x1305));
            cmpw(R.cx, 0xffff);
            if (R.cx != 0xffff) {
                R.si = d16((uint16_t)(R.bp + 0x1307));
                R.di = d16((uint16_t)(R.bp + 0x12fd));
                R.ax = d16((uint16_t)(R.bp + 0x12ff));
                R.di = on_screen(R.di, d16(0x264a), -8);
                R.ax = on_screen(R.ax, d16(0x264c), -8);
                Gfx_DrawSmall8();
                R.di = d16((uint16_t)(R.bp + 0x1301));
                R.ax = d16((uint16_t)(R.bp + 0x1303));
                R.di = on_screen(R.di, d16(0x264a), -8);
                R.ax = on_screen(R.ax, d16(0x264c), -8);
                Gfx_DrawSmall8();
                cmpw(d16(V(0x12b2)), 0);
                if ((int16_t)d16(V(0x12b2)) <= 0) {
                    uint16_t f = incw(d16((uint16_t)(R.bp + 0x1305)));
                    d16w((uint16_t)(R.bp + 0x1305), f);
                    cmpw(f, 8);
                    if ((int16_t)f >= 8) d16w((uint16_t)(R.bp + 0x1305), 0xffff);
                }
            }
            R.dx = addw(R.dx, 0xc);
            cmpw(R.dx, 0x60);
            if ((int16_t)R.dx >= 0x60) break;
        }
    }
    cmpw(d16(V(0x12b2)), 0);
    if ((int16_t)d16(V(0x12b2)) > 0) goto out;
    d16w(V(0x12b2), 3);
    cmpw(d16(V(0x1284)), 0);
    if (d16(V(0x1284)) != 0) {
        cmpw(d16(V(0x1272)), 0);
        if (d16(V(0x1272)) == 0) {
            cmpw(d16(V(0x1276)), 0);
            if (d16(V(0x1276)) == 0) goto out;
        }
        R.si = 0x43e3;
    } else {
        cmpw(d16(V(0x1288)), 0);
        if (d16(V(0x1288)) != 0) {
            d16w(V(0x1288), 0);
            R.si = 0x41e3;
        } else {
            cmpw(d16(V(0x128a)), 0);
            if (d16(V(0x128a)) == 0) goto out;
            d16w(V(0x128a), 0);
            R.si = 0x3fe3;
        }
    }
    {
        uint16_t sheet = R.si;
        R.si = d16(V(0x1278));
        R.di = d16(V(0x125c));
        R.dx = d16(V(0x1268));
        cmpb(d8(0x28bf), 2);
        if (d8(0x28bf) != 2) R.si = subw(R.si, 0x1e);
        else trail_sway(0x128e, 0x1290);
        trail_point();
        R.si = sheet;
        R.bp = R.bx;
        R.bp = addw(R.bp, d16(V(0x1296)));
        d16w((uint16_t)(R.bp + 0x12fd), R.di);
        d16w((uint16_t)(R.bp + 0x12ff), R.dx);
        d16w((uint16_t)(R.bp + 0x1305), 0);
        d16w((uint16_t)(R.bp + 0x1307), R.si);
    }
    R.di = d16(V(0x125c));
    R.dx = d16(V(0x1268));
    R.si = d16(V(0x1278));
    cmpb(d8(0x28bf), 2);
    if (d8(0x28bf) != 2) R.si = addw(R.si, 0x1e);
    else trail_sway(0x1292, 0x1294);
    trail_point();
    {
        uint16_t b = R.bx;
        R.bx = addw(R.bx, d16(V(0x1296)));
        d16w(V(0x1301), R.di);
        d16w(V(0x1303), R.dx);
        R.bx = b;
    }
    d16w(V(0x1296), addw(d16(V(0x1296)), 0xc));
    cmpw(d16(V(0x1296)), 0x60);
    if (d16(V(0x1296)) >= 0x60) d16w(V(0x1296), subw(d16(V(0x1296)), 0x54));
    d16w(V(0x12b2), 3);
out:
    R.ds = ds, R.di = di, R.si = si, R.dx = dx, R.cx = cx, R.bx = bx, R.ax = ax;
}

/* Car_StateSplash 1000:82be
   States 2 and 0Dh: the sequence at 193c:289d (frames 0Eh further) drawn at the saved position
   [12ba]/[12bc] (1000:8339); in state 0Dh the vehicle is drawn up to step 3, in state 2 from step 3.
   At the end state 0 (and [2911] = 0, the vehicle drawn) from state 2, state 7 from state 0Dh. The
   stuck counter [12aa] is cleared every frame. */
void Car_StateSplash(void)
{
    R.si = d16(V(0x12b8));
    R.si = (uint16_t)(R.si << 1);
    R.si = addw(R.si, 0x289d);
    R.ax = d16(R.si);
    R.si = addw(R.si, 0xe);
    R.cx = d16(R.si);
    R.si = R.cx;
    cmpw(R.si, 0xfffe);
    if (R.si != 0xfffe) {
        cmpw(R.si, 0xffff);
        if (R.si == 0xffff) {
            d16w(V(0x12b8), 0);
            d16w(V(0x12b0), 0);
            cmpw(d16(V(0x12ae)), 0xd);
            if (d16(V(0x12ae)) == 0xd) {
                d16w(V(0x12ae), 7);
            } else {
                d16w(V(0x12ae), 0);
                d16w(0x2911, 0);
                Car_Draw();
            }
            d16w(V(0x12aa), 0);
            return;
        }
        cmpw(d16(V(0x12ae)), 2);
        if (d16(V(0x12ae)) == 2) {
            cmpw(d16(V(0x12b8)), 3);
            if ((int16_t)d16(V(0x12b8)) >= 3) Car_Draw();
        } else {
            cmpw(d16(V(0x12b8)), 3);
            if ((int16_t)d16(V(0x12b8)) <= 3) Car_Draw();
        }
        Car_DrawSplashFrame();
    }
    cmpw(d16(V(0x12b0)), R.ax);
    if ((int16_t)d16(V(0x12b0)) >= (int16_t)R.ax) d16w(V(0x12b8), addw(d16(V(0x12b8)), 1));
    d16w(V(0x12aa), 0);
}

/* Car_DrawSplashFrame 1000:8339
   SI = frame: a 24 x 24 frame from 193c:45e3 + frame * 240h at the saved position [12ba]/[12bc],
   lifted by the height, plain. All registers kept. */
void Car_DrawSplashFrame(void)
{
    Cpu s = R;
    R.dx = d16(V(0x12d6));
    R.di = d16(V(0x12ba));
    R.ax = d16(V(0x12bc));
    R.di = subw(R.di, R.dx);
    R.ax = subw(R.ax, R.dx);
    R.di = on_screen(R.di, d16(0x264a), -0xc);
    R.ax = on_screen(R.ax, d16(0x264c), -0xc);
    R.ax = subw(R.ax, 0xc);
    R.di = subw(R.di, 0xc);
    R.bx = R.si;
    R.si = (uint16_t)(R.si << 9);
    R.bx = (uint16_t)(R.bx << 6);
    R.si = addw(R.si, R.bx);
    R.si = addw(R.si, 0x45e3);
    R.cx = 0x18;
    R.dx = R.cx;
    Gfx_ClipSprite();
    if (!R.cf) Gfx_BlitSprite();
    popa(&s);
}

/* Car_DrawRipples 1000:8386
   Vehicle BX's ripples: a ring of five 6-byte entries at [135d] (x, y, frame) with the cursor [1298].
   While the cursor is above 0 the live ones (frame != -1) are drawn as 32 x 32 frames (1000:847e) and
   aged when the timer [12b2] has run out (5 frames). On request [128c], when [12b4] has run out (then
   6), a new ripple at the vehicle's position: always into entry 0, while the cursor steps by 6 from 1Eh
   back to 6 (as the original: one ripple shows at a time, the others age out). AX BX CX DX SI DI DS
   kept, BP not. */
void Car_DrawRipples(void)
{
    uint16_t ax = R.ax, bx = R.bx, cx = R.cx, dx = R.dx, si = R.si, di = R.di, ds = R.ds;
    cmpw(d16(V(0x1298)), 0);
    if ((int16_t)d16(V(0x1298)) > 0) {
        R.dx = 0;
        for (;;) {
            R.bp = R.bx;
            R.bp = addw(R.bp, R.dx);
            R.di = d16((uint16_t)(R.bp + 0x135d));
            R.ax = d16((uint16_t)(R.bp + 0x135f));
            R.si = d16((uint16_t)(R.bp + 0x1361));
            cmpw(R.si, 0xffff);
            if (R.si != 0xffff) {
                R.di = on_screen(R.di, d16(0x264a), -0x20);
                R.ax = on_screen(R.ax, d16(0x264c), -0x20);
                Car_Draw32();
                cmpw(d16(V(0x12b2)), 0);
                if ((int16_t)d16(V(0x12b2)) <= 0) {
                    uint16_t f = incw(d16((uint16_t)(R.bp + 0x1361)));
                    d16w((uint16_t)(R.bp + 0x1361), f);
                    cmpw(f, 5);
                    if ((int16_t)f >= 5) d16w((uint16_t)(R.bp + 0x1361), 0xffff);
                }
            }
            R.dx = addw(R.dx, 6);
            cmpw(R.dx, 0x1e);
            if ((int16_t)R.dx >= 0x1e) break;
        }
    }
    cmpw(d16(V(0x128c)), 0);
    if (d16(V(0x128c)) != 0) {
        cmpw(d16(V(0x12b4)), 0);
        if ((int16_t)d16(V(0x12b4)) <= 0) {
            d16w(V(0x12b4), 6);
            d16w(V(0x128c), 0);
            R.di = d16(V(0x125c));
            R.dx = d16(V(0x1268));
            d16w(V(0x135d), R.di);
            d16w(V(0x135f), R.dx);
            d16w(V(0x1361), 0);
            d16w(V(0x1298), addw(d16(V(0x1298)), 6));
            cmpw(d16(V(0x1298)), 0x1e);
            if (d16(V(0x1298)) >= 0x1e) d16w(V(0x1298), subw(d16(V(0x1298)), 0x18));
            d16w(V(0x12b4), 6);
        }
    }
    R.ds = ds, R.di = di, R.si = si, R.dx = dx, R.cx = cx, R.bx = bx, R.ax = ax;
}

/* Car_DrawOverlay8 1000:843d
   Theme 8: a 32 x 32 frame over vehicle BX, one of four cycling with the counter [1392] / 2 (a rotor,
   inferred). The final test of the counter's low three bits branches to the next instruction (no
   effect, as the original). Leaves DI, AX, SI as computed. */
void Car_DrawOverlay8(void)
{
    R.di = d16(V(0x125c));
    R.di = subw(R.di, d16(0x264a));
    R.di = subw(R.di, 4);
    cmpw(R.di, 0xfff4);
    if ((int16_t)R.di <= -0xc) R.di = addw(R.di, 0xc00);
    R.ax = d16(V(0x1268));
    R.ax = subw(R.ax, d16(0x264c));
    R.ax = subw(R.ax, 4);
    cmpw(R.ax, 0xfff4);
    if ((int16_t)R.ax <= -0xc) R.ax = addw(R.ax, 0xc00);
    R.si = d16(V(0x1392));
    R.si >>= 1;
    R.si = andw(R.si, 3);
    Car_Draw32();
    d16w(V(0x1392), incw(d16(V(0x1392))));
    R.ax = d16(V(0x1392));
    R.ax = andw(R.ax, 7);
}

/* Car_Draw32 1000:847e
   SI = frame, DI/AX = the screen position: a 32 x 32 frame from 193c:5ee3 + frame * 400h, offset by
   0Ch up and left (not 10h: as the original), clipped, plain. All registers kept. */
void Car_Draw32(void)
{
    Cpu s = R;
    R.si = (uint16_t)(R.si << 10);
    R.si = addw(R.si, 0x5ee3);
    R.ax = subw(R.ax, 0xc);
    R.di = subw(R.di, 0xc);
    R.cx = 0x20;
    R.dx = R.cx;
    Gfx_ClipSprite();
    if (!R.cf) Gfx_BlitSprite();
    popa(&s);
}

/* Car_DrawShell 1000:8712
   Vehicle BX's shell: [1394] set, the timer [13a4] counting down. In flight (timer above 28h) the 8 x 8
   shell 193c:72e3 (frame [1396] / 4, the frame counter stopping at 5) at its position [1398]/[139c],
   and first its shadow in colour 0 at the position plus the height offset [139a]/[139e] (1000:8d36).
   From 28h down to 1Eh the burst 193c:41e3, its frame from the word table at cs:8780 (0 1 2 3 4 5 4 3 2
   1 0, read where the program keeps it); below 1Eh nothing. The timer runs out: [1394] = 0. */
void Car_DrawShell(void)
{
    cmpw(d16(V(0x1394)), 0);
    if (d16(V(0x1394)) == 0) goto tick;
    R.si = 0x72e3;
    R.cx = d16(V(0x1396));
    R.cx >>= 2;
    cmpw(d16(V(0x1396)), 5);
    if (d16(V(0x1396)) < 5) d16w(V(0x1396), incw(d16(V(0x1396))));
    cmpw(d16(V(0x13a4)), 0x28);
    if (d16(V(0x13a4)) > 0x28) {
        R.di = d16(V(0x1398));
        R.ax = d16(V(0x139c));
        Cpu s = R;
        R.di = addw(R.di, d16(V(0x139a)));
        R.ax = addw(R.ax, d16(V(0x139e)));
        R.di = on_screen(R.di, d16(0x264a), -8);
        R.ax = on_screen(R.ax, d16(0x264c), -8);
        R.si = 0x72e3;
        Gfx_DrawSmall8Black();
        popa(&s);
        R.di = on_screen(R.di, d16(0x264a), -8);
        R.ax = on_screen(R.ax, d16(0x264c), -8);
        R.si = 0x72e3;
        Gfx_DrawSmall8();
        goto tick;
    }
    cmpw(d16(V(0x13a4)), 0x1e);
    if (d16(V(0x13a4)) < 0x1e) goto tick;
    R.di = d16(V(0x1398));
    R.ax = d16(V(0x139c));
    R.si = 0x41e3;
    {
        uint16_t ax = R.ax, bx = R.bx;
        R.ax = d16(V(0x13a4));
        R.ax = subw(R.ax, 0x1e);
        R.ax = addw(R.ax, R.ax);
        R.bx = 0x8780;
        R.bx = addw(R.bx, R.ax);
        R.cx = c16(R.bx);
        R.bx = bx;
        R.ax = ax;
    }
    R.di = on_screen(R.di, d16(0x264a), -8);
    R.ax = on_screen(R.ax, d16(0x264c), -8);
    Gfx_DrawSmall8();
tick:
    d16w(V(0x13a4), decw(d16(V(0x13a4))));
    if (d16(V(0x13a4)) == 0) d16w(V(0x1394), 0);
}

/* Car_StateFall 1000:880a
   State 1 (fell off or sank, set by the terrain handlers; also a terrain handler itself in themes 8
   and 9): the speed [127a] = 0. Themes 4 and 9 first turn the vehicle by 4 a frame to face 0 or 80h,
   whichever is nearer, drawing it; then (as all other themes at once) with [12c2] set just the vehicle,
   else a sequence: facing 0 193c:27c1, facing 80h 27dd (both frames 0Eh further), other themes 27f9
   (frames 1Eh further; theme 2: 2835, 10h further), drawn with 1000:7fe8 (theme 9: 1000:8034). Effect
   7 at step 8, in themes 2, 4, 9 effect 11h at step 4; at the end state 7 (respawn). */
void Car_StateFall(void)
{
    d16w(V(0x127a), 0);
    cmpb(d8(0x28bf), 9);
    bool turn = d8(0x28bf) == 9;
    if (!turn) {
        cmpb(d8(0x28bf), 4);
        turn = d8(0x28bf) == 4;
    }
    if (turn) {
        R.ax = d16(V(0x1278));
        R.ax = andw(R.ax, 0xf8);
        if (R.ax == 0) goto face0;
        cmpw(R.ax, 0x80);
        if (R.ax == 0x80) goto face80;
        cmpw(R.ax, 0x40);
        if ((int16_t)R.ax < 0x40) goto left;
        cmpw(R.ax, 0xc0);
        if ((int16_t)R.ax > 0xc0) goto right;
        cmpw(R.ax, 0x80);
        if ((int16_t)R.ax > 0x80) goto left;
    right:
        d16w(V(0x1278), addw(d16(V(0x1278)), 4));
        goto draw;
    left:
        d16w(V(0x1278), subw(d16(V(0x1278)), 4));
        goto draw;
    face80:
        cmpw(d16(V(0x12c2)), 0);
        if (d16(V(0x12c2)) == 0) {
            R.si = d16(V(0x12b6));
            R.si = (uint16_t)(R.si << 1);
            R.si = addw(R.si, 0x27dd);
            R.ax = d16(R.si);
            R.si = addw(R.si, 0xe);
            goto seq;
        }
    face0:
        cmpw(d16(V(0x12c2)), 0);
        if (d16(V(0x12c2)) == 0) {
            R.si = d16(V(0x12b6));
            R.si = (uint16_t)(R.si << 1);
            R.si = addw(R.si, 0x27c1);
            R.ax = d16(R.si);
            R.si = addw(R.si, 0xe);
            goto seq;
        }
    }
    cmpw(d16(V(0x12c2)), 0);
    if (d16(V(0x12c2)) == 0) {
        R.si = d16(V(0x12b6));
        R.si = (uint16_t)(R.si << 1);
        cmpb(d8(0x28bf), 2);
        if (d8(0x28bf) == 2) {
            R.si = addw(R.si, 0x2835);
            R.ax = d16(R.si);
            R.si = addw(R.si, 0x10);
        } else {
            R.si = addw(R.si, 0x27f9);
            R.ax = d16(R.si);
            R.si = addw(R.si, 0x1e);
        }
        goto seq;
    }
draw:
    Car_Draw();
    return;
seq:
    R.cx = d16(R.si);
    R.si = R.cx;
    cmpw(R.si, 0xfffe);
    if (R.si != 0xfffe) {
        cmpw(R.si, 0xffff);
        if (R.si == 0xffff) {
            d16w(V(0x12b6), 0);
            d16w(V(0x12ae), 7);
            d16w(V(0x12b0), 0);
            return;
        }
        cmpb(d8(0x28bf), 9);
        if (d8(0x28bf) == 9) Car_DrawEffect40();
        else Car_DrawEffect24();
    }
    cmpw(d16(V(0x12b0)), R.ax);
    if ((int16_t)d16(V(0x12b0)) < (int16_t)R.ax) return;
    d16w(V(0x12b6), addw(d16(V(0x12b6)), 1));
    uint8_t th = d8(0x28bf);
    cmpb(th, 4);
    bool late = !(th == 4);
    if (late) { cmpb(th, 9); late = th != 9; }
    if (late) { cmpb(th, 2); late = th != 2; }
    if (late) {
        cmpw(d16(V(0x12b6)), 8);
        if (d16(V(0x12b6)) != 8) return;
        cmpw(d16(V(0x1250)), 0);
        if (d16(V(0x1250)) != 0) drv_ah_al(5, 7);
    } else {
        cmpw(d16(V(0x12b6)), 4);
        if (d16(V(0x12b6)) != 4) return;
        cmpw(d16(V(0x1250)), 0);
        if (d16(V(0x1250)) != 0) drv_ah_al(5, 0x11);
    }
}

/* Dead_HudDrawNumber 1000:8929
   Unreachable (no call, jump or stored address reaches it). AX as five decimal digits drawn right to
   left from DI - 8 (8 to the left each, 1000:905f), the sign glyph (digit 0Ah) at DI first if AX is
   negative. All registers kept. */
void Dead_HudDrawNumber(void)
{
    Cpu s = R;
    R.cf = false;
    flags_zs16(R.ax);
    if (R.ax & 0x8000) {
        R.ax = subw(0, R.ax);
        Cpu t = R;
        R.cx = 0xa;
        Hud_DrawDigit();
        popa(&t);
    }
    R.di = subw(R.di, 8);
    for (int i = 0; i < 5; i++) {
        R.dx = 0;
        R.cx = 0xa;
        uint16_t q = (uint16_t)(R.ax / R.cx), r = (uint16_t)(R.ax % R.cx);
        R.ax = q;
        R.dx = r;
        Cpu t = R;
        R.cx = R.dx;
        Hud_DrawDigit();
        popa(&t);
        R.di = subw(R.di, 8);
    }
    popa(&s);
}
