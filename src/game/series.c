/* The one-player games (docs/re/game-structure.md): head to head and the championship, both a series of
   races run by Race_Series; the screens between the races (the progress map, the pre-race screen, the
   results, the knock-out, the lives, the bonus race, the champion) and the key waits the front end
   shares.

   The racers on these screens are picture objects of 1bh bytes in the data segment, 0c03 (the player),
   0c1e, 0c39 and 0c54: +2 x, +4 y, +8 the picture's segment, +0a mirror, +13 the frame word (low nibble
   the character, flag 10h flashing, 40h, places 100h..300h; 0bh an empty place), +19 the height.
   The series' state: 193c:03f8 1 in head to head, 2656 the racers on the race screen (1 = four,
   2 = two), 28bf / 28c0 the theme and the track (1-based), 28c1 the race number (0 = the qualifying
   race), 0439 the last race, 043c the race list (theme * 4 + track - 1 a byte), 0342 / 0343 the bonus
   track and 1 during a bonus race, 043b the last bonus track, 03fa the wins left before a bonus race,
   0406 the lives, 0310 the races since the qualification, 03fc..0402 the finishing order as racer
   objects, 0164..016e the roster (20h out, 40h picked), 0346 the next opponent to be knocked out. */
#include "cpu.h"
#include "game.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"
#include "series.h"

/* Menu_HeadToHeadGame 1000:0fbf
   Head to head (one player against one computer racer): [03f3] = 0, [03f8] = 1, two racers on the race
   screen, [0310] = 0, the menus read player 1's input (137b), player 2's slot set to none (6, the
   computer), cs:9c62 (193c:08a2 through CS) = 0, the input slots rebuilt. The portraits; the player
   picks a character (SI = 020f the pick-yourself prompt, object 0c03, the previous pick [03f4]), then an
   opponent (SI = 0227, object 0c1e, [03f6]); each kept; then the press-a-key prompt and the series. Esc
   at either pick (carry) skips to the end: player 2's control back from [0f61], the slots rebuilt. */
void Menu_HeadToHeadGame(void)
{
    d8w(0x03f3, 0);
    d8w(0x03f8, 1);
    d16w(0x2656, 2);
    d8w(0x0310, 0);
    d16w(0x1080, 0x137b);
    d16w(0x265a, 6);
    c16w(0x9c62, 0);
    Input_SetSlots();
    Menu_DrawRacers();
    R.si = 0x020f;
    d16w(0x019e, 0x0c03);
    R.ax = d16(0x03f4);
    Menu_SelectCharacter();
    if (!R.cf) {
        R.ax = d16(0x0c16);
        d16w(0x03f4, R.ax);
        R.si = 0x0227;
        d16w(0x019e, 0x0c1e);
        R.ax = d16(0x03f6);
        Menu_SelectCharacter();
        if (!R.cf) {
            R.ax = d16(0x0c31);
            d16w(0x03f6, R.ax);
            Menu_PressAnyKey();
            CALL_R(Race_Series);
        }
    }
    R.ax = d16(0x0f61);
    d16w(0x265a, R.ax);
    Input_SetSlots();
}

/* Menu_ChampionshipGame 1000:102b
   The championship: [03f3] = [03f8] = [0342] (the first bonus track) = [0310] = 0, four racers, the
   menus on player 1's input, player 2's slot none, cs:9c62 = 0, the slots rebuilt. The player's object
   0c03 at 68h,27h with its face and a box (colour 0dh); the pick-yourself prompt (previous pick
   [03f4]); on a pick its frame word kept in [03f4], the press-a-key prompt, [266a] = 6, the series. */
void Menu_ChampionshipGame(void)
{
    d8w(0x03f3, 0);
    d8w(0x03f8, 0);
    d8w(0x0342, 0);
    d16w(0x2656, 1);
    d8w(0x0310, 0);
    c16w(0x9c62, 0);
    d16w(0x1080, 0x137b);
    d16w(0x265a, 6);
    Input_SetSlots();
    R.bx = 0x0c03;
    d16w(0x019e, R.bx);
    d16w((uint16_t)(R.bx + 2), 0x68);
    d16w((uint16_t)(R.bx + 4), 0x27);
    Menu_DrawFace();
    R.ax = 0x0d;
    Gfx_FrameObject();
    R.si = 0x020f;
    R.ax = d16(0x03f4);
    Menu_SelectCharacter();
    if (!R.cf) {
        R.bx = d16(0x019e);
        R.ax = d16((uint16_t)(R.bx + 0x13));
        d16w(0x03f4, R.ax);
        Menu_PressAnyKey();
        d16w(0x266a, 6);
        CALL_R(Race_Series);
    }
    R.ax = d16(0x0f61);
    d16w(0x265a, R.ax);
    Input_SetSlots();
}

/* Race_Series 1000:10a0
   The series: Esc's flag cleared, 3 wins to a bonus race, no bonus race, race 0, [0310] = 0, the slots
   of vehicles 3 and 4 none (6). Race 0 qualifies: first place, or (outside head to head) second, gives
   QUALIFIED (message 1) and, in the championship, the opponents picked again for the empty places;
   anything else FAILED TO QUALIFY (message 0) and the end. Then race after race up to [0439]:
   - head to head: a win goes on to the next race, a loss (a life gone in Menu_RaceResults) repeats it;
   - championship: a win counts down [03fa], and at 0 (unless it is the last race) a bonus race;
     second place goes on (except at race 19h, which must be won); third or fourth (a life gone)
     resets the count to 3 and repeats the race.
   No lives left (carry from Menu_RaceResults) ends the series; after the last race the champion's
   screen for the player (BX = 0c03). */
void Race_Series(void)
{
    d8w(0x1096, 0);
    d8w(0x03fa, 3);
    d8w(0x0343, 0);
    d8w(0x28c1, 0);
    d8w(0x0310, 0);
    d16w(0x266c, 6);
    d16w(0x266e, 6);
    R.bx = 0x0c03;
    CALL_R(Race_RunOne);
    bool qualified = cmpw(d16(0x03fc), 0x0c03);
    if (!qualified && !cmpb(d8(0x03f8), 1)) qualified = cmpw(d16(0x03fe), 0x0c03);
    if (!qualified) {
        R.cx = 0;
        logicw(0);
        Menu_LivesMessage();
        return;
    }
    R.cx = 1;
    Menu_LivesMessage();
    if (cmpb(d8(0x03f8), 0)) Menu_PickOpponents();
    for (;;) {
        /* 10f9: the next race (inc leaves CF) */
        d8w(0x28c1, (uint8_t)(d8(0x28c1) + 1));
        d8w(0x0310, (uint8_t)(d8(0x0310) + 1));
        SET_AL(d8(0x0439));
        cmpb(d8(0x28c1), AL);
        if (!R.cf && !R.zf) break;
        for (;;) {
            /* 110a: run it */
            CALL_R(Race_RunOne);
            Menu_RaceResults();
            if (R.cf) return;
            if (!cmpb(d8(0x03f8), 0)) {
                if (cmpw(d16(0x03fc), 0x0c03)) break;
                continue;
            }
            if (cmpw(d16(0x03fc), 0x0c03)) {
                d8w(0x03fa, (uint8_t)(d8(0x03fa) - 1));
                flags_zs8(d8(0x03fa));
                if (d8(0x03fa) == 0) {
                    SET_AL(d8(0x0439));
                    if (!cmpb(d8(0x28c1), AL)) CALL_R(Race_BonusRace);
                }
                break;
            }
            if (!cmpb(d8(0x28c1), 0x19) && cmpw(d16(0x03fe), 0x0c03)) break;
            d8w(0x03fa, 3);
        }
    }
    R.bx = 0x0c03;
    Menu_ChampionScreen();
}

