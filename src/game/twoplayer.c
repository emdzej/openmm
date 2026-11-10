/* The two-player games (docs/re/game-structure.md): the two players pick their characters, then a
   tournament (tracks drawn at random, first to four wins) or single races on a vehicle class chosen
   on the screen; the result screens keep each character's wins and losses for the session and rate
   them.

   Data (193c): 0c03 / 0c1e the two racer objects (frame word +13: the character in the low nibble);
   098a / 098c the tournament's tallies (player one, player two), 0989 / 098b their dead twins; 09a4 + n
   / 09af + n the wins and losses of character n; 08a5 1 in a tournament, 0988 the same for the dead
   result screen; 08a0 the last menu choice, 08a2 the choice (also cs:9c62); 08a3 the vehicle-class
   index 0..9 and 09d9 its ten (class, track) pairs; 09d8 the vehicle class; 09ba the tournament's
   eight tracks (theme * 4 + track - 1) and 09c2..09c9 the used flags. */
#include "cpu.h"
#include "game.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* A tune through the driver (AH = 9 asks, AH = 4 plays); AX as it was, the flags the driver's when it
   played, else those of `or al, al`. */
static void play_tune(uint8_t n)
{
    uint16_t ax = R.ax;
    R.ax = (uint16_t)(0x0900 | n);
    mach_drv();
    R.cf = false;
    flags_zs8(AL);
    if (AL != 0) {
        R.ax = (uint16_t)(0x0400 | n);
        mach_drv();
    }
    R.ax = ax;
}

static void wait_tick(void)
{
    R.ax = d16(0x0002);
    while (d16(0x0002) == R.ax) mach_frame();
}

static void obj_w(uint16_t obj, uint16_t field, uint16_t v) { d16w((uint16_t)(obj + field), v); }
static uint16_t obj_r(uint16_t obj, uint16_t field) { return d16((uint16_t)(obj + field)); }

/* Menu_TwoPlayerGame 1000:1e20
   From the choice of players: the two small pictures placed (0cdb at 0ah,46h, 0cf6 at d2h,46h
   mirrored); [03f3] = [0156] = [03f8] = 1, player 2's slot from its setting [0f61], two racers,
   [09d6] = 0; the slots rebuilt, the characters reset, the portraits; PLAYER ONE ([094d] / [0954] at
   x 4) picks (object 0c03, input 137b, previous pick [09a0]); the label cleared, PLAYER TWO ([094d] /
   [0958] at x beh) picks (object 0c1e, input 14df, previous [09a2]); then the game menu. Esc at either
   pick ends it. [03f3] = 0 on the way out. */
void Menu_TwoPlayerGame(void)
{
    d16w(0x0cdd, 0x0a);
    d16w(0x0cdf, 0x46);
    d8w(0x0ce5, 0);
    d16w(0x0cf8, 0xd2);
    d16w(0x0cfa, 0x46);
    d8w(0x0d00, 1);
    R.ax = d16(0x0f61);
    d8w(0x03f3, 1);
    d8w(0x0156, 1);
    d16w(0x265a, R.ax);
    d16w(0x2656, 2);
    d8w(0x03f8, 1);
    d16w(0x09d6, 0);
    Input_SetSlots();
    Menu_ResetCharacters();
    Menu_DrawRacers();
    R.si = 0x094d;
    R.bx = 0x32;
    R.dx = 0x0b40;
    R.ax = 4;
    Text_Draw();
    R.si = 0x0954;
    R.bx = 0x3b;
    R.dx = 0x0b40;
    R.ax = 4;
    Text_Draw();
    d16w(0x019e, 0x0c03);
    d16w(0x1080, 0x137b);
    R.si = 0x020f;
    R.ax = d16(0x09a0);
    Menu_SelectCharacter();
    if (!R.cf) {
        R.ax = 0;
        R.bx = 0x32;
        R.cx = 0x14;
        R.dx = 0x40;
        R.si = 0;
        Gfx_FillRect();
        R.si = 0x094d;
        R.bx = 0x32;
        R.dx = 0x0b40;
        R.ax = 0xbe;
        Text_Draw();
        R.si = 0x0958;
        R.bx = 0x3b;
        R.dx = 0x0b40;
        R.ax = 0xbe;
        Text_Draw();
        d16w(0x019e, 0x0c1e);
        d16w(0x1080, 0x14df);
        R.si = 0x020f;
        R.ax = d16(0x09a2);
        Menu_SelectCharacter();
        if (!R.cf) CALL_R(Menu_TwoPlayerChoose);
    }
    d8w(0x03f3, 0);
}

