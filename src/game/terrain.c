/* The terrain (docs/re/terrain.md): what the surface under a vehicle does to it, and the lap count.
   Once per game step Car_Physics hands each vehicle (BX = its record offset from 193c:124c, stride
   164h) to Terrain_Dispatch, which takes the terrain class from the surface byte Track_SurfaceAt left in
   [bx+12da], runs that class's handler from the theme's table (the word at 193c:28bd, set by the race
   set-up from 193c:26d7), then the checkpoint and lap logic, and on the bathtub (theme 2) the plughole.

   All routines run with DS = 193c (the data segment) and use whatever DS holds: the port reads R.ds.
   Register-exact: every ret leaves R as the original's registers, CF included (docs/re/porting.md). */
#include "cpu.h"
#include "fntab.h"
#include "machine.h"
#include "mem.h"
#include "pending.h"
#include "routines.h"

/* ---- memory: the data segment through DS, a vehicle's fields through DS:BX ---- */
static uint8_t db(uint16_t off) { return mb(R.ds, off); }
static uint16_t dw(uint16_t off) { return mw(R.ds, off); }
static void dww(uint16_t off, uint16_t v) { mww(R.ds, off, v); }
static uint16_t vw(uint16_t f) { return mw(R.ds, (uint16_t)(R.bx + f)); }
static int16_t vs(uint16_t f) { return (int16_t)vw(f); }
static void vww(uint16_t f, uint16_t v) { mww(R.ds, (uint16_t)(R.bx + f), v); }
static uint8_t theme(void) { return db(0x28bf); }   /* 193c:28bf the race's theme, 1..9 */