/* Race_RunOne 1000:115c
   One race of the series: the theme and the track from the race list (race [28c1]), or for a bonus race
   ([0343]) theme 9 and track [0342] + 1; the progress map (not in head to head, not before the last
   race); the pre-race screen; the fade flag [0311] cleared, [1082] = 1 (in a race), the driver's tune
   stopped (AH = 7), the race (DS kept round it); ten lives when the cheat flag [0f69] is 1; [1082] = 0;
   the finishing order into racer objects, the front end loaded again. A race quit with Esc ([1096] = 1)
   leaves the game: a jump to Main_Restart. Flags at the return: those of `cmp [1096], 1`. */
void Race_RunOne(void)
{
    SET_BL(d8(0x28c1));
    if (!cmpb(d8(0x0343), 0)) {
        SET_AL(9);
        SET_AH(d8(0x0342));
        d8w(0x28bf, AL);
        SET_AH(AH + 1);
        flags_zs8(AH);
        d8w(0x28c0, AH);
    } else {
        race_from_list();
    }
    if (!cmpb(d8(0x03f8), 1)) {
        SET_CL(d8(0x0439));
        if (!cmpb(d8(0x28c1), CL)) Menu_ProgressMap();
    }
    Menu_PreRaceScreen();
    d8w(0x0311, 0);
    uint16_t ds = R.ds;
    d8w(0x1082, 1);
    SET_AH(7);
    mach_drv();
    Race_Main();
    R.ds = ds;
    if (cmpb(d8(0x0f69), 1)) d8w(0x0406, 10);
    d8w(0x1082, 0);
    Race_ResultsToRacers();
    Menu_LoadFrontEnd();
    if (cmpb(d8(0x1096), 1)) {
        Main_Restart();
        return;
    }
}

/* Race_ResultsToRacers 1000:11d5
   The race's finishing order, four vehicle-record offsets at [2678..267e] (multiples of the record size
   [2662], 164h), into racer objects 0c03 + 1bh * n at [03fc..0402] (ES = DS for the stores). All
   registers kept (pusha / popa); the flags those of the last `cmp di, 402h`. */
void Race_ResultsToRacers(void)
{
    Cpu saved = R;
    uint16_t si = 0x2678, di = 0x03fc;
    do {
        uint16_t v = mw(R.ds, si);
        si = (uint16_t)(si + 2);
        uint16_t size = mw(R.ds, 0x2662);
        /* div: a record size of 0 would be the CPU's divide fault (it is 164h, set by the race) */
        uint16_t n = size ? (uint16_t)(v / size) : 0;
        mww(R.ds, di, (uint16_t)(n * 0x1b + 0x0c03));
        di = (uint16_t)(di + 2);
    } while ((int16_t)di <= (int16_t)0x0402);
    R = saved;
    R.cf = di < 0x0402;
    flags_zs16((uint16_t)(di - 0x0402));
}

/* Menu_PreRaceScreen 1000:11f8
   Tune 4 (5 before race 1ah). Three layouts:
   - a bonus race (theme 9): the wins count reset to 3; TRIPLE WIN !!!, BONUS RACE and the beat-the-clock
     line ([03bf], [03ce], [03d9], font 0b54), the class picture (frame 8 at row 5ah), TIMETRIALS ([03e8]);
   - race 0, the qualifying race: nothing in head to head; else QUALIFYING / RACE ([035c], [0367]) and the
     class picture of the theme, faded in;
   - any other race: the portraits, the track name on row 6ch, the class picture (CX: CH not cleared
     before the store, as the original), faded in; then the racers' small pictures (objects 0cdb..0d2c,
     frames theme - 1 + 8n, at x 100h + 40h n, row 5ah, mirrored) slide in, two pixels a tick: in head to
     head two of them from both sides until the left one reaches 54h, else all four leftwards until the
     first is at 10h or less.
   Then a key wait. With the cheat flag [0f6a] set, the last key released steps the race: keypad +
   (4eh) up to 19h at most, keypad - (4ah) down, from 0 to the last race; the theme and the track taken
   from the list again and the screen redone (a jump back to the entry). */