/* Menu_TwoPlayerChoose 1000:1ef1
   Tune 2; the tallies [0989..098c] and [08a2] cleared; the portraits, TOURNAMENT and SINGLE RACE
   ([092f] / [093a] on row beh) under two pictures (object 0c8a frames 4 and 5), CHOOSE GAME! ([0922]);
   the choice of two (row 90h, previous [08a0]): 0 (Esc) back with [08a2] = 0; else [08a0] = [08a2] =
   the choice, two racers; 1 the tournament (and back); 2 single races ([0194] kept round them), then
   this menu again (a jump to the entry). */
void Menu_TwoPlayerChoose(void)
{
    for (;;) {
        play_tune(2);
        d8w(0x098a, 0);
        d8w(0x098c, 0);
        d8w(0x0989, 0);
        d8w(0x098b, 0);
        d8w(0x08a2, 0);
        Menu_DrawRacers();
        R.si = 0x092f;
        R.bx = 0xbe;
        R.dx = 0x0b40;
        R.ax = 0x18;
        Text_Draw();
        R.si = 0x093a;
        R.bx = 0xbe;
        R.dx = 0x0b40;
        R.ax = 0x9c;
        Text_Draw();
        R.si = 0x0922;
        R.bx = 0x68;
        R.dx = 0x0b54;
        Text_DrawCentred();
        R.bx = 0x0c8a;
        obj_w(R.bx, 4, 0x7c);
        obj_w(R.bx, 2, 0x10);
        obj_w(R.bx, 0x13, 4);
        Gfx_DrawObject();
        R.bx = 0x0c8a;
        obj_w(R.bx, 2, 0x94);
        obj_w(R.bx, 0x13, 5);
        Gfx_DrawObject();
        Gfx_Present();
        R.bx = 0x90;
        R.ax = d16(0x08a0);
        Menu_ChooseOfTwo();
        if (R.cx == 0) {
            d8w(0x08a2, 0);
            R.cf = false;
            R.zf = true;
            return;
        }
        d16w(0x08a0, R.cx);
        d16w(0x2656, 2);
        d8w(0x08a2, CL);
        if (R.cx != 2) {
            Menu_TwoPlayerTournament();
            return;
        }
        uint16_t saved = d16(0x0194);
        CALL_R(Menu_TwoPlayerSingleRace);
        d16w(0x0194, saved);
    }
}

/* Menu_TwoPlayerTournament 1000:1faf
   The tournament: [0988] = [08a5] = 1, race 1. Each race: a track of the eight at [09ba] not used yet
   (the used flags [09c2..09c9] cleared again once all are used), drawn by the low three bits of the
   tick counter [0002] and drawn again until a free one comes up (on the real machine the loop spins
   until the timer moves the counter to a free value; the port lets a frame pass between tries, the same
   thing in its time); DS = 193c; the theme, the track and the vehicle class (= the theme) set; the two
   track pictures (0cdb frame theme - 1, 0cf6 frame theme + 7); the header (row 24h) and the ratings,
   TOURNAMENT RACE nn ([0975], digits at [0985]), the class picture (0bb2 at 50h,7fh) and the class
   banner, a key wait, the race, its result, the next race number. Until a player's tally reaches 4:
   then the champion's screen for that player (BX = 0c03 or 0c1e). */