/* ---- flags as the instructions leave them (only CF, ZF and SF are kept) ---- */
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
static uint16_t f_logic16(uint16_t r)   /* and, or, xor, test */
{
    R.cf = false;
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

/* The speed squared, as every handler computes it: the high bytes (whole pixels) of the velocities
   [1272] and [1276], each squared with a signed byte multiply, summed. AX = the sum, DX = the x square,
   CL = the y velocity's high byte (CH kept); flags as the add leaves them. */
static void speed_sq(void)
{
    int8_t x = (int8_t)hi8(vw(0x1272));
    R.dx = (uint16_t)(x * x);
    int8_t y = (int8_t)hi8(vw(0x1276));
    SET_CL((uint8_t)y);
    R.ax = f_add16((uint16_t)(y * y), R.dx);
}

/* mov dx, 0 / mov cx, c / div cx (the sum is at most 8000h: no overflow). The flags div leaves are
   undefined; every use is followed by an instruction that sets them. */
static void div_by(uint16_t c)
{
    uint16_t n = R.ax;
    R.cx = c;
    R.ax = n / c;
    R.dx = n % c;
}

/* The jump made when leaving a raised level ([138e] set): the level is cleared and the vertical speed
   [12d4] = speed^2 / div + add. Shared by most handlers' first lines. */
static void jump_off_level(uint16_t div, uint16_t add)
{
    f_cmp16(vw(0x138e), 0);
    if (vw(0x138e) == 0) return;
    vww(0x138e, 0);
    speed_sq();
    div_by(div);
    R.ax = f_add16(R.ax, add);
    vww(0x12d4, R.ax);
}

/* A sound effect: call far 2424:0000 with AH = 5, AL = n, only for a vehicle drawn this frame ([1250],
   the players' cars on screen). */
static void effect_if_visible(uint8_t n)
{
    f_cmp16(vw(0x1250), 0);
    if (vw(0x1250) == 0) return;
    SET_AH(5);
    SET_AL(n);
    mach_drv();
}

/* The lap list of this theme and track: the word at 193c:1feb + (theme - 1) * 8 + (track - 1) * 2
   points to pairs of checkpoint numbers (low byte from, high byte to: one word), ending with ffffh.
   SI on return: the list's address; AX as the original leaves it. */
static uint16_t lap_list(void)
{
    uint16_t si = 0x1feb;
    uint8_t al = (uint8_t)(theme() - 1);
    R.ax = (uint16_t)al;                      /* xor ah, ah */
    R.cf = (R.ax >> 13) & 1;                  /* shl ax, 3 */
    R.ax = (uint16_t)(R.ax << 3);
    si = f_add16(si, R.ax);
    SET_AL((uint8_t)(db(0x28c0) - 1));        /* the track, 1..4 */
    R.cf = (AL >> 7) & 1;                     /* shl al, 1 */
    SET_AL((uint8_t)(AL << 1));
    si = f_add16(si, R.ax);
    return dw(si);
}

/* Vehicle BX after its move (Car_Physics 1000:5be7):
   - Head to head ([2656] = 2): both cars sinking (state 1), or both in a hole (state 5), ends the
     race ([2913] = 1) at once.
   - In state 0 (driving): the terrain class from the surface byte [12da] (>> 5 on themes 1, 2, 3 and 6,
     >> 4 on the others; the high byte stays in AH as the original) becomes [12d0], the previous one
     [12d2]; on the ground ([12d6] = 0), or on themes 4 and 5 for classes 5 and up (their stepped
     levels apply in the air too), the theme's handler for the class runs: the word at [28bd] + 2 x
     class, called with AX = 2 x class and SI pointing at the entry. On the bathtub, a surface with
     flags 18h is also a current (Terrain_Current).
   - Theme 3 on the lower layer ([1388] != 1) over a tile marked in 193c:25cc (a bridge's upper
     deck): no checkpoint logic.
   - Checkpoints ([12e5] set when the number under the car changed, Car_MoveProbe): the move from the
     previous checkpoint [12e1] to the new one [12e3] against the most a lap allows ([2654]):
       * within it, forward or back: the lap list entry at [12e7] (a range of checkpoints, low byte
         first, high byte last) is passed when the car's old checkpoint is in it at its end (the
         cursor moves on); if the old checkpoint is before the range's start nothing happens; inside
         the range: a short cut, see below;
       * a jump back by more (the numbers wrap at the line): at the list's end, with the new
         checkpoint before the last range's end, a lap is done ([12ed] laps to go, one less, never
         below 0; the lap sound for a player; the cursor back to [12e9]); otherwise a short cut;
       * a jump forward by more (backwards over the line): the cursor goes to the list's last entry,
         the old one saved in [12e9], one more lap to go (at most 9).
     A short cut (a checkpoint range entered from the wrong place): in a four-car race the vehicle is
     taken off ([12ae] = 0dh, the splash at its place [12ba]/[12bc], sound 1 for a player) and put back
     at the safe point before the last ([12f5]/[12f7] -> [12f1]/[12f3]); in head to head the cursor
     just moves on.
     On the first bathtub track, the first player's car on its last two laps ahead of the other three
     sets [26c6] = 2 (inferred: a cue for the music).
   - On the bathtub, every vehicle and state: the plughole (Terrain_Plughole). */
/* Terrain_Dispatch 1000:5e4e */
void Terrain_Dispatch(void)
{
    if (dw(0x2656) == 2) {
        f_cmp16(dw(0x2656), 2);
        R.si = dw(0x2660);
        f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 1);
        if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) == 1) {
            R.si = dw(0x2662);
            f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 1);
            if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) == 1) {
                dww(0x2913, 1);
                return;
            }
        }
        R.si = dw(0x2660);
        f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 5);
        if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) == 5) {
            R.si = dw(0x2662);
            f_cmp16(mw(R.ds, (uint16_t)(R.si + 0x12ae)), 5);
            if (mw(R.ds, (uint16_t)(R.si + 0x12ae)) == 5) {
                dww(0x2913, 1);
                return;
            }
        }
    } else {
        f_cmp16(dw(0x2656), 2);
    }

    f_cmp16(vw(0x12ae), 0);
    if (vw(0x12ae) != 0) goto plughole;

    /* the class */
    R.ax = vw(0x12da);
    {
        int8_t t = (int8_t)theme();
        int sh = (t <= 3 || t == 6) ? 5 : 4;   /* signed byte compares, as the original */
        R.cf = (AL >> (sh - 1)) & 1;
        SET_AL((uint8_t)(AL >> sh));
        flags_zs8(AL);
    }
    R.cx = vw(0x12d0);
    vww(0x12d2, R.cx);
    vww(0x12d0, R.ax);
    f_cmp16(vw(0x12d0), 5);
    bool run = false;
    if ((int16_t)vw(0x12d0) >= 5) {
        f_cmp8(theme(), 4);
        if (theme() == 4) run = true;
        else {
            f_cmp8(theme(), 5);
            if (theme() == 5) run = true;
        }
    }
    if (!run) {
        f_cmp16(vw(0x12d6), 0);
        run = vw(0x12d6) == 0;
    }
    if (run) {
        R.si = dw(0x28bd);
        R.cf = (R.ax >> 15) & 1;
        R.ax = (uint16_t)(R.ax << 1);
        R.si = f_add16(R.si, R.ax);
        uint16_t target = dw(R.si);
        const FnEntry *e = fntab_find(0x10000000u | target);
        if (e) e->fn();
        else PENDING_NOTE(0x10000000u | target, "a terrain handler not in names.tsv");
    }

    f_cmp8(theme(), 2);
    if (theme() == 2) {
        f_logic16(vw(0x12da) & 0x18);
        if (vw(0x12da) & 0x18) Terrain_Current();
    }

    f_cmp8(theme(), 3);
    if (theme() == 3) {
        f_cmp16(vw(0x1388), 1);
        if (vw(0x1388) != 1) {
            R.si = f_add16(0x25cc, vw(0x12cc));
            SET_AL(db(R.si));
            R.si++;
            f_cmp8(AL, 1);
            if (AL == 1) goto plughole;
        }
    }

    f_cmp16(vw(0x12e5), 0);
    if (vw(0x12e5) == 0) goto plughole;

    R.ax = f_sub16(vw(0x12e3), vw(0x12e1));
    R.cx = dw(0x2654);
    f_cmp16(R.ax, R.cx);
    if ((int16_t)R.ax > (int16_t)R.cx) goto backwards_over_line;
    R.cf = R.cx != 0;                        /* neg cx */
    R.cx = (uint16_t)-R.cx;
    f_cmp16(R.ax, R.cx);
    if ((int16_t)R.ax < (int16_t)R.cx) goto over_line;

    /* within reach: the list entry at the cursor */
    R.si = lap_list();
    R.si = f_add16(R.si, vw(0x12e7));
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    f_cmp16(R.ax, 0xffff);
    if (R.ax == 0xffff) goto plughole;
    R.cx = AH;                               /* mov cl, ah / xor ch, ch */
    R.ax = f_logic16(AL);                    /* xor ah, ah */
    f_cmp16(vw(0x12e1), R.ax);
    if (vs(0x12e1) < (int16_t)R.ax) goto plughole;
    f_cmp16(vw(0x12e1), R.cx);
    if (vs(0x12e1) >= (int16_t)R.cx) goto short_cut;
    vww(0x12e7, f_add16(vw(0x12e7), 2));
    goto plughole;

