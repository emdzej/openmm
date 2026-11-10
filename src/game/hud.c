/* The race's HUD (docs/re/drawing.md, "HUD"): drawn into the race's back buffer each presented frame
   after the vehicles. Down the left edge, the race order: for each place a vehicle's icon in its team
   colour and the place's digit; at the top the laps left of player 1 (or, on the time-trial theme 9,
   the time left). Head to head shows the point bar instead: eight icons, the leader's count lit.
   The sprites are in the data segment's tail, loaded from BITSFILE.PH0 (Gfx_LoadBits). */
#include "cpu.h"
#include "gfx.h"
#include "mem.h"
#include "routines.h"

/* the routines address their variables through whatever DS holds (the data segment in the race) */
static uint16_t v16(uint16_t off) { return mw(R.ds, off); }
static uint8_t v8(uint16_t off) { return mb(R.ds, off); }
static void v16w(uint16_t off, uint16_t v) { mww(R.ds, off, v); }

/* the race order's ranking key of the vehicle at 193c:[2678 + si]: laps done * the lap length [2652]
   (8-bit multiply) plus the progress in the lap [12e3] */
static void rank_key(uint16_t si)
{
    R.bx = v16((uint16_t)(si + 0x2678));
    R.ax = (uint16_t)(9 - v16((uint16_t)(R.bx + 0x12ed)));
    R.ax = (uint16_t)(AL * CL);
    R.ax = (uint16_t)(R.ax + v16((uint16_t)(R.bx + 0x12e3)));
    v16w((uint16_t)(si + 0x2670), R.ax);
}

/* a bubble sort of the keys 193c:2670.. (descending, signed) with the vehicles 2678.., CX passes over
   `last` / 2 pairs */
static void rank_sort(uint16_t passes, uint16_t last)
{
    R.cx = passes;
    do {
        R.bx = 0;
        do {
            R.ax = v16((uint16_t)(R.bx + 0x2670));
            R.dx = v16((uint16_t)(R.bx + 0x2672));
            if ((int16_t)R.ax < (int16_t)R.dx) {
                v16w((uint16_t)(R.bx + 0x2670), R.dx);
                v16w((uint16_t)(R.bx + 0x2672), R.ax);
                R.ax = v16((uint16_t)(R.bx + 0x2678));
                R.dx = v16((uint16_t)(R.bx + 0x267a));
                v16w((uint16_t)(R.bx + 0x2678), R.dx);
                v16w((uint16_t)(R.bx + 0x267a), R.ax);
            }
            R.bx = (uint16_t)(R.bx + 2);
        } while (R.bx != last);
    } while (--R.cx);
}

/* a time digit of theme 9: AX / 10, the remainder drawn at DI; AX the quotient */
static void time_digit(uint16_t di)
{
    uint16_t q = (uint16_t)(R.ax / 10), r = (uint16_t)(R.ax % 10);
    R.ax = q;
    R.dx = r;
    R.cx = R.dx;
    R.di = di;
    Hud_DrawDigit();
    R.ax = q;
}

/* Hud_DrawPositions 1000:8dfc
   Theme 9 ([28bf] = 9): the time left [26c8] / 16 as four digits at the top right; after the units digit
   the sign glyph 10 is drawn when the shifted digit (units * 80h) is 5 or more, i.e. whenever the units
   are not 0 (as the original: the compare is made after Hud_DrawDigit has shifted CX). Head to head
   ([2656] = 2): the two players ranked (when both are out of laps and their progress differs by at
   least [2654], by progress alone), places 1 and 2 set, the leader's laps left at the top and the
   point bar down the left (eight icons, the bottom [26b4] of them lit). Else the four vehicles ranked by laps and
   progress (all four keys 7d00h as soon as one has no laps left or [26c6] >= 2: the order then stays),
   their places [12ef] set, player 1's laps left at the top and the four places' icons and digits.
   No registers kept. */