void Menu_PreRaceScreen(void)
{
again:
    SET_AL(4);
    if (cmpb(d8(0x28c1), 0x1a)) SET_AL(5);
    play_tune(AL);
    if (cmpb(d8(0x28bf), 9)) {
        d8w(0x03fa, 3);
        Menu_DrawBackdrop();
        R.si = 0x03bf;
        R.bx = 0x28;
        R.dx = 0x0b54;
        Text_DrawCentred();
        R.si = 0x03ce;
        R.bx = 0x38;
        R.dx = 0x0b54;
        Text_DrawCentred();
        R.si = 0x03d9;
        R.bx = 0x48;
        R.dx = 0x0b54;
        Text_DrawCentred();
        d16w(0x0bc5, 8);
        d16w(0x0bb6, 0x5a);
        Menu_DrawAttractVehicle();
        R.si = 0x03e8;
        R.bx = 0xa2;
        R.dx = 0x0b40;
        Text_DrawCentred();
        Gfx_Present();
        goto wait_key;
    }
    if (cmpb(d8(0x28c1), 0)) {
        if (cmpb(d8(0x03f8), 1)) return;
        Menu_DrawBackdrop();
        R.si = 0x035c;
        R.bx = 0x32;
        R.dx = 0x0b54;
        R.ax = 0x58;
        Text_Draw();
        R.si = 0x0367;
        R.bx = 0x46;
        R.dx = 0x0b54;
        R.ax = 0x70;
        Text_Draw();
        SET_CL(d8(0x28bf));
        SET_CL(CL - 1);   /* dec: CF kept */
        SET_CH(0);
        logicb(0);
        d16w(0x0bc5, R.cx);
        d16w(0x0bb6, 0x5a);
        Menu_DrawAttractVehicle();
        Gfx_Present();
        Pal_FadeIn();
        goto wait_key;
    }
    Menu_DrawRacers();
    R.bx = 0x6c;
    Menu_DrawTrackName();
    SET_CL(d8(0x28bf) - 1);
    d16w(0x0bc5, R.cx);   /* CH not cleared before the store, as the original */
    SET_CH(0);
    logicb(0);
    uint16_t cx = R.cx;
    d16w(0x0bb6, 0x80);
    Menu_DrawAttractVehicle();
    Gfx_Present();
    Pal_FadeIn();
    R.cx = cx;
    R.bx = 0x0cdb;
    R.ax = 0x0100;
    do {
        d16w((uint16_t)(R.bx + 0x13), R.cx);
        d16w((uint16_t)(R.bx + 2), R.ax);
        d16w((uint16_t)(R.bx + 4), 0x5a);
        d8w((uint16_t)(R.bx + 0x0a), 1);
        R.cx = (uint16_t)(R.cx + 8);
        R.ax = (uint16_t)(R.ax + 0x40);
        R.bx = (uint16_t)(R.bx + 0x1b);
        cmpw(R.bx, 0x0d2c);
    } while ((int16_t)R.bx <= (int16_t)0x0d2c);
    if (!cmpb(d8(0x03f8), 0)) {
        d16w(0x0cdd, 0xffe0);
        d16w(0x0cf8, 0x0100);
        d8w(0x0ce5, 0);
    }
    for (;;) {
        /* 131f: one tick of [261f] */
        wait_ticks(0x261f, 1);
        R.bx = 0x0cdb;
        Gfx_DrawObjectSaved();
        R.bx = 0x0cf6;
        Gfx_DrawObjectSaved();
        if (cmpb(d8(0x03f8), 0)) {
            R.bx = 0x0d11;
            Gfx_DrawObjectSaved();
            R.bx = 0x0d2c;
            Gfx_DrawObjectSaved();
        }
        R.ax = d16(0x0cdf);
        R.cx = 0x20;
        Gfx_PresentRows();
        if (!cmpb(d8(0x03f8), 0)) {
            R.bx = 0x0cdb;
            if (cmpw(d16((uint16_t)(R.bx + 2)), 0x54)) break;
            Gfx_RestoreUnder();
            add_mw((uint16_t)(R.bx + 2), 2);
            R.bx = 0x0cf6;
            Gfx_RestoreUnder();
            sub_mw((uint16_t)(R.bx + 2), 2);
            continue;
        }
        cmpw(d16(0x0cdd), 0x10);
        if ((int16_t)d16(0x0cdd) <= 0x10) break;
        R.bx = 0x0d2c;
        do {
            cx = R.cx;
            Gfx_RestoreUnder();
            R.cx = cx;
            sub_mw((uint16_t)(R.bx + 2), 2);
            R.bx = (uint16_t)(R.bx - 0x1b);
            cmpw(R.bx, 0x0cdb);
        } while (!R.cf);
    }
wait_key:                      /* 1000:1395 */
    Menu_WaitKey();
    if (cmpb(d8(0x0f6a), 0)) return;
    SET_AH(d8(0x28c1));
    SET_AL(d8(0x107e));
    if (cmpb(AL, 0x4e)) {
        if (cmpb(AH, 0x19)) return;
        SET_AH(AH + 1);
    } else if (cmpb(AL, 0x4a)) {
        if (AH != 0) SET_AH(AH - 1);
        else SET_AH(d8(0x0439));
    } else {
        return;
    }
    d8w(0x28c1, AH);
    SET_BL(AH);
    race_from_list();
    goto again;
}

/* First or second (not second at race 19h): the player goes on. The compares' flags are left for the
   next call, as the original's. */
static bool player_placed(void)
{
    if (cmpw(d16(0x03fc), 0x0c03)) return true;
    if (cmpb(d8(0x28c1), 0x19)) return false;
    return cmpw(d16(0x03fe), 0x0c03);
}

/* The results of a championship race (Menu_RaceResults, below): RESULTS! ([036c]) and the track name;
   four boxes (object 0bcd frames 0..3) in a 2 x 2 grid, the racers in order put in them (first top
   left, second top right, third and fourth below); the small pictures (0cdb..0d2c) set off-screen at
   both sides, framed, given the frame of their racer and theme, mirrored alternately; presented, faded
   in. Then each racer's picture slides into its box two pixels a tick (the left ones rightwards to 7,
   the right ones leftwards to c6h); its face and name drawn, and under the player's character QUALIFY
   ([039a]) for a first or second place (not at race 19h) or FAILED ([03a2]), at x 4 or bch. The places
   marked in the frame words (first 200h, third 100h, fourth 300h), the first and the last flashing
   until fire or a key (blinks of 0fh ticks) or 700 ticks of [261f]. */

/* Menu_RaceResults 1000:13e4
   After a race of the series; carry = no lives left (the series ends). Head to head: a loss costs a
   life (the lives screen, message 2). Championship: tune 8 for a first or second place (except at race
   19h), else 6; the results screen (above). Third or fourth (or second at race 19h): a life gone
   (message 2), carry if none are left. Else every third race ([0310] mod 3 = 0) an opponent is knocked
   out while any character of the roster [0164..016e] is neither out nor picked: at race 3 the one with
   the lowest character number (the last of equals), later the next object after [0346] in turn. */