short_cut:
    f_cmp16(dw(0x2656), 2);
    if (dw(0x2656) == 2) {
        vww(0x12e7, (uint16_t)(vw(0x12e7) + 2));   /* two inc: CF kept */
        goto plughole;
    }
    vww(0x12f1, vw(0x12f5));
    R.ax = vw(0x12f5);
    vww(0x12f3, vw(0x12f7));
    vww(0x12ba, vw(0x125c));
    vww(0x12bc, vw(0x1268));
    R.ax = vw(0x1268);
    vww(0x12ae, 0x0d);
    effect_if_visible(1);
    goto plughole;

over_line:
    R.si = lap_list();
    R.si = f_add16(R.si, vw(0x12e7));
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    f_cmp16(R.ax, 0xffff);
    if (R.ax != 0xffff) goto short_cut;
    R.si = f_sub16(R.si, vw(0x12e7));
    R.si = f_sub16(R.si, 2);
    R.ax = dw(R.si);
    R.si = (uint16_t)(R.si + 2);
    R.ax = f_logic16(AH);                    /* mov al, ah / xor ah, ah */
    f_cmp16(vw(0x12e3), R.ax);
    if (vs(0x12e3) >= (int16_t)R.ax) goto short_cut;
    /* a lap */
    vww(0x12ed, (uint16_t)(vw(0x12ed) - 1));
    flags_zs16(vw(0x12ed));
    f_cmp16(dw(0x2656), 1);
    if (dw(0x2656) == 1) {
        effect_if_visible(2);
        f_cmp8(theme(), 2);
        if (theme() == 2) {
            f_cmp8(db(0x28c0), 1);
            if (db(0x28c0) == 1) {
                f_cmp16(vw(0x12ed), 2);
                if (vs(0x12ed) <= 2) {
                    f_cmp16(R.bx, 0);
                    if (R.bx == 0) {
                        R.ax = vw(0x12ed);
                        f_cmp16(dw(0x15b5), R.ax);
                        if ((int16_t)dw(0x15b5) > (int16_t)R.ax) {
                            f_cmp16(dw(0x1451), R.ax);
                            if ((int16_t)dw(0x1451) > (int16_t)R.ax) {
                                f_cmp16(dw(0x1719), R.ax);
                                if ((int16_t)dw(0x1719) > (int16_t)R.ax) dww(0x26c6, 2);
                            }
                        }
                    }
                }
            }
        }
    }
    R.ax = f_logic16(vw(0x12ed));
    if (R.ax & 0x8000) vww(0x12ed, 0);
    R.ax = vw(0x12e9);
    vww(0x12e7, R.ax);
    vww(0x12e9, 0);
    goto plughole;

backwards_over_line:
    R.si = lap_list();
    R.cx = R.si;
    R.si = f_add16(R.si, vw(0x12e7));
    do {
        R.ax = dw(R.si);
        R.si = (uint16_t)(R.si + 2);
        f_cmp16(R.ax, 0xffff);
    } while (R.ax != 0xffff);
    R.si = f_sub16(R.si, 2);
    R.si = f_sub16(R.si, R.cx);
    R.ax = vw(0x12e7);
    vww(0x12e9, R.ax);
    vww(0x12e7, R.si);
    vww(0x12ed, (uint16_t)(vw(0x12ed) + 1));
    f_cmp16(vw(0x12ed), 9);
    if (vs(0x12ed) > 9) vww(0x12ed, 9);

plughole:
    f_cmp8(theme(), 2);
    if (theme() == 2) Terrain_Plughole();
}

/* Terrain_Floor 1000:60cb
   Class 0 on every theme (and 2.7, 3.0, 4.15, 5.13, 6.5, 8.3): plain ground. The boost stage [1390]
   ends; leaving a raised level jumps (speed^2 / 15 + 4). On theme 1, coming off class 4 jumps
   speed^2 / 7 + 4 and off class 3 speed^2 / 12 + 8 (both + 4, as the original). */
void Terrain_Floor(void)
{
    vww(0x1390, 0);
    jump_off_level(15, 4);
    f_cmp8(theme(), 1);
    if (theme() != 1) return;
    f_cmp16(vw(0x12d2), 4);
    if (vw(0x12d2) == 4) {
        speed_sq();
        div_by(7);
    } else {
        f_cmp16(vw(0x12d2), 3);
        if (vw(0x12d2) != 3) return;
        speed_sq();
        div_by(12);
        R.ax = f_add16(R.ax, 4);
    }
    R.ax = f_add16(R.ax, 4);
    vww(0x12d4, R.ax);
}