void Menu_TwoPlayerTournament(void)
{
    d8w(0x0988, 1);
    d8w(0x08a5, 1);
    d8w(0x28c1, 1);
refill:                        /* 1000:1fbe */
    {
        uint16_t es = R.es;
        R.es = R.ds;
        SET_AL(0);
        R.cx = 8;
        R.di = 0x09c2;
        while (R.cx != 0) {
            mbw(R.es, R.di, AL);
            R.di++;
            R.cx--;
        }
        R.es = es;
    }
    for (;;) {
        /* 1fcc */
        R.si = 0x09c2;
        while (d8(R.si) != 0) {
            R.si++;
            if ((int16_t)R.si >= (int16_t)0x09ca) goto refill;
        }
        for (;;) {
            R.bx = d16(0x0002) & 7;
            if (d8((uint16_t)(R.bx + 0x09c2)) == 0) break;
            mach_frame();
        }
        d8w((uint16_t)(R.bx + 0x09c2), 1);
        R.ax = 0x193c;
        R.ds = SEG_DATA;
        R.bx = (uint16_t)(R.bx + 0x09ba);
        SET_AL(d8(R.bx));
        SET_AH(AL & 3);
        SET_AH(AH + 1);
        SET_AL(AL >> 2);
        d8w(0x28bf, AL);
        d8w(0x28c0, AH);
        d8w(0x09d8, AL);
        R.bx = 0x0cdb;
        SET_CL(d8(0x28bf) - 1);
        SET_CH(0);
        obj_w(R.bx, 0x13, R.cx);
        Gfx_DrawObject();
        R.bx = 0x0cf6;
        SET_CL(d8(0x28bf) - 1);
        SET_CH(0);
        R.cx = (uint16_t)(R.cx + 8);
        obj_w(R.bx, 0x13, R.cx);
        Gfx_DrawObject();
        R.ax = 0x24;
        Menu_TwoPlayerHeader();
        Menu_TwoPlayerRatings();
        SET_AL(d8(0x28c1));
        R.si = 0x0985;
        Text_TwoDigits();
        R.si = 0x0975;
        R.bx = 0x6e;
        R.dx = 0x0b54;
        Text_DrawCentred();
        d16w(0x0bb6, 0x7f);
        d16w(0x0bb4, 0x50);
        SET_AL(d8(0x28bf) - 1);
        SET_AH(0);
        d16w(0x0bc5, R.ax);
        R.bx = 0x0bb2;
        Gfx_DrawObject();
        Menu_ClassBanner();
        Menu_WaitKey();
        CALL_R(Race_RunTwoPlayer);
        Menu_TwoPlayerResult();
        d8w(0x28c1, (uint8_t)(d8(0x28c1) + 1));
        R.bx = 0x0c03;
        if (d8(0x098a) == 4) break;
        R.bx = 0x0c1e;
        if (d8(0x098c) == 4) break;
    }
    Menu_ChampionScreen();
}

/* Race_RunTwoPlayer 1000:216c
   [1082] = 1, the driver's tune stopped (AH = 7), the race (DS kept round it), [1082] = 0, the front end
   loaded again; a race quit with Esc ([1096] = 1) leaves the game (a jump to Main_Restart); else the
   finishing order into racer objects. */
void Race_RunTwoPlayer(void)
{
    d8w(0x1082, 1);
    SET_AH(7);
    mach_drv();
    uint16_t ds = R.ds;
    Race_Main();
    R.ds = ds;
    d8w(0x1082, 0);
    Menu_LoadFrontEnd();
    if (d8(0x1096) == 1) {
        Main_Restart();
        return;
    }
    Race_ResultsToRacers();
}

/* Menu_StepVehicleClass 1000:2193
   BX = +1, -1 or 0. Tune 2; the class index [08a3] moved (below 0 wraps to 9, above 9 to 0) and its
   pair at [09d9 + 2n] taken: [09d8] = the vehicle class, [28c0] = the track; the theme [28bf] the class,
   except class 10 (theme 3) and 11 (theme 1). The class picture (0bb2 frame theme - 1 at 50h,7fh) and
   the class banner, then the rows from the picture's y, 48h of them, presented. Registers kept
   (pusha / popa), the flags Gfx_PresentRows'. */