void Hud_DrawPositions(void)
{
    if (v8(0x28bf) == 9) {
        R.ax = (uint16_t)(v16(0x26c8) >> 4);
        uint16_t q = (uint16_t)(R.ax / 10), r = (uint16_t)(R.ax % 10);
        R.ax = q, R.dx = r;
        R.cx = R.dx;
        R.di = 0x19b0;
        Hud_DrawDigit();
        if ((int16_t)R.cx >= 5) {
            R.cx = 10;
            R.di = 0x19a8;
            Hud_DrawDigit();
        }
        R.ax = q;
        time_digit(0x19a0);
        time_digit(0x1998);
        return;
    }
    if (v16(0x2656) == 2) {
        R.cx = v16(0x2652);
        for (uint16_t si = 0; (int16_t)si <= 2; si = (uint16_t)(si + 2)) {
            R.si = si;
            rank_key(si);
        }
        R.si = 4;
        rank_sort(1, 2);
        if ((int16_t)v16(0x12ed) <= 0 && (int16_t)v16(0x1451) <= 0) {
            R.cx = (uint16_t)(v16(0x12e3) - v16(0x1447));
            if (R.cx & 0x8000) R.cx = (uint16_t)-R.cx;
            if ((int16_t)R.cx >= (int16_t)v16(0x2654)) {
                R.cx = v16(0x12e3);
                R.ax = v16(0x1447);
                if ((int16_t)R.ax > (int16_t)R.cx) {
                    v16w(0x2678, 0);
                    v16w(0x267a, 0x164);
                } else {
                    v16w(0x2678, 0x164);
                    v16w(0x267a, 0);
                }
            }
        }
        R.bx = v16(0x2678);
        v16w((uint16_t)(R.bx + 0x12ef), 1);
        R.di = 0x1118;
        R.cx = v16((uint16_t)(R.bx + 0x12ed));
        Hud_DrawDigit();
        R.bx = v16(0x267a);
        v16w((uint16_t)(R.bx + 0x12ef), 2);
        R.cx = 8;
        R.di = 0x2210;
        bool lit = false;
        do {   /* the unlit icons from the bottom of the bar until the lit count is reached */
            if (!lit && (int16_t)R.cx <= (int16_t)v16(0x26b4)) lit = true;
            uint16_t cx = R.cx, di = R.di;
            R.si = lit ? 0x59e3 : 0x5ae3;
            R.dx = 0x1010;
            Gfx_BlitAt();
            R.cx = cx, R.di = di;
            R.di = add16(R.di, 0x1100);
        } while (--R.cx);
        return;
    }
    R.cx = v16(0x2652);
    for (uint16_t si = 0; (int16_t)si <= 6; si = (uint16_t)(si + 2)) {
        R.si = si;
        R.bx = v16((uint16_t)(si + 0x2678));
        if ((int16_t)v16((uint16_t)(R.bx + 0x12ed)) > 0 && (int16_t)v16(0x26c6) < 2) {
            rank_key(si);
        } else {
            v16w(0x2670, 0x7d00);
            v16w(0x2672, 0x7d00);
            v16w(0x2674, 0x7d00);
            v16w(0x2676, 0x7d00);
        }
    }
    R.si = 8;
    rank_sort(3, 6);
    R.bx = v16(0x2660);
    R.di = 0x1118;
    R.cx = v16((uint16_t)(R.bx + 0x12ed));
    Hud_DrawDigit();
    for (uint16_t place = 1; place <= 4; place++) {
        R.bx = v16((uint16_t)(0x2678 + 2 * (place - 1)));
        v16w((uint16_t)(R.bx + 0x12ef), place);
        R.di = (uint16_t)(0x1110 + 0x1100 * place);
        Hud_DrawCarIcon();
        R.di = (uint16_t)(0x1120 + 0x1100 * place);
        R.cx = place;
        Hud_DrawDigit();
    }
}

/* Hud_DrawCarIcon 1000:903f
   BX = vehicle record offset, DI = buffer offset: its 16 x 16 icon (193c:5363; 5be3 once it has no laps
   left) with the team colour offset [bx+1252]. BX kept; the rest as Gfx_BlitRecolourAt leaves them.
   (The compare of DS with the data segment at the start has no effect.) */
void Hud_DrawCarIcon(void)
{
    uint16_t bx = R.bx;
    R.si = v16((uint16_t)(R.bx + 0x12ed)) == 0 ? 0x5be3 : 0x5363;
    R.dx = 0x1010;
    R.bx = v16((uint16_t)(R.bx + 0x1252));
    Gfx_BlitRecolourAt();
    R.bx = bx;
}

/* Hud_DrawDigit 1000:905f
   CX = digit (0Ah the sign), DI = buffer offset: an 8 x 16 glyph from 193c:5463 + digit * 80h. Returns
   CX = digit * 80h, SI the glyph, DX = 1008h; the rest kept. (The compare of DS has no effect.) */
void Hud_DrawDigit(void)
{
    R.cx = (uint16_t)(R.cx << 7);
    R.si = (uint16_t)(0x5463 + R.cx);
    R.dx = 0x1008;
    Gfx_BlitAt();
}

/* Hud_DrawCarPosition 1000:9076
   BX = vehicle: its race place [bx+12ef] as a 16 x 8 label (193c:5ce3 + (place - 1) * 80h) over the
   vehicle (8 left of and 20 above its position, lifted by its height [bx+12d6]), relative to the view
   [264a], [264c] and brought back across the world's wrap (C00h) when it lands 12 or more left of or
   above the window. Clipped. Registers kept (flags from the draw). */
void Hud_DrawCarPosition(void)
{
    Cpu saved = R;
    R.dx = (uint16_t)((uint16_t)(mw(R.ds, (uint16_t)(R.bx + 0x12ef)) - 1) << 7);
    R.si = (uint16_t)(0x5ce3 + R.dx);
    uint16_t lift = mw(R.ds, (uint16_t)(R.bx + 0x12d6));
    R.di = (uint16_t)(mw(R.ds, (uint16_t)(R.bx + 0x125c)) - lift - 8 - mw(R.ds, 0x264a));
    if ((int16_t)R.di <= -0x0c) R.di = (uint16_t)(R.di + 0xc00);
    R.ax = (uint16_t)(mw(R.ds, (uint16_t)(R.bx + 0x1268)) - lift - 0x14 - mw(R.ds, 0x264c));
    if ((int16_t)R.ax <= -0x0c) R.ax = (uint16_t)(R.ax + 0xc00);
    R.cx = 0x10;
    R.dx = 8;
    Gfx_ClipSprite();
    if (!R.cf) Gfx_BlitSprite();
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = saved;
    R.cf = cf, R.zf = zf, R.sf = sf;
}