/* Terrain_FallOff 1000:6169
   Theme 8 class 1: off the edge of the table. The vehicle's place is kept for the splash ([12ba],
   [12bc]), state 0dh, the timer [1382] = 46h (70 steps). */
void Terrain_FallOff(void)
{
    vww(0x1390, 0);
    R.ax = vw(0x125c);
    vww(0x12ba, R.ax);
    R.ax = vw(0x1268);
    vww(0x12bc, R.ax);
    vww(0x12ae, 0x0d);
    vww(0x1382, 0x46);
}

/* Terrain_Boost 1000:618c
   Theme 8 class 2: in two steps: the first contact sets [1390] = 1, the next [1390] = 2 and, if the
   vehicle is not moving left ([1272] >= 0), adds 0c8h to its x velocity. */
void Terrain_Boost(void)
{
    f_cmp16(vw(0x1390), 1);
    if (vw(0x1390) != 1) {
        vww(0x1390, 1);
        return;
    }
    vww(0x1390, 2);
    R.ax = f_logic16(vw(0x1272));
    if (R.ax & 0x8000) return;
    vww(0x1272, f_add16(vw(0x1272), 0xc8));
}

/* Terrain_Rough 1000:61b0
   Theme 5 class 3, theme 6 class 4. Leaving a raised level jumps (/15 + 4) and nothing else.
   Otherwise, entered from another class (class 3 on theme 5, class 4 elsewhere counts as already on
   it): both velocities halved and sound 6 for a player. Then the speed is held to at most 200h, and
   at the limit the driver is asked about effect 6 (AH = 0Ah) and the answer ignored (a compare with
   a jump to the next instruction, as the original). */
void Terrain_Rough(void)
{
    f_cmp16(vw(0x138e), 0);
    if (vw(0x138e) != 0) {
        jump_off_level(15, 4);
        return;
    }
    bool same;
    f_cmp8(theme(), 5);
    if (theme() == 5) {
        f_cmp16(vw(0x12d2), 3);
        same = vw(0x12d2) == 3;
    } else {
        f_cmp16(vw(0x12d2), 4);
        same = vw(0x12d2) == 4;
    }
    if (!same) {
        vww(0x1272, f_sar16(vw(0x1272), 1));
        vww(0x1276, f_sar16(vw(0x1276), 1));
        effect_if_visible(6);
    }
    f_cmp16(vw(0x127a), 0x200);
    if (vs(0x127a) < 0x200) return;
    vww(0x127a, 0x200);
    SET_AH(0x0a);
    SET_AL(6);
    mach_drv();
    f_cmp8(AL, 0);
}

/* Terrain_Current 1000:6231
   Themes 8 (classes 6-8) and 9 (3-5), and the bathtub's surfaces with flags 18h (from the dispatch):
   a flow pushes the vehicle. The direction: the surface's low bits (& 7 on theme 2, & 0fh) index the
   word table 193c:18fb; on a tile with orientation flag 2 ([12de]) the index is first remapped through
   the 16-byte row (tile byte [12e0] bits 5-6) of 193c:191b; orientation flag 1 turns it half round
   (xor 80h). The push on each axis is twice the sine (193c:10a0, cosine 40h on) of that direction,
   held between [12a0] and the maximum speed [129c], added to the velocities [1272] and [1276]; the
   speed [127a] is held to at most 100h. */
void Terrain_Current(void)
{
    R.ax = vw(0x12da);
    R.dx = vw(0x12de);
    SET_CL(mb(R.ds, (uint16_t)(R.bx + 0x12e0)));
    SET_CH(0);
    R.cf = false;
    f_cmp8(theme(), 2);
    if (theme() == 2) R.ax = f_logic16(R.ax & 7);
    R.ax = f_logic16(R.ax & 0x0f);
    f_logic16(R.dx & 2);
    if (R.dx & 2) {
        R.cf = (R.cx >> 4) & 1;
        R.cx = (uint16_t)(R.cx >> 5);
        R.cx = f_logic16(R.cx & 3);
        R.si = R.cx;
        R.cf = (R.si >> 12) & 1;
        R.si = (uint16_t)(R.si << 4);
        R.si = f_add16(R.si, 0x191b);
        R.si = f_add16(R.si, R.ax);
        SET_AL(db(R.si));
        R.si++;
    }
    R.si = R.ax;
    R.cf = (R.si >> 15) & 1;
    R.si = (uint16_t)(R.si << 1);
    R.si = f_add16(R.si, 0x18fb);
    R.ax = dw(R.si);
    f_logic16(R.dx & 1);
    if (R.dx & 1) R.ax = f_logic16(R.ax ^ 0x80);
    R.si = R.ax;
    R.si = f_add16(R.si, 0x10a0);
    for (int axis = 0; axis < 2; axis++) {
        if (axis == 1) {
            R.si = f_sub16(R.si, 0x41);
            f_cmp16(R.si, 0x109f);
            if ((int16_t)R.si <= 0x109f) R.si = f_add16(R.si, 0x100);
        }
        SET_AL(db(R.si));
        R.si++;
        R.ax = (uint16_t)(int16_t)(int8_t)AL;   /* cbw */
        R.cx = 0x100;
        int32_t p = (int16_t)R.ax * 0x100;      /* imul cx: DX:AX */
        R.dx = (uint16_t)((uint32_t)p >> 16);
        R.ax = (uint16_t)p;
        SET_AL(AH);
        SET_AH(DL);
        R.cf = (R.ax >> 15) & 1;
        R.ax = (uint16_t)(R.ax << 1);
        f_cmp16(R.ax, vw(0x129c));
        if ((int16_t)R.ax > vs(0x129c)) R.ax = vw(0x129c);
        f_cmp16(R.ax, vw(0x12a0));
        if ((int16_t)R.ax < vs(0x12a0)) R.ax = vw(0x12a0);
        uint16_t f = axis == 0 ? 0x1272 : 0x1276;
        vww(f, f_add16(vw(f), R.ax));
    }
    f_cmp16(vw(0x127a), 0x100);
    if (vs(0x127a) >= 0x100) vww(0x127a, 0x100);
}