void Menu_StepVehicleClass(void)
{
    play_tune(2);
    Cpu saved = R;
    R.ax = (uint16_t)(d16(0x08a3) + R.bx);
    if ((int16_t)R.ax < 0) R.ax = 9;
    if ((int16_t)R.ax > 9) R.ax = 0;
    d16w(0x08a3, R.ax);
    R.ax = (uint16_t)(R.ax << 1);
    R.si = (uint16_t)(0x09d9 + R.ax);
    R.ax = d16(R.si);
    R.ax = (uint16_t)(R.ax << 8 | R.ax >> 8);
    d8w(0x09d8, AL);
    d8w(0x28c0, AH);
    if (AL == 0x0a) SET_AL(3);
    if (AL == 0x0b) SET_AL(1);
    d8w(0x28bf, AL);
    SET_CL(d8(0x28bf) - 1);
    SET_CH(0);
    d16w(0x0bc5, R.cx);
    d16w(0x0bb6, 0x7f);
    d16w(0x0bb4, 0x50);
    R.bx = 0x0bb2;
    Gfx_DrawObject();
    Menu_ClassBanner();
    R.bx = 0x0bb2;
    R.bx = obj_r(R.bx, 4);
    R.ax = R.bx;
    R.bx = (uint16_t)(R.bx + 0x40);
    R.cx = 0x48;
    Gfx_PresentRows();
    uint16_t ds = R.ds, es = R.es;
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    R = saved;
    R.ds = ds, R.es = es;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Menu_ClassBanner 1000:2216
   The rows under the class picture's neighbour (object 0bec's y + 40h, 8 rows) and under the class
   picture ([0bb6] + 40h) cleared; the vehicle class's name (entry [09d8] - 1 of the list at [002f],
   font 0b40, centred) on the latter. Then the two track pictures (0cdb frame theme - 1 from x 0, 0cf6
   frame theme + 7 from x e0h, both at row 46h) slide towards each other by smoothness [263a] * 4 pixels
   a step until the left one is past 58h: each step cs:4ade cleared, both drawn, a tick (Sys_WaitTick),
   presented, both erased. Flags: the last `cmp [0cdd], 58h`. */
void Menu_ClassBanner(void)
{
    R.bx = (uint16_t)(d16(0x0bec) + 0x40);
    R.ax = 0;
    R.cx = 8;
    Gfx_FillRows();
    R.ax = 0xffff;
    SET_CL(d8(0x28bf) - 1);
    SET_CH(0);
    d16w(0x0cee, R.cx);
    d16w(0x0cdd, 0);
    R.cx = (uint16_t)(R.cx + 8);
    d16w(0x0d09, R.cx);
    d16w(0x0cf8, 0xe0);
    R.bx = (uint16_t)(d16(0x0bb6) + 0x40);
    uint16_t row = R.bx;
    R.cx = 8;
    R.ax = 0;
    Gfx_FillRows();
    R.bx = row;
    SET_CL(d8(0x09d8) - 1);
    SET_CH(0);
    R.si = 0x002f;
    R.ax = 0xffff;
    R.dx = 0x0b40;
    Text_DrawNth();
    do {
        c8w(0x4ade, 0);
        d16w(0x0cfa, 0x46);
        d16w(0x0cdf, 0x46);
        uint32_t p = (uint32_t)d16(0x263a) * 4;
        R.ax = (uint16_t)p;
        R.dx = (uint16_t)(p >> 16);
        R.cx = 4;
        d16w(0x0cdd, (uint16_t)(d16(0x0cdd) + R.ax));
        d16w(0x0cf8, (uint16_t)(d16(0x0cf8) - R.ax));
        R.bx = 0x0cdb;
        Gfx_DrawObjectSaved();
        R.bx = 0x0cf6;
        Gfx_DrawObjectSaved();
        Sys_WaitTick();
        Gfx_Present();
        R.bx = 0x0cdb;
        Gfx_RestoreUnder();
        R.bx = 0x0cf6;
        Gfx_RestoreUnder();
    } while ((int16_t)d16(0x0cdd) <= 0x58);
    R.cf = d16(0x0cdd) < 0x58;
    flags_zs16((uint16_t)(d16(0x0cdd) - 0x58));
}