void Menu_RaceResults(void)
{
    d8w(0x107f, 0);
    d8w(0x107e, 0);
    if (!cmpb(d8(0x03f8), 0)) {
        if (cmpw(d16(0x03fc), 0x0c03)) {
            R.cf = false;
            return;
        }
        R.cx = 2;
        Menu_LivesMessage();
        R.cf = d8(0x0406) == 0;
        return;
    }
    SET_AL(8);
    if (!player_placed()) SET_AL(6);
    play_tune(AL);
    Menu_DrawBackdrop();
    R.si = 0x036c;
    R.bx = 0x25;
    R.dx = 0x0b54;
    Text_DrawCentred();
    R.bx = 0x34;
    Menu_DrawTrackName();
    R.bx = 0x0bcd;
    d16w((uint16_t)(R.bx + 0x13), 0);
    d16w((uint16_t)(R.bx + 2), 7);
    d16w((uint16_t)(R.bx + 4), 0x47);
    Gfx_DrawObject();
    d16w((uint16_t)(R.bx + 4), 0x8f);
    d16w((uint16_t)(R.bx + 0x13), 2);
    Gfx_DrawObject();
    d16w((uint16_t)(R.bx + 2), 0xc6);
    d16w((uint16_t)(R.bx + 4), 0x47);
    d16w((uint16_t)(R.bx + 0x13), 1);
    Gfx_DrawObject();
    d16w((uint16_t)(R.bx + 4), 0x8f);
    d16w((uint16_t)(R.bx + 0x13), 3);
    Gfx_DrawObject();
    R.bx = d16(0x03fc);
    d16w((uint16_t)(R.bx + 2), 0x3f);
    d16w((uint16_t)(R.bx + 4), 0x47);
    R.bx = d16(0x03fe);
    d16w((uint16_t)(R.bx + 2), 0x86);
    d16w((uint16_t)(R.bx + 4), 0x47);
    R.bx = d16(0x0400);
    d16w((uint16_t)(R.bx + 2), 0x3f);
    d16w((uint16_t)(R.bx + 4), 0x8f);
    R.bx = d16(0x0402);
    d16w((uint16_t)(R.bx + 2), 0x86);
    d16w((uint16_t)(R.bx + 4), 0x8f);
    d16w(0x0cdd, 0xffe0);
    d16w(0x0cf8, 0x0120);
    d16w(0x0d13, 0xffe0);
    d16w(0x0d2e, 0x0120);
    d16w(0x0cdf, 0x6f);
    d16w(0x0cfa, 0x6f);
    d16w(0x0d15, 0xb7);
    d16w(0x0d30, 0xb7);
    R.si = 0x03fc;
    R.di = 0x0cdb;
    SET_CH(0);
    logicb(0);
    do {
        R.bx = d16(R.si);
        R.ax = 0x0f;
        Gfx_FrameObject();
        SET_CL(d8(0x28bf) - 1);
        if (!cmpw(R.bx, 0x0c03)) {
            SET_CL(CL + 8);
            if (!cmpw(R.bx, 0x0c1e)) {
                SET_CL(CL + 8);
                if (!cmpw(R.bx, 0x0c39)) SET_CL(CL + 8);
            }
        }
        d8w((uint16_t)(R.di + 0x13), CL);
        d8w((uint16_t)(R.di + 0x0a), CH);
        SET_CH(CH ^ 1);
        R.di = (uint16_t)(R.di + 0x1b);
        R.si = (uint16_t)(R.si + 2);
        cmpw(R.si, 0x0402);
    } while ((int16_t)R.si <= (int16_t)0x0402);
    Gfx_Present();
    Pal_FadeIn();
    R.si = 0x03fc;
    R.bx = 0x0cdb;
    for (;;) {
        /* 154f: the next racer's picture slides into its box */
        R.di = d16(R.si);
        uint16_t s_bx = R.bx, s_di = R.di, s_si = R.si, racer = R.di;
        R.ax = d16((uint16_t)(R.bx + 2));
        for (;;) {
            wait_ticks(0x0002, 1);
            d16w((uint16_t)(R.bx + 2), R.ax);
            uint16_t x = R.ax;
            Gfx_DrawObjectSaved();
            R.ax = d16((uint16_t)(R.bx + 4));
            R.cx = 0x10;
            Gfx_PresentRows();
            Gfx_RestoreUnder();
            R.ax = x;
            if (cmpw(R.bx, 0x0cdb) || cmpw(R.bx, 0x0d11)) {
                R.ax = (uint16_t)(R.ax + 2);
                cmpw(R.ax, 7);
                if ((int16_t)R.ax < 7) continue;
            } else {
                R.ax = (uint16_t)(R.ax - 2);
                cmpw(R.ax, 0xc6);
                if (R.ax > 0xc6) continue;
            }
            break;
        }
        Gfx_DrawObject();
        R.bx = racer;
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) & 0x0f);
        logicw(d16((uint16_t)(R.bx + 0x13)));
        Menu_DrawFace();
        Menu_DrawFaceName();
        R.ax = d16((uint16_t)(R.bx + 0x13));
        if (cmpw(R.ax, d16(0x0c16))) {
            R.si = 0x039a;
            if (!cmpw(d16(0x03fc), R.bx)) {
                if (cmpb(d8(0x28c1), 0x19) || !cmpw(d16(0x03fe), R.bx)) R.si = 0x03a2;
            }
            R.ax = 4;
            if (!cmpw(d16(0x03fc), R.bx) && !cmpw(d16(0x0400), R.bx)) R.ax = 0xbc;
            R.bx = d16((uint16_t)(R.bx + 4));
            R.bx = addw(R.bx, 0x20);
            R.dx = 0x0b40;
            Text_Draw();
        }
        Gfx_Present();
        R.si = s_si;
        R.di = s_di;
        R.bx = s_bx;
        R.di = (uint16_t)(R.di + 0x1b);
        R.bx = (uint16_t)(R.bx + 0x1b);
        R.si = (uint16_t)(R.si + 2);
        cmpw(R.si, 0x0402);
        if ((int16_t)R.si > (int16_t)0x0402) break;
    }
    R.bx = d16(0x03fc);
    d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) | 0x200);
    R.bx = d16(0x0400);
    d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) | 0x100);
    R.bx = d16(0x0402);
    d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) | 0x300);
    d16w(0x261f, 0);
    for (;;) {
        /* 161e: the first and the last flash */
        R.bx = d16(0x03fc);
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 0x10);
        R.bx = d16(0x0402);
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 0x10);
        logicw(d16((uint16_t)(R.bx + 0x13)));
        R.bx = 0x0c03;
        do {
            Menu_DrawFace();
            R.bx = (uint16_t)(R.bx + 0x1b);
            cmpw(R.bx, 0x0c54);
        } while ((int16_t)R.bx <= (int16_t)0x0c54);
        Gfx_Present();
        cmpw(d16(0x261f), 0x2bc);
        if (!R.cf) break;
        R.cx = 0x0f;
        Menu_WaitKeyBlink();
        if (!R.cf) break;
    }
    if (!player_placed()) {
        R.cx = 2;
        Menu_LivesMessage();
        R.cf = d8(0x0406) == 0;
        return;
    }
    /* every third race since the qualification: someone is knocked out */
    SET_AL(d8(0x0310));
    SET_AH(0);
    SET_CL(3);
    R.ax = (uint16_t)((AL % 3) << 8 | (AL / 3));
    if (AH != 0) {
        R.cf = false;
        return;
    }
    if (cmpb(d8(0x28c1), 3)) {
        R.ax = 0x63;
        R.bx = 0x0c1e;
        R.cx = R.bx;
        do {
            R.dx = d16((uint16_t)(R.bx + 0x13)) & 0x0f;
            if (!((int16_t)R.ax < (int16_t)R.dx)) {
                R.cx = R.bx;
                R.ax = R.dx;
            }
            R.bx = (uint16_t)(R.bx + 0x1b);
        } while ((int16_t)R.bx <= (int16_t)0x0c54);
        R.ax = R.cx;
        d16w(0x0346, R.ax);
    } else {
        R.ax = (uint16_t)(d16(0x0346) + 0x1b);
        if ((int16_t)R.ax > (int16_t)0x0c54) R.ax = 0x0c1e;
        d16w(0x0346, R.ax);
    }
    R.si = 0x0164;
    R.bx = 0;
    do {
        if (!(d8(R.si) & 0x60)) R.bx++;
        R.si++;
    } while ((int16_t)R.si < (int16_t)0x016f);
    if (R.bx != 0) {
        logicw(R.bx);
        Menu_RacerIsOut();
    }
    R.cf = false;
}

/* Menu_RacerIsOut 1000:16de
   AX = the racer object knocked out (0c1e..0c54): tune 6; its character's roster byte [0164 + n] gets
   20h (out) and its frame word 40h; the portraits, IS OUT!! ([03b6] at 80h) and the character's name
   (eight bytes each from [0258]) on row 64h; presented. The racer's picture becomes the frowning one
   ([0a3a], frame 2n) and hops off downwards: each step the next byte of the offsets at [034b] (to a 0)
   added to its row, its frame flipped, the picture clipped and its height cut by the offset, drawn,
   presented (rows 20h..5bh) and erased, then 9 ticks. Its frame word then 0bh (an empty place); a key
   wait and the opponents picked again. Registers kept (pusha / popa), the flags Menu_PickOpponents'. */