/* Terrain_Plughole 1000:62e3
   The bathtub's plughole, for a driving vehicle (state 0) inside the box 614h-68ch x b34h-bach around
   its centre (650h, b70h): the vehicle is pulled toward the centre on both axes by the smaller of
   (3ch - |dx|) * 4 + 28h and (3ch - |dy|) * 4 + 5 (harder near it). Within 0ch of the centre it goes
   down the drain: state 1 (sinking, [1382] = 46h), the slide target the centre, four slide steps of a
   quarter of the way ([12be], [12c0], [12c2] = 4). */
void Terrain_Plughole(void)
{
    f_cmp16(vw(0x12ae), 0);
    if (vw(0x12ae) != 0) return;
    f_cmp16(vw(0x125c), 0x614);
    if (vs(0x125c) < 0x614) return;
    f_cmp16(vw(0x125c), 0x68c);
    if (vs(0x125c) > 0x68c) return;
    f_cmp16(vw(0x1268), 0xb34);
    if (vs(0x1268) < 0xb34) return;
    f_cmp16(vw(0x1268), 0xbac);
    if (vs(0x1268) > 0xbac) return;

    R.ax = f_sub16(vw(0x125c), 0x650);
    R.si = R.ax;
    f_cmp16(R.si, 0xffff);
    if ((int16_t)R.si <= -1) {
        R.cf = R.si != 0;
        R.si = (uint16_t)-R.si;
    }
    R.cx = f_sub16(0x3c, R.si);
    R.cx = f_add16(R.cx, R.cx);
    R.cx = f_add16(R.cx, R.cx);
    R.cx = f_add16(R.cx, 0x28);
    uint16_t pull_x = R.cx;
    R.ax = f_sub16(vw(0x1268), 0xb70);
    R.si = R.ax;
    f_cmp16(R.si, 0xffff);
    if ((int16_t)R.si <= -1) {
        R.cf = R.si != 0;
        R.si = (uint16_t)-R.si;
    }
    R.cx = f_sub16(0x3c, R.si);
    R.cx = f_add16(R.cx, R.cx);
    R.cx = f_add16(R.cx, R.cx);
    R.cx = f_add16(R.cx, 5);
    R.dx = pull_x;
    f_cmp16(R.dx, R.cx);
    if ((int16_t)R.dx <= (int16_t)R.cx) R.cx = R.dx;
    uint16_t pull = R.cx;

    R.ax = f_sub16(vw(0x125c), 0x650);
    if (!R.cf) {
        R.cf = R.cx != 0;
        R.cx = (uint16_t)-R.cx;
    }
    vww(0x1272, f_add16(vw(0x1272), R.cx));
    R.cx = pull;
    R.ax = f_sub16(vw(0x1268), 0xb70);
    if (!R.cf) {
        R.cf = R.cx != 0;
        R.cx = (uint16_t)-R.cx;
    }
    vww(0x1276, f_add16(vw(0x1276), R.cx));
    R.cx = pull;

    f_cmp16(vw(0x125c), 0x644);
    if (vs(0x125c) < 0x644) return;
    f_cmp16(vw(0x125c), 0x65c);
    if (vs(0x125c) > 0x65c) return;
    f_cmp16(vw(0x1268), 0xb64);
    if (vs(0x1268) < 0xb64) return;
    f_cmp16(vw(0x1268), 0xb7c);
    if (vs(0x1268) > 0xb7c) return;
    vww(0x1382, 0x46);
    vww(0x12ae, 1);
    vww(0x125e, 0x650);
    vww(0x126a, 0xb70);
    vww(0x12c2, 4);
    R.ax = f_sub16(0x650, vw(0x125c));
    R.ax = f_sar16(R.ax, 2);
    vww(0x12be, R.ax);
    R.ax = f_sub16(0xb70, vw(0x1268));
    R.ax = f_sar16(R.ax, 2);
    vww(0x12c0, R.ax);
}

/* Terrain_Splash 1000:63d6
   Theme 2 class 1: asks for the splash effect ([128a] = 1, drawn by the vehicle's effects). */
void Terrain_Splash(void)
{
    vww(0x128a, 1);
}