/* Menu_TwoPlayerSingleRace 1000:2329
   Single races: [08a5] = 0 (no tournament), tune 2, [0988] = 0. Each time: the two track pictures
   placed (0cdb at 56h, 0cf6 at 96h mirrored), the input cleared; the header (row 24h); a vehicle picture
   (object 0c6f frame 1 at b8h,88h; its x restored after); the ratings; SELECT VEHICLE ([0913], row 6eh).
   After fire is released, the class shown (BX = 0); then each tick the class name drawn and the input
   read: Esc ([107e] = 1) returns; fire runs the race ([0194] = 0b40), its result, and starts over; left
   or right step the class -- both forwards (BX = 1 for either bit, as the original) -- and the tick
   counter is cleared. */
void Menu_TwoPlayerSingleRace(void)
{
    d8w(0x08a5, 0);
    play_tune(2);
    d8w(0x0988, 0);
    for (;;) {
        /* 234b */
        d16w(0x0cdd, 0x56);
        d8w(0x0ce5, 0);
        d16w(0x0cf8, 0x96);
        d8w(0x0d00, 1);
        d16w(0x1080, 0);
        d8w(0x107e, 0);
        d8w(0x107f, 0);
        R.ax = 0x24;
        Menu_TwoPlayerHeader();
        R.bx = 0x0c6f;
        R.ax = d16(0x0c71);
        uint16_t x = R.ax;
        d16w(0x0c82, 1);
        d16w(0x0c71, 0xb8);
        d16w(0x0c73, 0x88);
        Gfx_DrawObjectSaved();
        R.ax = x;
        d16w(0x0c71, R.ax);
        Menu_TwoPlayerRatings();
        R.bx = 0;
        R.si = 0x0913;
        R.bx = 0x6e;
        R.dx = 0x0b54;
        Text_DrawCentred();
        do {
            wait_tick();
            Input_Read();
        } while (d8(0x108b) & 8);
        R.bx = 0;
        Menu_StepVehicleClass();
        bool race = false;
        while (!race) {
            /* 23c2 */
            d16w(0x0002, 0);
            for (;;) {
                /* 23c8 */
                Menu_ShowClassName();
                wait_tick();
                Input_Read();
                SET_AL(d8(0x108b));
                SET_CL(2);
                if (d8(0x107e) == 1) {
                    R.cf = false;
                    R.zf = true;
                    return;
                }
                SET_CL(3);
                if (AL & 8) {
                    race = true;
                    break;
                }
                R.bx = 1;
                if (AL & 0x80 || AL & 0x40) {
                    Menu_StepVehicleClass();
                    break;
                }
            }
        }
        d16w(0x0194, 0x0b40);
        CALL_R(Race_RunTwoPlayer);
        Menu_TwoPlayerResult();
    }
}

/* Menu_TwoPlayerHeader 1000:240a
   AX = the row. The backdrop; the two portraits (0c03 at x 20h, 0c1e at x b0h, both at the row, frame
   words masked to 4fh) with boxes (0dh) and name labels; in a tournament ([08a5]) the tallies as
   words of the list at [08a6] (font 0b54) beside them, at x + 38h and x - 10h. Flags at the return
   outside a tournament: `cmp [08a5], 0`. */