void Menu_RacerIsOut(void)
{
    Cpu saved = R;
    play_tune(6);
    R.bx = R.ax;
    R.ax = d16((uint16_t)(R.bx + 0x13)) & 0x0f;
    uint16_t n = R.ax, obj = R.bx;
    R.si = (uint16_t)(0x0164 + R.ax);
    d8w(R.si, d8(R.si) | 0x20);
    d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) | 0x40);
    Menu_DrawRacers();
    R.si = 0x03b6;
    R.bx = 0x64;
    R.dx = 0x0b54;
    R.ax = 0x80;
    Text_Draw();
    R.ax = (uint16_t)(n << 3);
    R.si = (uint16_t)(R.ax + 0x0258);
    R.ax = 0x48;
    R.bx = 0x64;
    R.dx = 0x0b54;
    Text_Draw();
    Gfx_Present();
    R.bx = obj;
    R.ax = (uint16_t)(n << 1);
    R.si = d16(0x0a3a);
    d16w((uint16_t)(R.bx + 8), R.si);
    d16w((uint16_t)(R.bx + 0x13), R.ax);
    R.si = 0x034b;
    R.cx = d16((uint16_t)(R.bx + 4));
    for (;;) {
        SET_AL(d8(R.si));
        R.si++;
        SET_AH(0);
        R.cx = (uint16_t)(R.cx + R.ax);
        d16w((uint16_t)(R.bx + 4), R.cx);
        R.cx = (uint16_t)(R.cx - R.ax);
        if (R.ax == 0) break;
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 1);
        uint16_t s_cx = R.cx, s_si = R.si, s_bx = R.bx, s_ax = R.ax;
        Gfx_ClipObject();
        R.ax = s_ax;
        d8w((uint16_t)(R.bx + 0x19), (uint8_t)(d8((uint16_t)(R.bx + 0x19)) - AL));
        Gfx_DrawObjectMasked();
        R.cx = 0x3c;
        R.ax = 0x20;
        Gfx_PresentRows();
        Gfx_RestoreUnder();
        d16w(0x0002, 0);
        while ((int16_t)d16(0x0002) < 9) mach_frame();
        R.bx = s_bx;
        R.si = s_si;
        R.cx = s_cx;
    }
    d16w((uint16_t)(R.bx + 0x13), 0x0b);
    Gfx_Present();
    Menu_WaitKey();
    Menu_PickOpponents();
    bool cf = R.cf, zf = R.zf, sf = R.sf;
    uint16_t ds = R.ds, es = R.es;
    R = saved;
    R.ds = ds, R.es = es;
    R.cf = cf, R.zf = zf, R.sf = sf;
}

/* Menu_WaitKey 1000:179b
   A key wait of the menus: [1080] (the input record they read) 0 meanwhile and restored after; the tick
   counter [0002] and the keys cleared. First while fire (bit 8 of [108b]) is held: a tick, the input
   read; then until fire is pressed: a tick, the input read. Any key released ([107e]) or 700 ticks end
   it at once (the second part reads the counter as cs:93c2, the same word 193c:0002). CF = 0. */
void Menu_WaitKey(void)
{
    uint16_t input = d16(0x1080);
    d16w(0x0002, 0);
    d16w(0x1080, 0);
    d8w(0x107f, 0);
    d8w(0x107e, 0);
    for (;;) {
        if (d16(0x0002) >= 0x2bc) goto done;
        wait_tick();
        Input_Read();
        if (d8(0x107e) != 0) goto done;
        if (!(d8(0x108b) & 8)) break;
    }
    for (;;) {
        if (c16(0x93c2) >= 0x2bc) goto done;
        wait_tick();
        Input_Read();
        if (d8(0x107e) != 0) goto done;
        if (d8(0x108b) & 8) break;
    }
done:
    d16w(0x1080, input);
    R.cf = false;
}

/* Menu_WaitKeyBlink 1000:17ff
   CX = ticks. [1080] 0 meanwhile and restored; [0002] and the keys cleared; up to CX ticks (signed
   compare, CX as the input routine leaves it) for fire to be released, then up to CX more counted from
   the same start for it to be pressed. Carry = the time ran out (the caller flips its blinking picture
   and calls again); clear = fire or a key released ([107e]). */
void Menu_WaitKeyBlink(void)
{
    uint16_t input = d16(0x1080);
    d16w(0x1080, 0);
    d16w(0x0002, 0);
    d8w(0x107e, 0);
    d8w(0x107f, 0);
    for (;;) {
        wait_tick();
        Input_Read();
        if (d8(0x107e) != 0) goto key;
        if (!(d8(0x108b) & 8)) break;
        if ((int16_t)d16(0x0002) > (int16_t)R.cx) goto timeout;
    }
    for (;;) {
        wait_tick();
        Input_Read();
        if (d8(0x107e) != 0) goto key;
        if (d8(0x108b) & 8) goto key;
        if ((int16_t)d16(0x0002) > (int16_t)R.cx) goto timeout;
    }
timeout:
    d16w(0x1080, input);
    R.cf = true;
    return;
key:
    d16w(0x1080, input);
    R.cf = false;
}

/* Menu_DrawTrackName 1000:1867
   BX = the row. The name of the track: entry (theme - 1) * 4 + track - 1 of the zero-separated list at
   [0460], centred in the 255 pixels right of 40h by its length (eight pixels a character, the
   terminator counted), in font 0b54, with RACE nn ([03ae], the digits of [28c1] written at [03b3]) 40h
   to its left. Before the last race ([28c1] = [0439]) the name alone, centred (Text_DrawNth, AX = FFFFh)
   -- except for the first track of the first theme (entry 0), where the check is skipped and RACE nn
   is drawn too (as the original). */
void Menu_DrawTrackName(void)
{
    SET_CL(d8(0x28c0));
    SET_CH(d8(0x28bf));
    SET_CH(CH - 1);
    SET_CL(CL - 1);
    SET_CH(CH << 2);
    SET_CL(CL + CH);
    SET_CH(0);
    R.si = 0x0460;
    if (R.cx != 0) {
        SET_AL(d8(0x0439));
        if (d8(0x28c1) == AL) {
            R.ax = 0xffff;
            R.dx = 0x0b54;
            Text_DrawNth();
            return;
        }
        do {
            do {
                SET_AL(d8(R.si));
                R.si++;
            } while (AL != 0);
            SET_CL(CL - 1);
        } while (CL != 0);
    }
    uint16_t name = R.si;
    R.cx = R.si;
    do {
        SET_AL(d8(R.si));
        R.si++;
    } while (AL != 0);
    R.cx = (uint16_t)(R.cx - R.si);
    R.cx = (uint16_t)-R.cx;
    R.cx = (uint16_t)(R.cx << 3);
    R.ax = 0xff;
    R.cx = (uint16_t)(R.cx + 0x40);
    R.ax = (uint16_t)(R.ax - R.cx);
    R.ax = (uint16_t)(R.ax >> 1);
    R.ax = (uint16_t)(R.ax + 0x40);
    R.si = name;
    uint16_t x = R.ax, row = R.bx;
    R.dx = 0x0b54;
    Text_Draw();
    SET_AL(d8(0x28c1));
    R.si = 0x03b3;
    Text_TwoDigits();
    R.bx = row;
    R.ax = x;
    R.ax = (uint16_t)(R.ax - 0x40);
    R.dx = 0x0b54;
    R.si = 0x03ae;
    Text_Draw();
}