/* Terrain_Ramp 1000:63dd
   Theme 2 class 6: on the ground, a jump of max(speed^2, 1ch) / 8 + 5, [12d8] = 0. */
void Terrain_Ramp(void)
{
    f_cmp16(vw(0x12d6), 0);
    if (vw(0x12d6) != 0) return;
    speed_sq();
    f_cmp16(R.ax, 0x1c);
    if ((int16_t)R.ax < 0x1c) R.ax = 0x1c;
    div_by(8);
    R.ax = f_add16(R.ax, 5);
    vww(0x12d4, R.ax);
    vww(0x12d8, 0);
}

/* Terrain_Bump 1000:641a
   Theme 2 class 3: unless coming from class 6 (the ramp), a speed^2 of at least 0eh jumps
   max(speed^2, 1ch) / 10 + 5. */
void Terrain_Bump(void)
{
    f_cmp16(vw(0x12d2), 6);
    if (vw(0x12d2) == 6) return;
    speed_sq();
    f_cmp16(R.ax, 0x0e);
    if ((int16_t)R.ax < 0x0e) return;
    f_cmp16(R.ax, 0x1c);
    if ((int16_t)R.ax < 0x1c) R.ax = 0x1c;
    div_by(10);
    R.ax = f_add16(R.ax, 5);
    vww(0x12d4, R.ax);
}

/* The slide into a hole's centre (Terrain_Hole, Terrain_HoleKeepSpeed): state 1, the animation
   counter 0, the target [125e]/[126a] the centre of the 16-pixel cell, the step a quarter of the way
   on each axis (the shorter way round the 0c00h world), four steps ([12c2] = 4), the skid and grip
   timers cleared. */
static uint16_t to_centre(uint16_t target, uint16_t now)
{
    vww(target, f_logic16(vw(target) & 0xfff0));
    vww(target, f_add16(vw(target), 8));
    R.ax = f_sub16(vw(target), vw(now));
    f_cmp16(R.ax, 0xbe0);
    if ((int16_t)R.ax >= 0xbe0) R.ax = f_sub16(R.ax, 0xc00);
    f_cmp16(R.ax, 0xf420);
    if ((int16_t)R.ax <= (int16_t)0xf420) R.ax = f_add16(R.ax, 0xc00);
    return f_sar16(R.ax, 2);
}

static void fall_into_hole(void)
{
    vww(0x12ae, 1);
    vww(0x12b0, 0);
    uint16_t sx = to_centre(0x125e, 0x125c);
    R.ax = sx;
    vww(0x12be, sx);
    uint16_t sy = to_centre(0x126a, 0x1268);
    R.ax = sy;
    vww(0x12c0, sy);
    vww(0x12c2, 4);
    vww(0x1282, 0);
    vww(0x1284, 0);
    vww(0x1286, 0);
    vww(0x1288, 0);
}

/* Terrain_Hole 1000:6456
   Theme 1 class 1, theme 3 class 2, theme 5 class 4: a vehicle that can fall ([12f9]) falls in (the
   slide above) and stops dead (wanted and actual velocities 0). */
void Terrain_Hole(void)
{
    f_cmp16(vw(0x12f9), 0);
    if (vw(0x12f9) == 0) return;
    fall_into_hole();
    vww(0x1270, 0);
    vww(0x1274, 0);
    vww(0x1272, 0);
    vww(0x1276, 0);
}

/* Terrain_HoleKeepSpeed 1000:64f5
   Theme 4 class 3, theme 8 class 10, theme 9 class 7: as Terrain_Hole, the velocities left alone. */
void Terrain_HoleKeepSpeed(void)
{
    f_cmp16(vw(0x12f9), 0);
    if (vw(0x12f9) == 0) return;
    fall_into_hole();
}

/* Terrain_FloorSkid 1000:657e
   Theme 1 class 2, 3.1, 4.1, 5.14, 6.1: ground that leaves skid marks. Leaving a raised level jumps
   (/15 + 4). On theme 1, coming off class 4 jumps speed^2 / 7 + 4 and off class 3 speed^2 / 12 + 4
   (one + 4 here, two in Terrain_Floor); otherwise a skidding vehicle ([1282]) asks for a mark ([1288]). */
void Terrain_FloorSkid(void)
{
    jump_off_level(15, 4);
    f_cmp8(theme(), 1);
    if (theme() == 1) {
        f_cmp16(vw(0x12d2), 4);
        bool jump = true;
        if (vw(0x12d2) == 4) {
            speed_sq();
            div_by(7);
        } else {
            f_cmp16(vw(0x12d2), 3);
            if (vw(0x12d2) == 3) {
                speed_sq();
                div_by(12);
            } else {
                jump = false;
            }
        }
        if (jump) {
            R.ax = f_add16(R.ax, 4);
            vww(0x12d4, R.ax);
            return;
        }
    }
    f_cmp16(vw(0x1282), 0);
    if (vw(0x1282) != 0) vww(0x1288, 1);
}

/* Terrain_Splashy 1000:6622
   Theme 4 class 2, 5.2, 8.9, 9.6: a wet surface. Leaving a raised level jumps (/15 + 4); a moving
   vehicle ([127a] != 0) asks for the splash ([128a] = 1) and, for a player, sound 12h. */