void Menu_TwoPlayerHeader(void)
{
    d16w(0x0c07, R.ax);
    d16w(0x0c22, R.ax);
    Menu_DrawBackdrop();
    d16w(0x0c16, d16(0x0c16) & 0x4f);
    d16w(0x0c05, 0x20);
    R.bx = 0x0c03;
    Menu_DrawFace();
    R.ax = 0x0d;
    Gfx_FrameObject();
    Menu_DrawFaceName();
    d16w(0x0c31, d16(0x0c31) & 0x4f);
    d16w(0x0c20, 0xb0);
    R.bx = 0x0c1e;
    Menu_DrawFace();
    R.ax = 0x0d;
    Gfx_FrameObject();
    Menu_DrawFaceName();
    if (d8(0x08a5) == 0) {
        R.cf = false;
        R.zf = true;
        return;
    }
    SET_CL(d8(0x098a));
    SET_CH(0);
    R.si = 0x08a6;
    R.ax = d16(0x0c05);
    R.bx = d16(0x0c07);
    R.ax = (uint16_t)(R.ax + 0x38);
    R.dx = 0x0b54;
    Text_DrawNth();
    SET_CL(d8(0x098c));
    SET_CH(0);
    R.si = 0x08a6;
    R.ax = d16(0x0c20);
    R.bx = d16(0x0c22);
    R.ax = (uint16_t)(R.ax - 0x10);
    R.dx = 0x0b54;
    Text_DrawNth();
}

/* One racer's lines of Menu_TwoPlayerRatings: BX = the character, the object's x / y words at
   [xw] / [yw]. */
static void rating_lines(uint16_t xw, uint16_t yw)
{
    SET_AL(d8((uint16_t)(R.bx + 0x09a4)));
    R.si = 0x099a;
    uint16_t wins = R.ax;
    Text_TwoDigits();
    SET_AL(d8((uint16_t)(R.bx + 0x09af)));
    uint16_t losses = R.ax;
    R.si = 0x0991;
    Text_TwoDigits();
    R.si = 0x0997;
    R.dx = 0x0b40;
    R.bx = d16(yw);
    R.ax = d16(xw);
    R.bx = (uint16_t)(R.bx + 0x3a);
    R.ax = (uint16_t)(R.ax - 0x18);
    uint16_t ax = R.ax, bx = R.bx, dx = R.dx;
    Text_Draw();
    R.dx = dx;
    R.bx = bx;
    R.ax = ax;
    R.si = 0x098d;
    R.ax = (uint16_t)(R.ax + 0x30);
    Text_Draw();
    R.di = R.ax;
    R.bp = R.bx;
    R.ax = losses;
    R.bx = wins;
    SET_AH(0);
    SET_BH(0);
    R.bx = (uint16_t)(R.bx - R.ax);
    R.bx = (uint16_t)(R.bx + 0x0a);
    if ((int16_t)R.bx < 0) R.bx = 0;
    if ((int16_t)R.bx > 0x14) R.bx = 0x14;
    SET_CL(d8((uint16_t)(R.bx + 0x08b0)));
    SET_CH(0);
    R.ax = R.di;
    R.bx = R.bp;
    R.ax = (uint16_t)(R.ax - 0x30);
    R.bx = (uint16_t)(R.bx + 8);
    R.si = 0x08cd;
    Text_DrawNth();
}

/* Menu_TwoPlayerRatings 1000:2481
   For each of the two racers, beside its portrait: WON nn and LOST nn ([0997] / [098d], digits at
   [099a] / [0991], font 0b40) from its character's wins [09a4 + n] and losses [09af + n], and a rating
   word of the list at [08cd] (GRANNY .. EXPERT) chosen by the byte [08b0 + clamp(wins - losses + 10,
   0, 20)]. The first racer's character is its frame word and 1fh, the second's and 0fh (as the
   original). DI and BP are left as the second rating's text position. */
void Menu_TwoPlayerRatings(void)
{
    R.bx = d16(0x0c16) & 0x1f;
    rating_lines(0x0c05, 0x0c07);
    R.bx = d16(0x0c31) & 0x0f;
    rating_lines(0x0c20, 0x0c22);
}

/* Menu_TwoPlayerResult 1000:256e
   After a two-player race: the winner ([03fc] is 0c03 or not) gets a tally ([098a] or [098c]) and
   [03fc] / [03fe] become the winner and the loser; tune 8; the winner's character a win, the loser's a
   loss. The header (row 64h) and the ratings; WINNER! / LOSER! ([0966] / [096e], row 5ch, at 1dh and b1h)
   under each; the places in the frame words (winner 200h, loser 300h); RESULTS!! ([095c], row 3ch) and
   SINGLE RACE or TOURNAMENT RACE nn (row 4ch); presented and faded in; the two track pictures slide in
   from both sides four pixels a tick to x 58h (row b6h); then the two racers flash until fire or a key
   (blinks of 14h ticks). */