/* Menu_ProgressMap 1000:18d8
   Between the championship's races (not before race 0, not when [043a] = 0): the map of the races run
   so far, presented. Outside the bonus races the current race's marker (the last picture drawn, BX as
   the map leaves it) blinks, 23h ticks on and 23h off, until fire or a key, or 700 ticks of [261f].
   Before a bonus race (theme 9) a column of bonus pictures (object 0cdb, frames 20h.. two a row at x 95h
   and b5h, rows 8eh..beh) alternates with the map until fire or a key ([261f] is reset at each turn, so
   its 700-tick limit never ends this one, as the original). */
void Menu_ProgressMap(void)
{
    if (d8(0x28c1) == 0 || d8(0x043a) == 0) {
        R.cf = false;
        R.zf = true;
        return;
    }
    Menu_DrawBackdrop();
    Menu_DrawProgressMap();
    Gfx_Present();
    if (d8(0x28bf) != 9) {
        d16w(0x261f, 0);
        for (;;) {
            Gfx_Present();
            R.cx = 0x23;
            Menu_WaitKeyBlink();
            if (!R.cf) return;
            Gfx_RestoreUnder();
            Gfx_Present();
            R.cx = 0x23;
            Menu_WaitKeyBlink();
            if (!R.cf) return;
            Gfx_DrawObjectSaved();
            if (d16(0x261f) >= 0x2bc) {
                R.cf = false;
                return;
            }
        }
    }
    for (;;) {
        d16w(0x261f, 0);
        R.bx = 0x0cdb;
        R.cx = 0x8e;
        d16w((uint16_t)(R.bx + 0x13), 0x1f);
        do {
            uint16_t cx = R.cx;
            d16w((uint16_t)(R.bx + 0x13), (uint16_t)(d16((uint16_t)(R.bx + 0x13)) + 1));
            d16w((uint16_t)(R.bx + 2), 0x95);
            d16w((uint16_t)(R.bx + 4), R.cx);
            Gfx_DrawObjectSaved();
            d16w((uint16_t)(R.bx + 0x13), (uint16_t)(d16((uint16_t)(R.bx + 0x13)) + 1));
            d16w((uint16_t)(R.bx + 2), 0xb5);
            Gfx_DrawObjectSaved();
            R.cx = (uint16_t)(cx + 0x10);
        } while ((int16_t)R.cx <= (int16_t)0xbe);
        R.cx = 0x30;
        R.ax = 0x8e;
        Gfx_PresentRows();
        R.cx = 0x23;
        Menu_WaitKeyBlink();
        if (!R.cf) return;
        if (d16(0x261f) >= 0x2bc) {
            R.cf = false;
            return;
        }
        Menu_DrawProgressMap();
        R.cx = 0x30;
        R.ax = 0x8e;
        Gfx_PresentRows();
        R.cx = 0x23;
        Menu_WaitKeyBlink();
        if (!R.cf) return;
    }
}

/* Menu_DrawProgressMap 1000:198e
   The championship's map: the tile picture (CASE.MAP / CASE.CHR, descriptors 0ab4 and 0aa0) at
   back-buffer offset 21ffh; then for each race run, 0 to [28c1] - 1 (at least one), a small picture
   (object 0cdb, not mirrored) at the grid position of the word [0312 + 2i] (x * 8 + 0ch, y * 8 + 4eh)
   with the frame of race byte [043d + i]: (track - 1) * 8 + theme - 1. Flags: the last
   `cmp cl, [28c1]`. */
void Menu_DrawProgressMap(void)
{
    R.di = 0x21ff;
    R.si = 0x0ab4;
    R.bx = 0x0aa0;
    Gfx_DrawTileMap();
    R.si = 0x043c;
    R.cx = 0;
    do {
        R.si++;
        uint16_t cx = R.cx, si = R.si;
        R.bx = 0x0312;
        R.cx = (uint16_t)(R.cx << 1);
        R.bx = (uint16_t)(R.bx + R.cx);
        R.ax = d16(R.bx);
        R.bx = 0x0cdb;
        d8w((uint16_t)(R.bx + 0x0a), 0);
        SET_CL(AL);
        SET_CH(0);
        R.cx = (uint16_t)((R.cx << 3) + 0x0c);
        d16w((uint16_t)(R.bx + 2), R.cx);
        SET_CL(AH);
        SET_CH(0);
        R.cx = (uint16_t)((R.cx << 3) + 0x4e);
        d16w((uint16_t)(R.bx + 4), R.cx);
        SET_AL(d8(R.si));
        SET_CL(AL & 3);
        SET_CH(0);
        R.cx = (uint16_t)(R.cx << 3);
        SET_AL(AL >> 2);
        SET_AL(AL - 1);
        SET_AH(0);
        R.ax = (uint16_t)(R.ax + R.cx);
        d16w((uint16_t)(R.bx + 0x13), R.ax);
        Gfx_DrawObjectSaved();
        R.si = si;
        R.cx = cx;
        SET_CL(CL + 1);
    } while ((int8_t)CL < (int8_t)d8(0x28c1));
    R.cf = CL < d8(0x28c1);
    flags_zs8((uint8_t)(CL - d8(0x28c1)));
}

/* Menu_DrawRacers 1000:19f2
   The backdrop and the racers' portraits in a row at y = 24h: the four objects 0c03..0c54 from x = 8,
   40h apart, or in head to head 0c03 and 0c1e from x = 47h; each frame word masked to 4fh (the
   character, the flash and out bits), the face, a box (colour 0dh) and the name label. Flags: the last
   `cmp bx, dx`. */
void Menu_DrawRacers(void)
{
    Menu_DrawBackdrop();
    R.bx = 0x0c03;
    R.dx = 0x0c54;
    R.ax = 8;
    if (d8(0x03f8) != 0) {
        R.ax = 0x47;
        R.dx = 0x0c1e;
    }
    do {
        uint16_t dx = R.dx, bx = R.bx, ax = R.ax;
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) & 0x4f);
        d16w((uint16_t)(R.bx + 4), 0x24);
        d16w((uint16_t)(R.bx + 2), R.ax);
        Menu_DrawFace();
        R.ax = 0x0d;
        Gfx_FrameObject();
        Menu_DrawFaceName();
        R.ax = (uint16_t)(ax + 0x40);
        R.bx = (uint16_t)(bx + 0x1b);
        R.dx = dx;
    } while ((int16_t)R.bx <= (int16_t)R.dx);
    R.cf = R.bx < R.dx;
    flags_zs16((uint16_t)(R.bx - R.dx));
}