void Terrain_Splashy(void)
{
    jump_off_level(15, 4);
    f_cmp16(vw(0x127a), 0);
    if (vw(0x127a) == 0) return;
    vww(0x128a, 1);
    effect_if_visible(0x12);
}

/* Terrain_Wall 1000:6674
   Theme 1 classes 3 and 4: entered from class 0 or 2 (the floor) it counts as class 0 and the vehicle
   bounces back on both axes ([12a8], and blocked x- [12c4], y- [12c8]). */
void Terrain_Wall(void)
{
    f_cmp16(vw(0x12d2), 0);
    if (vw(0x12d2) != 0) {
        f_cmp16(vw(0x12d2), 2);
        if (vw(0x12d2) != 2) return;
    }
    vww(0x12d0, 0);
    vww(0x12a8, 1);
    vww(0x12c4, 1);
    vww(0x12c8, 1);
}

/* Terrain_Slick 1000:669b
   Theme 1 class 5, theme 6 class 3: grip lost for 20 steps ([1284] = 14h), sound 12h for a player. */
void Terrain_Slick(void)
{
    vww(0x1284, 0x14);
    effect_if_visible(0x12);
}

/* Terrain_SlickB 1000:66b2
   Theme 7 class 15: leaving a raised level jumps (/15 + 4); the second low-grip timer [1286] = 14h. */
void Terrain_SlickB(void)
{
    jump_off_level(15, 4);
    vww(0x1286, 0x14);
}

/* Terrain_RampBig 1000:66ed
   Theme 5 class 1, 8.4, 9.1: leaving a raised level jumps (/15 + 6); unless coming from class 6, a
   jump of speed^2 / 9 + 0bh on theme 2 (no theme 2 table has it), + 2 on the others. */
void Terrain_RampBig(void)
{
    jump_off_level(15, 6);
    f_cmp16(vw(0x12d2), 6);
    if (vw(0x12d2) == 6) return;
    speed_sq();
    f_cmp8(theme(), 2);
    bool t2 = theme() == 2;
    div_by(9);
    R.ax = f_add16(R.ax, t2 ? 0x0b : 2);
    vww(0x12d4, R.ax);
}

/* Terrain_RampSmall 1000:6768
   Theme 1 class 6, 2.2, 4.4, 5.15, 6.2, 8.5, 9.2: leaving a raised level jumps (/15 + 4); a speed^2
   of at least 0eh jumps max(speed^2, 1ch) / 7 (+ 7 on theme 2). */
void Terrain_RampSmall(void)
{
    jump_off_level(15, 4);
    speed_sq();
    f_cmp16(R.ax, 0x0e);
    if ((int16_t)R.ax < 0x0e) return;
    f_cmp16(R.ax, 0x1c);
    if ((int16_t)R.ax < 0x1c) R.ax = 0x1c;
    div_by(7);
    f_cmp8(theme(), 2);
    if (theme() == 2) R.ax = f_add16(R.ax, 7);
    vww(0x12d4, R.ax);
}

/* Terrain_RampSteep 1000:67d8
   Theme 7 class 14: leaving a raised level jumps (/15 + 4); unless coming from class 6, a jump of
   speed^2 / 5 + 4. */
void Terrain_RampSteep(void)
{
    jump_off_level(15, 4);
    f_cmp16(vw(0x12d2), 6);
    if (vw(0x12d2) == 6) return;
    speed_sq();
    div_by(5);
    R.ax = f_add16(R.ax, 4);
    vww(0x12d4, R.ax);
}

/* Terrain_None 1000:683b
   Theme 1 class 7: a lone ret. */
void Terrain_None(void)
{
}

/* Terrain_Edge 1000:683c
   Theme 3 class 7, and Car_MoveProbe when the previous surface had flag 10h: leaving a surface with
   flag 10h ([12dc], the previous byte) jumps speed^2 / 12 + 4, marks the edge jump ([1384] = 1) and
   plays sound 1 for a player. */
void Terrain_Edge(void)
{
    f_logic16(vw(0x12dc) & 0x10);
    if (!(vw(0x12dc) & 0x10)) return;
    speed_sq();
    div_by(12);
    R.ax = f_add16(R.ax, 4);
    vww(0x12d4, R.ax);
    vww(0x1384, 1);
    effect_if_visible(1);
}

/* The four probes of a refused step up (Terrain_Levels, Terrain_LevelsB): Track_TileAt at the
   vehicle's x - 8 and x + 8 (y as is), then at x and y - 8, and y + 8; each that answers carry (a
   solid cell) marks the blocked side ([12c4] x-, [12c6] x+, [12c8] y-, [12ca] y+). The last probe
   wraps x again instead of y (as the original: y + 8 past the world's edge is not wrapped). If none
   answered, x- and y- are marked. Either way the vehicle bounces ([12a8] = 1). */