void Menu_TwoPlayerResult(void)
{
    R.bx = 0x0c03;
    R.ax = 0x0c1e;
    if (d16(0x03fc) != 0x0c03) {
        uint16_t t = R.bx;
        R.bx = R.ax;
        R.ax = t;
        d8w(0x098c, (uint8_t)(d8(0x098c) + 1));
    } else {
        d8w(0x098a, (uint8_t)(d8(0x098a) + 1));
    }
    d16w(0x03fc, R.bx);
    d16w(0x03fe, R.ax);
    play_tune(8);
    R.bx = d16(0x03fc);
    R.bx = obj_r(R.bx, 0x13) & 0x0f;
    d8w((uint16_t)(R.bx + 0x09a4), (uint8_t)(d8((uint16_t)(R.bx + 0x09a4)) + 1));
    R.bx = d16(0x03fe);
    R.bx = obj_r(R.bx, 0x13) & 0x0f;
    d8w((uint16_t)(R.bx + 0x09af), (uint8_t)(d8((uint16_t)(R.bx + 0x09af)) + 1));
    R.ax = 0x64;
    Menu_TwoPlayerHeader();
    Menu_TwoPlayerRatings();
    R.ax = 0x1d;
    R.bx = 0x5c;
    R.si = 0x0966;
    if (d16(0x03fc) != 0x0c03) R.si = 0x096e;
    Text_Draw();
    R.ax = 0xb1;
    R.bx = 0x5c;
    R.si = 0x0966;
    if (d16(0x03fc) != 0x0c1e) R.si = 0x096e;
    Text_Draw();
    R.cx = 0x200;
    R.dx = 0x300;
    if (d16(0x03fc) != 0x0c03) {
        uint16_t t = R.dx;
        R.dx = R.cx;
        R.cx = t;
    }
    d16w(0x0c16, d16(0x0c16) | R.cx);
    d16w(0x0c31, d16(0x0c31) | R.dx);
    R.si = 0x095c;
    R.bx = 0x3c;
    R.dx = 0x0b54;
    Text_DrawCentred();
    R.si = d8(0x08a5) == 0 ? 0x093a : 0x0975;
    R.bx = 0x4c;
    R.dx = 0x0b54;
    Text_DrawCentred();
    Gfx_Present();
    Pal_FadeIn();
    d16w(0x0cdd, 0);
    d16w(0x0cf8, 0xe0);
    d16w(0x0cdf, 0xb6);
    d16w(0x0cfa, 0xb6);
    SET_CL(d8(0x28bf) - 1);
    SET_CH(0);
    d16w(0x0cee, R.cx);
    R.cx = (uint16_t)(R.cx + 8);
    d16w(0x0d09, R.cx);
    for (;;) {
        d16w(0x0cdd, (uint16_t)(d16(0x0cdd) + 4));
        d16w(0x0cf8, (uint16_t)(d16(0x0cf8) - 4));
        R.bx = 0x0cdb;
        Gfx_DrawObjectSaved();
        R.bx = 0x0cf6;
        Gfx_DrawObjectSaved();
        Sys_WaitTick();
        Gfx_Present();
        if (d16(0x0cdd) == 0x58) break;
        R.bx = 0x0cdb;
        Gfx_RestoreUnder();
        R.bx = 0x0cf6;
        Gfx_RestoreUnder();
    }
    do {
        R.bx = 0x0c03;
        obj_w(R.bx, 0x13, obj_r(R.bx, 0x13) ^ 0x10);
        Menu_DrawFace();
        R.bx = 0x0c1e;
        obj_w(R.bx, 0x13, obj_r(R.bx, 0x13) ^ 0x10);
        Menu_DrawFace();
        Gfx_Present();
        R.cx = 0x14;
        Menu_WaitKeyBlink();
    } while (R.cf);
}