/* Menu_PickOpponents 1000:1a4a
   The portraits; [0404] = 0; for every racer object 0c03..0c54 whose frame word is 0bh (an empty place)
   the player picks a character for it (the pick-an-opponent prompt, SI = 0227, AX = FFFFh, [019e] = the
   object; Esc picks again), [0404] = the object filled, and the scan starts over; when none is empty,
   the press-a-key prompt. */
void Menu_PickOpponents(void)
{
    Menu_DrawRacers();
    d16w(0x0404, 0);
    for (;;) {
        R.bx = 0x0c03;
        while (d16((uint16_t)(R.bx + 0x13)) != 0x0b) {
            R.bx = (uint16_t)(R.bx + 0x1b);
            if ((int16_t)R.bx > (int16_t)0x0c54) {
                Menu_PressAnyKey();
                return;
            }
        }
        do {
            d16w(0x019e, R.bx);
            uint16_t bx = R.bx;
            R.si = 0x0227;
            R.ax = 0xffff;
            Menu_SelectCharacter();
            R.bx = bx;
        } while (R.cf);
        d16w(0x0404, R.bx);
    }
}

/* Race_BonusRace 1000:1a82
   A bonus race: [0343] = 1 for Race_RunOne (theme 9, track [0342] + 1), then cleared. Its result is the
   race's bonus flag [291d]: 1 gives EXTRA LIFE (message 3) and the next bonus track (up to [043b]),
   anything else NO BONUS (message 4). */
void Race_BonusRace(void)
{
    d8w(0x0343, 1);
    CALL_R(Race_RunOne);
    d8w(0x0343, 0);
    R.cx = 4;
    if (d8(0x291d) == 1) {
        R.cx = 3;
        SET_AL(d8(0x043b));
        if (d8(0x0342) != AL) d8w(0x0342, (uint8_t)(d8(0x0342) + 1));
    }
    Menu_LivesMessage();
}

/* Menu_ChampionScreen 1000:1aad
   BX = the winning racer object (kept in [0348]). Tune 3; the backdrop, the cup's base (object 0cc0
   frame 0 at 50h,5ah) and a filled bar (68h,62h, 4 x 30h, colour 0fh); the winner's happy picture
   ([0a26], frame 2n, its character n kept in [0344]) at x 68h. Then each turn: the winner drawn at the
   row AX (from 62h, one pixel up a turn to 36h once the texts are in place), its frame flipped every
   15 ticks of [0002] (which starts at 32h); CHAMPIONSHIP WINNER!! ([0384]) on row 26h at [03aa], from
   -0b0h two pixels a turn to 28h; the cup (frames 1..8, 8 pixels apart from row 62h); the winner's name
   on row a4h at [03ac], from 100h two pixels a turn to 68h; faded in, presented, the winner erased.
   Once both texts are in, a tick and the input read each turn ([1080] = 0) until any input bit
   ([108b]). The object's frame word restored. Until then nothing waits: the slide runs at the machine's
   speed, about three turns a frame on the reference (the port's mach_busy). */
void Menu_ChampionScreen(void)
{
    d16w(0x0348, R.bx);
    uint16_t bx = R.bx;
    play_tune(3);
    R.bx = bx;
    d16w(0x03aa, 0xff50);
    d16w(0x03ac, 0x0100);
    R.ax = d16((uint16_t)(R.bx + 0x13));
    uint16_t frame = R.ax, winner = R.bx;
    Menu_DrawBackdrop();
    R.bx = 0x0cc0;
    d16w((uint16_t)(R.bx + 4), 0x5a);
    d16w((uint16_t)(R.bx + 2), 0x50);
    d16w((uint16_t)(R.bx + 0x13), 0);
    Gfx_DrawObject();
    R.ax = 0x68;
    R.bx = 0x62;
    R.cx = 4;
    R.dx = 0x30;
    R.si = 0x0f;
    Gfx_FillRect();
    R.bx = winner;
    R.si = d16(0x0a26);
    d16w((uint16_t)(R.bx + 2), 0x68);
    d16w((uint16_t)(R.bx + 8), R.si);
    R.ax = d16((uint16_t)(R.bx + 0x13)) & 0x0f;
    d16w(0x0344, R.ax);
    R.ax = (uint16_t)(R.ax << 1);
    d16w((uint16_t)(R.bx + 0x13), R.ax);
    R.ax = 0x62;
    d16w(0x0002, 0x32);
    for (;;) {
        /* 1b2b */
        uint16_t l_ax = R.ax, l_bx = R.bx;
        if ((int16_t)d16(0x0002) >= 0x0f) {
            d16w(0x0002, 0);
            R.bx = d16(0x0348);
            d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 1);
        }
        R.bx = d16(0x0348);
        d16w((uint16_t)(R.bx + 4), R.ax);
        Gfx_DrawObjectSaved();
        R.bx = 0x26;
        R.cx = 0x10;
        R.ax = 0;
        Gfx_FillRows();
        R.ax = d16(0x03aa);
        R.bx = 0x26;
        R.dx = 0x0b54;
        R.si = 0x0384;
        Text_Draw();
        if (d16(0x03aa) != 0x28) d16w(0x03aa, (uint16_t)(d16(0x03aa) + 2));
        R.bx = 0x0cc0;
        d16w((uint16_t)(R.bx + 0x13), 1);
        d16w((uint16_t)(R.bx + 4), 0x62);
        Gfx_DrawObjectSaved();
        R.cx = 1;
        R.ax = 0x62;
        do {
            R.cx++;
            R.ax = (uint16_t)(R.ax + 8);
            d16w((uint16_t)(R.bx + 0x13), R.cx);
            d16w((uint16_t)(R.bx + 4), R.ax);
            uint16_t ax = R.ax, cx = R.cx;
            Gfx_DrawObject();
            R.cx = cx;
            R.ax = ax;
        } while ((int16_t)R.cx < 9);
        R.bx = 0xa4;
        R.cx = 0x10;
        R.ax = 0;
        Gfx_FillRows();
        R.si = (uint16_t)((d16(0x0344) << 3) + 0x0258);
        R.ax = d16(0x03ac);
        R.bx = 0xa4;
        R.dx = 0x0b54;
        Text_Draw();
        if (d16(0x03ac) != 0x68) d16w(0x03ac, (uint16_t)(d16(0x03ac) - 2));
        Cpu regs = R;
        Pal_FadeIn();
        R.ax = regs.ax, R.bx = regs.bx, R.cx = regs.cx, R.dx = regs.dx;
        R.si = regs.si, R.di = regs.di, R.bp = regs.bp;
        Gfx_Present();
        R.bx = l_bx;
        R.ax = l_ax;
        if (d16(0x03aa) != 0x28 || d16(0x03ac) != 0x68) {
            /* no wait while the texts slide in: a turn's drawing is the time; on the reference machine
               (the harness's) the 108 turns take about 34 frames */
            mach_busy(MACH_FRAME_COUNTS / 3);
            if ((int16_t)R.ax >= 0x36) R.ax--;
            uint16_t ax = R.ax;
            Gfx_RestoreUnder();
            R.ax = ax;
            continue;
        }
        uint16_t ax = R.ax;
        Gfx_RestoreUnder();
        d16w(0x1080, 0);
        wait_tick();
        Input_Read();
        R.ax = ax;
        if (d8(0x108b) != 0) break;
    }
    R.bx = winner;
    R.ax = frame;
    d16w((uint16_t)(R.bx + 0x13), R.ax);
    R.cf = false;
}