static void blocked_step(void)
{
    R.ax = vw(0x125c);
    R.dx = vw(0x1268);
    R.ax = f_sub16(R.ax, 8);
    f_cmp16(R.ax, 0xffff);
    if ((int16_t)R.ax <= -1) R.ax = f_add16(R.ax, 0xc00);
    Track_TileAt();
    if (R.cf) vww(0x12c4, 1);
    R.ax = f_add16(R.ax, 0x10);
    f_cmp16(R.ax, 0xc00);
    if ((int16_t)R.ax >= 0xc00) R.ax = f_sub16(R.ax, 0xc00);
    Track_TileAt();
    if (R.cf) vww(0x12c6, 1);
    R.dx = f_sub16(R.dx, 8);
    f_cmp16(R.dx, 0xffff);
    if ((int16_t)R.dx <= -1) R.dx = f_add16(R.dx, 0xc00);
    R.ax = f_sub16(R.ax, 8);
    f_cmp16(R.ax, 0xffff);
    if ((int16_t)R.ax <= -1) R.ax = f_add16(R.ax, 0xc00);
    Track_TileAt();
    if (R.cf) vww(0x12c8, 1);
    R.dx = f_add16(R.dx, 0x10);
    f_cmp16(R.ax, 0xc00);
    if ((int16_t)R.ax >= 0xc00) R.ax = f_sub16(R.ax, 0xc00);
    Track_TileAt();
    if (R.cf) vww(0x12ca, 1);
    static const uint16_t sides[4] = {0x12c8, 0x12ca, 0x12c4, 0x12c6};
    bool any = false;
    for (int i = 0; i < 4 && !any; i++) {
        f_cmp16(vw(sides[i]), 1);
        any = vw(sides[i]) == 1;
    }
    if (!any) {
        vww(0x12c8, 1);
        vww(0x12c4, 1);
    }
    vww(0x12a8, 1);
}

/* The level change of a stepped surface (Terrain_Levels, Terrain_LevelsB): DX = the new level AX
   less the current [138e]. A step up of more than 2 is a wall (blocked_step). Otherwise the level is
   taken; no change, or a change of one, is a smooth step; more (a drop of 2 or more, or any change
   when the new level is -1 on Terrain_Levels) jumps speed^2 / div + 4. */
static void step_levels(uint16_t div, bool minus_one_jumps)
{
    R.dx = R.ax;
    R.dx = f_sub16(R.dx, vw(0x138e));
    f_cmp16(R.dx, 2);
    if ((int16_t)R.dx > 2) {
        blocked_step();
        return;
    }
    vww(0x138e, R.ax);
    if (minus_one_jumps) {   /* Terrain_Levels' class 14: the high step with its own jump */
        f_cmp16(R.cx, 1);
        if (R.cx == 1) {
            speed_sq();
            div_by(7);
            R.ax = f_add16(R.ax, 4);
            vww(0x12d4, R.ax);
            return;
        }
    }
    f_cmp16(R.dx, 0);
    if (R.dx == 0) return;
    bool jump = false;
    if (minus_one_jumps) {
        f_cmp16(R.ax, 0xffff);
        jump = R.ax == 0xffff;
    }
    if (!jump) {
        f_cmp16(R.dx, 0xffff);
        if ((int16_t)R.dx <= -1) {
            R.cf = R.dx != 0;
            R.dx = (uint16_t)-R.dx;
            flags_zs16(R.dx);
        }
        f_cmp16(R.dx, 1);
        if (R.dx == 1) return;
    }
    f_cmp16(R.dx, 1);
    speed_sq();
    div_by(div);
    R.ax = f_add16(R.ax, 4);
    vww(0x12d4, R.ax);
}

/* Terrain_Levels 1000:6882
   Themes 4 and 5, classes 5-12 (and 13, 14 on theme 4): stepped heights. The level is class - 4;
   class 13 is level -1 (any change to it jumps speed^2 / 18 + 4), class 14 level 4 with a jump of
   speed^2 / 7 + 4. Other changes: see step_levels (jumps of speed^2 / 18 + 4). */
void Terrain_Levels(void)
{
    R.cx = 0;
    R.ax = vw(0x12d0);
    f_cmp16(R.ax, 0x0d);
    if (R.ax == 0x0d) {
        R.ax = 0xffff;
    } else {
        f_cmp16(R.ax, 0x0e);
        if (R.ax == 0x0e) {
            R.ax = 4;
            R.cx = 1;
        } else {
            R.ax = f_sub16(R.ax, 4);
        }
    }
    step_levels(0x12, true);
}

/* Terrain_LevelsB 1000:69c5
   Theme 6 classes 6-7, theme 7 classes 1-8: the same with the level = the class (class 8 counts as
   level 2) and jumps of speed^2 / 15 + 4. */
void Terrain_LevelsB(void)
{
    R.cx = 0;
    R.ax = vw(0x12d0);
    f_cmp16(R.ax, 8);
    if (R.ax == 8) R.ax = 2;
    step_levels(15, false);
}

/* Terrain_Layer 1000:6ac2
   Theme 3 class 5 (the bridges): surface flag 10h puts the vehicle on the upper layer ([1388] = 1,
   [1386] = 0), no flag on the lower ([1388] = 0, [1386] = 1). */
void Terrain_Layer(void)
{
    f_logic16(vw(0x12da) & 0x10);
    if (vw(0x12da) & 0x10) {
        vww(0x1388, 1);
        vww(0x1386, 0);
    } else {
        vww(0x1388, 0);
        vww(0x1386, 1);
    }
}