/* Menu_LivesMessage 1000:1c1b
   CX = the message of the zero-separated list at [081f] (font 0b54, centred on row 32h): 0 FAILED TO
   QUALIFY, 1 QUALIFIED (5, for head to head, when [03f8] is set), 2 a life lost, 3 EXTRA LIFE, 4 NO
   BONUS. The player's portrait at 68h,46h, happy (frame 2xxh) for odd messages, sad (3xxh) for even
   ones, its name and box; tune 8 (odd) or 6 (even). Messages 1, 2, 3, 5: LIVES nn ([0893] at 60h,8ch,
   font 0b40; the digits of [0406] written at [0899] and [089c]). For 2 and 3 the count rolls: [0406]
   decremented (2) or incremented (3), the old count at [0899] and the new at [089c] (or the reverse), and
   the second one drawn at a row moving two pixels a step from 8ch to c8h (2) or c8h to 8ch (3); a step
   every five ticks (Sys_WaitTick), the face's flash flipped every 0fh ticks (the counter starts at
   226h, so at once), faded in once ([0311]). Then until fire, a key or 700 ticks of [261f]: a tick and
   the input read each turn; releasing the key with scan code 1bh sets the lives to 0 (BX = 0 on
   return). The other messages: the face blinking (0fh ticks) until fire, a key or 700 ticks. */
void Menu_LivesMessage(void)
{
    d16w(0x261f, 0);
    if (d8(0x03f8) != 0 && R.cx == 1) R.cx = 5;
    d8w(0x107e, 0);
    d8w(0x107f, 0);
    uint16_t cx = R.cx;
    Menu_DrawBackdrop();
    R.cx = cx;
    R.bx = 0x0c03;
    d16w((uint16_t)(R.bx + 2), 0x68);
    d16w((uint16_t)(R.bx + 4), 0x46);
    R.ax = d16((uint16_t)(R.bx + 0x13)) & 0x0f;
    SET_AH(3);
    if (CL & 1) SET_AH(AH - 1);
    cx = R.cx;
    d16w((uint16_t)(R.bx + 0x13), R.ax);
    Menu_DrawFaceName();
    R.ax = 0x0d;
    Gfx_FrameObject();
    R.cx = cx;
    SET_AL(6);
    if (R.cx & 1) SET_AL(8);
    play_tune(AL);
    R.cx = cx;
    R.si = 0x081f;
    R.dx = 0x0b54;
    R.ax = 0xffff;
    R.bx = 0x32;
    Text_DrawNth();
    R.cx = cx;
    if (R.cx == 0 || R.cx == 4) goto blink;
    SET_AL(d8(0x0406));
    R.si = 0x089c;
    Text_TwoDigits();
    SET_AL(d8(0x0406));
    R.si = 0x0899;
    Text_TwoDigits();
    R.si = 0x0893;
    R.bx = 0x8c;
    R.dx = 0x0b40;
    R.ax = 0x60;
    Text_Draw();
    if (R.cx == 1 || R.cx == 5) goto blink;
    if (R.cx <= 2) {
        SET_AL(d8(0x0406));
        R.si = 0x089c;
        Text_TwoDigits();
        SET_AL(AL - 1);
        d8w(0x0406, AL);
        R.si = 0x0899;
        Text_TwoDigits();
    } else {
        SET_AL(d8(0x0406));
        R.si = 0x0899;
        Text_TwoDigits();
        SET_AL(AL + 1);
        d8w(0x0406, AL);
        R.si = 0x089c;
        Text_TwoDigits();
    }
    R.bx = 0x8c;
    R.ax = 2;
    R.dx = 0xc8;
    if ((int16_t)R.cx > 2) {
        uint16_t t = R.dx;
        R.dx = R.bx;
        R.bx = t;
        R.ax = (uint16_t)-R.ax;
    }
    d16w(0x261f, 0);
    d16w(0x0002, 0x0226);
    for (;;) {
        /* 1d2b */
        if (d16(0x261f) >= 0x2bc) {
            R.cf = false;
            return;
        }
        uint16_t s_cx = R.cx, s_dx = R.dx, s_ax = R.ax, s_bx = R.bx;
        if ((int16_t)d16(0x0002) > 0x0f) {
            d16w(0x0002, 0);
            R.bx = 0x0c03;
            d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 0x10);
            Menu_DrawFace();
        }
        R.ax = 0x90;
        R.dx = 0x0b40;
        R.si = 0x0899;
        R.bx = 0x8c;
        Text_Draw();
        R.bx = s_bx;
        if (R.bx != 0) {
            R.si = 0x089c;
            Text_Draw();
        }
        Cpu regs = R;
        R.cx = 5;
        do {
            uint16_t c = R.cx;
            Sys_WaitTick();
            R.cx = (uint16_t)(c - 1);
        } while (R.cx != 0);
        R.ax = regs.ax, R.bx = regs.bx, R.cx = regs.cx, R.dx = regs.dx;
        R.si = regs.si, R.di = regs.di, R.bp = regs.bp;
        Gfx_Present();
        if (d8(0x0311) != 1) {
            Pal_FadeIn();
            d8w(0x0311, 1);
        }
        R.dx = 0x10;
        R.cx = 8;
        R.si = 0;
        if (R.bx != 0) Gfx_FillRect();
        R.bx = 0x8c;
        Gfx_FillRect();
        R.bx = s_bx;
        R.ax = s_ax;
        R.dx = s_dx;
        R.cx = s_cx;
        if (R.bx != 0 && R.bx != R.dx) {
            R.bx = (uint16_t)(R.bx + R.ax);
            continue;
        }
        if (R.bx != 0 && R.cx == 3) d16w(0x0899, d16(0x089c));
        wait_tick();
        Input_Read();
        R.bx = 0;
        if (d8(0x107e) == 0x1b) {
            d8w(0x0406, 0);
            R.cf = false;
            return;
        }
        if (d8(0x107e) != 0 || (d8(0x108b) & 8)) {
            R.cf = false;
            return;
        }
    }
blink:                         /* 1000:1de5 */
    for (;;) {
        if (d16(0x261f) >= 0x2bc) {
            R.cf = false;
            return;
        }
        R.bx = 0x0c03;
        d16w((uint16_t)(R.bx + 0x13), d16((uint16_t)(R.bx + 0x13)) ^ 0x10);
        Menu_DrawFace();
        Gfx_Present();
        if (d8(0x0311) != 1) {
            Pal_FadeIn();
            d8w(0x0311, 1);
        }
        R.cx = 0x0f;
        Menu_WaitKeyBlink();
        if (!R.cf) return;
    }
}
