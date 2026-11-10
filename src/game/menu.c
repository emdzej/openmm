/* The front end's screens, part A (docs/re/frontend.md): the attract screens, SELECT GAME (one or two
   players), the one-player game screen, the two-way chooser, the menus' backdrop, the drivers' carousel
   with its faces and names, the handicap question and the press-a-key prompt.

   All of it runs with DS = 193c. The menus draw into the back buffer (6d78) through the drawing
   routines (sprite objects, text, fills) and present it, then wait for the timer: a loop on the word
   193c:0002 (or 261f), which the timer handler advances once a frame; the port keeps those loops with
   mach_frame() as their body. Input is the menus' byte [108b] that Input_Read builds (bit 3 fire, bit 4
   the second button, bit 6 right, bit 7 left) and the keyboard handler's last key released [107e]. */
#include "cpu.h"
#include "game.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

/* The data segment through DS, as the original addresses it. */
static inline uint8_t ds8(uint16_t o) { return mb(R.ds, o); }
static inline uint16_t ds16(uint16_t o) { return mw(R.ds, o); }
static inline void ds8w(uint16_t o, uint8_t v) { mbw(R.ds, o, v); }
static inline void ds16w(uint16_t o, uint16_t v) { mww(R.ds, o, v); }

/* pusha ... popa: the general registers back (SP is not modelled; flags as the body leaves them). */
static inline void popa(const Cpu *s)
{
    R.ax = s->ax, R.bx = s->bx, R.cx = s->cx, R.dx = s->dx;
    R.si = s->si, R.di = s->di, R.bp = s->bp;
}

/* The flags of the instructions whose results reach a call (the checker compares CF at every call):
   cmp a, b; add (returns the sum); and/or/xor/test (CF clear). */
static inline void f_cmp(uint16_t a, uint16_t b)
{
    R.cf = a < b;
    flags_zs16((uint16_t)(a - b));
}
static inline uint16_t f_add(uint16_t a, uint16_t b)
{
    uint16_t s = (uint16_t)(a + b);
    R.cf = s < a;
    flags_zs16(s);
    return s;
}
static inline void f_logic(uint16_t v)
{
    R.cf = false;
    flags_zs16(v);
}
static inline void f_logic8(uint8_t v)
{
    R.cf = false;
    flags_zs8(v);
}

/* mov ax, [0002] / cmp [0002], ax / je: one tick of the timer's counter 193c:0002. AX = the count read;
   the flags of the last compare. */
static void wait_tick2(void)
{
    R.ax = ds16(0x0002);
    while (ds16(0x0002) == R.ax) mach_frame();
    f_cmp(ds16(0x0002), R.ax);
}

/* push ax / mov al, n / mov ah, 9 / driver / or al, al / je / mov al, n / mov ah, 4 / driver / pop ax:
   asks the driver whether tune n is to be started (AH = 9) and starts it (AH = 4) if it answers AL != 0. */
static void ask_tune(uint8_t n)
{
    uint16_t ax = R.ax;
    SET_AL(n);
    SET_AH(9);
    mach_drv();
    f_logic8(AL);
    if (AL != 0) {
        SET_AL(n);
        SET_AH(4);
        mach_drv();
    }
    R.ax = ax;
}

/* Menu_Attract 1000:0100
   The attract screens: the engines silenced, the driver's tune stopped (AH = 7, 8, 6) and the title tune
   asked for; the four players' input bytes and the menus' input pointer [1080], the keyboard's last keys
   and [1082] cleared; LOGO.CHR (object 0b97) and the copyright line (string 193c:0010, font 0b54, row
   b7h); then the nine vehicle classes in turn ([0bc5] from -1 up, wrapping at 9), each faded in and shown
   for 118h ticks. Out: CF = 0 on player 1's fire ([137b] bit 3) or a key released other than Esc, CF = 1
   on Esc released. */
void Menu_Attract(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    R.es = R.ax;
    Snd_SilenceEngines();
    SET_AH(7);
    mach_drv();
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    ask_tune(1);
    R.ax = 0;
    f_logic(0);   /* xor ax, ax */
    ds8w(0x137b, AL);
    ds8w(0x14df, AL);
    ds8w(0x1643, AL);
    ds8w(0x17a7, AL);
    ds16w(0x1080, R.ax);
    ds8w(0x107e, 0);
    ds8w(0x107f, 0);
    ds8w(0x1082, 0);
    R.ax = 0;
    Gfx_ClearBuffer();
    R.bx = 0x0b97;
    Gfx_DrawObjectSaved();
    R.si = 0x0010;
    R.bx = 0x00b7;
    R.dx = 0x0b54;
    Text_DrawCentred();
    ds16w(0x0bc5, 0xffff);
    ds16w(0x0bb6, 0x0064);
    for (;;) {
        R.cx = (uint16_t)(ds16(0x0bc5) + 1);
        f_cmp(R.cx, 9);
        if ((int16_t)R.cx >= 9) {
            R.cx = 0;
            f_logic(0);
        }
        ds16w(0x0bc5, R.cx);
        ask_tune(1);
        Menu_DrawAttractVehicle();
        Gfx_Present();
        Pal_FadeIn();
        ds16w(0x0002, 0);
        do {
            wait_tick2();
            Input_Read();
            SET_AL(ds8(0x137b));
            if (AL & 8) {
                R.cf = false;
                return;
            }
            SET_AL(ds8(0x107e));
            if (AL == 1) {
                R.cf = true;
                return;
            }
            if (AL != 0) {
                R.cf = false;
                return;
            }
        } while ((int16_t)ds16(0x0002) < 0x118);
    }
}

/* Menu_DrawAttractVehicle 1000:01de
   INTRO.CHR (object 0bb2) at x 50h, frame [0bc5], drawn opaquely; the 8 rows 40h below its y [0bb6]
   cleared and the class's name (entry [0bc5] of the list at 193c:002f, font 0b40) centred there.
   General registers kept. */
void Menu_DrawAttractVehicle(void)
{
    Cpu s = R;
    ds16w(0x0bb4, 0x0050);
    R.bx = 0x0bb2;
    Gfx_DrawObject();
    R.ax = 0;
    R.bx = f_add(ds16(0x0bb6), 0x40);
    R.cx = 8;
    Gfx_FillRows();
    R.cx = ds16(0x0bc5);
    R.si = 0x002f;
    R.ax = 0xffff;
    R.bx = f_add(ds16(0x0bb6), 0x40);
    R.dx = 0x0b40;
    Text_DrawNth();
    popa(&s);
}

/* Menu_SelectPlayers 1000:0220
   SELECT GAME: the title tune asked for, the backdrop's sub-heading [0156] and the Esc flag [1096]
   cleared; LOGO.CHR, the heading (string 0134, font 0b54, row 5eh), the two labels (0140, 014b at x 18h
   and 9ch, row 6fh), SELGAM.CHR's frames 0 and 1 (object 0c8a) and WORDS.CHR's 2 and 1 (0ca5) as the two
   choices. The chooser at row 80h starts on the last choice [0130]. 0 (Esc or the timeout): CF = 1.
   1: the one-player game screen, 2: the two-player game; CF = 0 after it. */
void Menu_SelectPlayers(void)
{
    ask_tune(1);
    ds8w(0x0156, 0);
    ds8w(0x1096, 0);
    R.ax = 0;
    f_logic(0);
    Gfx_ClearBuffer();
    R.bx = 0x0b97;
    Gfx_DrawObjectSaved();
    R.si = 0x0134;
    R.bx = 0x005e;
    R.dx = 0x0b54;
    Text_DrawCentred();
    R.si = 0x0140;
    R.bx = 0x006f;
    R.dx = 0x0b40;
    R.ax = 0x0018;
    Text_Draw();
    R.si = 0x014b;
    R.bx = 0x006f;
    R.dx = 0x0b40;
    R.ax = 0x009c;
    Text_Draw();
    R.bx = 0x0c8a;
    ds16w((uint16_t)(R.bx + 4), 0x0076);
    ds16w((uint16_t)(R.bx + 2), 0x0010);
    ds16w((uint16_t)(R.bx + 0x13), 0);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 2), 0x0094);
    ds16w((uint16_t)(R.bx + 0x13), 1);
    Gfx_DrawObject();
    R.bx = 0x0ca5;
    ds16w((uint16_t)(R.bx + 4), 0x00b7);
    ds16w((uint16_t)(R.bx + 2), 0x0018);
    ds16w((uint16_t)(R.bx + 0x13), 2);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 2), 0x009c);
    ds16w((uint16_t)(R.bx + 0x13), 1);
    Gfx_DrawObject();
    Gfx_Present();
    R.bx = 0x0080;
    R.ax = ds16(0x0130);
    Menu_ChooseOfTwo();
    if (R.cx == 0) {
        R.cf = true;
        return;
    }
    ds16w(0x0130, R.cx);
    f_cmp(R.cx, 2);
    if (R.cx == 2)
        CALL_R(Menu_TwoPlayerGame);   /* a game left with Esc ends in Main_Restart */
    else
        CALL_R(Menu_OnePlayerGame);
    R.cf = false;
}

/* Menu_OnePlayerGame 1000:02e0
   The one-player game screen: the backdrop, three lines of text (0140 at 58h,32h; 013b at 70h,44h; 0134
   at 58h,b6h; font 0b54), SELGAM.CHR's frames 2 and 3 and WORDS.CHR's 1 and 2 as the two game types; the
   chooser at row 6eh from the last choice [0132]. The choice is remembered in [0132] and becomes the
   backdrop's sub-heading [0156] (0 too: Esc or the timeout forget the last choice, as the original);
   the drivers reset and the backdrop redrawn; 1: head to head, 2: the championship, 0: back. */
void Menu_OnePlayerGame(void)
{
    Menu_DrawBackdrop();
    R.bx = 0x0032;
    R.ax = 0x0058;
    R.dx = 0x0b54;
    R.si = 0x0140;
    Text_Draw();
    R.bx = 0x0044;
    R.ax = 0x0070;
    R.dx = 0x0b54;
    R.si = 0x013b;
    Text_Draw();
    R.bx = 0x00b6;
    R.ax = 0x0058;
    R.dx = 0x0b54;
    R.si = 0x0134;
    Text_Draw();
    R.bx = 0x0c8a;
    ds16w((uint16_t)(R.bx + 4), 0x0058);
    ds16w((uint16_t)(R.bx + 2), 0x0010);
    ds16w((uint16_t)(R.bx + 0x13), 2);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 2), 0x0094);
    ds16w((uint16_t)(R.bx + 0x13), 3);
    Gfx_DrawObject();
    R.bx = 0x0ca5;
    ds16w((uint16_t)(R.bx + 4), 0x0099);
    ds16w((uint16_t)(R.bx + 2), 0x0018);
    ds16w((uint16_t)(R.bx + 0x13), 1);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 2), 0x00a4);
    ds16w((uint16_t)(R.bx + 0x13), 2);
    Gfx_DrawObject();
    Gfx_Present();
    R.bx = 0x006e;
    R.ax = ds16(0x0132);
    Menu_ChooseOfTwo();
    ds16w(0x0132, R.cx);
    ds8w(0x0156, CL);
    uint16_t cx = R.cx;
    Menu_ResetCharacters();
    Menu_DrawBackdrop();
    R.cx = cx;
    f_cmp(CL, 1);
    if (CL == 1) {
        CALL_R(Menu_HeadToHeadGame);
        return;
    }
    f_cmp(CL, 2);
    if (CL == 2) {
        CALL_R(Menu_ChampionshipGame);
        return;
    }
}

/* Menu_ChooseOfTwo 1000:0382
   A choice of two with THUMB.CHR (object 0c6f) as the pointer: BX = the pointer's row (into [0c73]),
   AX = the choice it starts on (1 left, 2 right; its frame [0c82]). The menus read players 1 and 2
   together ([1080] = 0). Waits for fire to be let go, then each tick: left = 1, right = 2 (the pointer
   redrawn and the timeout restarted when it moves), fire with a choice returns CX = it; Esc or 7d0h
   ticks without a move return CX = 0. The pointer is drawn over a saved background, its 38h rows from
   row [0c71] (the object's +2) shown, the background put back. The fire wait clears every bit but fire
   in [108b] (and byte [108b], 8), as the original. CF = 0. */
void Menu_ChooseOfTwo(void)
{
    ds16w(0x0c73, R.bx);
    R.cx = 0;
    f_logic(0);
    ds16w(0x1080, R.cx);
    ds8w(0x107e, CL);
    R.cx = R.ax;
redraw:
    R.bx = 0x0c6f;
    ds16w((uint16_t)(R.bx + 0x13), R.cx);
    {
        uint16_t cx = R.cx;
        Gfx_DrawObjectSaved();
        R.ax = ds16((uint16_t)(R.bx + 2));
        R.cx = 0x0038;
        Gfx_PresentRows();
        Gfx_RestoreUnder();
        R.cx = cx;
    }
    do {
        wait_tick2();
        Input_Read();
        ds8w(0x108b, ds8(0x108b) & 8);
    } while (ds8(0x108b) != 0);
restart:
    ds16w(0x0002, 0);
    for (;;) {
        wait_tick2();
        Input_Read();
        if (ds16(0x0002) >= 0x07d0 || ds8(0x107e) == 1) {
            R.cx = 0;
            R.cf = false;
            R.zf = true;
            return;
        }
        SET_AL(ds8(0x108b));
        if (AL & 8) {
            f_logic(R.cx);   /* or cx, cx */
            if (R.cx == 0) goto redraw;
            return;
        }
        R.dx = 1;
        if (!(AL & 0x80)) {
            R.dx++;
            if (!(AL & 0x40)) continue;
        }
        f_cmp(R.cx, R.dx);
        if (R.cx == R.dx) goto restart;
        R.cx = R.dx;
        goto redraw;
    }
}

/* Menu_DrawBackdrop 1000:0400
   The menus' backdrop: the back buffer cleared to colour 0, BADGE.CHR (object 0b7c), WORDS.CHR's frame 0
   (the title) at 48h,8 and, when [0156] is set, frame [0156] 5ah to its right with an underline: rows
   1eh (colour 12h), 1fh-20h (0eh), 21h (12h). */
void Menu_DrawBackdrop(void)
{
    R.ax = 0;
    f_logic(0);
    Gfx_ClearBuffer();
    R.bx = 0x0b7c;
    Gfx_DrawObject();
    R.bx = 0x0ca5;
    ds16w((uint16_t)(R.bx + 2), 0x0048);
    ds16w((uint16_t)(R.bx + 4), 0x0008);
    ds16w((uint16_t)(R.bx + 0x13), 0);
    Gfx_DrawObject();
    SET_AL(ds8(0x0156));
    if (AL == 0) {
        R.cf = false;   /* or al, al */
        R.zf = true;
        R.sf = false;
        return;
    }
    SET_AH(0);
    R.bx = 0x0ca5;
    ds16w((uint16_t)(R.bx + 2), f_add(ds16((uint16_t)(R.bx + 2)), 0x5a));
    ds16w((uint16_t)(R.bx + 0x13), R.ax);
    Gfx_DrawObject();
    R.bx = 0x001e;
    R.ax = 0x0012;
    R.cx = 1;
    Gfx_FillRows();
    R.ax = 0x000e;
    R.bx = 0x001f;
    R.cx = 2;
    Gfx_FillRows();
    R.ax = 0x0012;
    R.bx = 0x0021;
    R.cx = 1;
    Gfx_FillRows();
}

/* Menu_SelectCharacter 1000:09e0
   The drivers' carousel ("who do you want to be"): AX = the slot to turn to (an index into the turn
   positions at 193c:016f; -1 keeps [0192]), SI = the prompt (into [019a], blinking at row 62h in font
   0b40), [019e] = the player's face object (0c03, 0c1e, 0c39, 0c54). The menus' tune (2) asked for. A
   driver the player had already picked goes back into the pool and the face is cleared. Waits for fire
   to be let go; while the driver in front ([0160]) is not a real one (> 0bh) the carousel turns on in
   the last direction [0162]. Then each tick: left/right turn it (0cd3) and restart the blink's clock;
   fire on a free driver (bit 6 clear in the pool 193c:0164) takes it: marked taken, the face shown happy
   and flashed five times (0fh ticks each), the driver stored for the face object's player ([2668],
   [266a] with the handicap question's 80h, [266c], [266e]) and the face shown normal. Out: AX = [0160],
   CF = 0; Esc: CF = 1 (CL = 2). */
void Menu_SelectCharacter(void)
{
    f_cmp(R.ax, 0xffff);
    if (R.ax != 0xffff) {
        R.bx = 0x016f;
        R.ax = (uint16_t)(R.ax << 1);
        R.bx = f_add(R.bx, R.ax);
        R.ax = ds16(R.bx);
        ds16w(0x0192, R.ax);
    }
    ds8w(0x1096, 0);
    ds16w(0x019a, R.si);
    ds16w(0x0194, 0x0b40);
    ds16w(0x0196, 0x0062);
    ask_tune(2);
    ds8w(0x107f, 0);
    ds8w(0x107e, 0);
    R.bx = ds16(0x019e);
    R.ax = ds16((uint16_t)(R.bx + 0x13));
    f_cmp(R.ax, 0x0a);
    if (R.ax <= 0x0a) {
        R.si = f_add(0x0164, R.ax);
        ds8w(R.si, AL);
        ds16w((uint16_t)(R.bx + 0x13), 0x000b);
        Gfx_DrawObject();
        Menu_DrawFaceName();
    }
    Menu_DrawCarousel();
    Gfx_Present();
    do {
        wait_tick2();
        Input_Read();
    } while (ds8(0x108b) & 8);
    f_logic8(0);   /* test byte [108b], 8 */
    Menu_DrawCarousel();
    Gfx_Present();
    for (;;) {
        f_cmp(ds16(0x0160), 0x0b);
        if ((int16_t)ds16(0x0160) <= 0x0b) break;
        R.bx = ds16(0x0162);
        Menu_TurnCarousel();
    }
restart:
    ds16w(0x0002, 0);
    for (;;) {
        Menu_BlinkMessage();
        wait_tick2();
        Input_Read();
        SET_AL(ds8(0x108b));
        SET_CL(2);
        if (ds8(0x107e) == 1) {
            R.cf = true;
            return;
        }
        SET_CL(3);
        if (AL & 8) {
            R.ax = ds16(0x0160);
            f_cmp(R.ax, 0x0a);
            if (R.ax > 0x0a) continue;
            break;
        }
        R.bx = 1;
        if (!(AL & 0x80)) {
            R.bx = (uint16_t)-R.bx;
            f_logic8(AL & 0x40);
            if (!(AL & 0x40)) continue;
        } else {
            f_logic8(AL & 0x80);
        }
        ds16w(0x0162, R.bx);
        Menu_TurnCarousel();
        goto restart;
    }
    R.si = (uint16_t)(0x0164 + R.ax);
    ds8w(R.si, ds8(R.si) | 0x40);
    f_logic8(ds8(R.si));
    Menu_DrawCarousel();
    SET_AH(2);
    R.bx = ds16(0x019e);
    ds16w((uint16_t)(R.bx + 0x13), R.ax);
    Menu_DrawFaceName();
    Gfx_Present();
    R.cx = 5;
    do {
        uint16_t cx = R.cx;
        ds16w((uint16_t)(R.bx + 0x13), ds16((uint16_t)(R.bx + 0x13)) ^ 0x10);
        f_logic(ds16((uint16_t)(R.bx + 0x13)));
        Menu_DrawFace();
        ds16w(0x0002, 0);
        R.ax = ds16((uint16_t)(R.bx + 4));
        R.cx = 0x0030;
        Gfx_PresentRows();
        while ((int16_t)ds16(0x0002) <= 0x0f) mach_frame();
        R.cx = (uint16_t)(cx - 1);
    } while (R.cx != 0);
    R.ax = ds16((uint16_t)(R.bx + 0x13)) & 0x0f;
    f_cmp(R.bx, 0x0c03);
    if (R.bx == 0x0c03) {
        ds16w(0x2668, R.ax);
        Cpu s = R;
        Menu_AskHandicap();
        ds16w(0x2668, ds16(0x2668) | R.ax);
        popa(&s);
    }
    f_cmp(R.bx, 0x0c1e);
    if (R.bx == 0x0c1e) {
        ds16w(0x266a, R.ax);
        Cpu s = R;
        Menu_AskHandicap();
        ds16w(0x266a, ds16(0x266a) | R.ax);
        popa(&s);
    }
    if (R.bx == 0x0c39) ds16w(0x266c, R.ax);
    f_cmp(R.bx, 0x0c54);
    if (R.bx == 0x0c54) ds16w(0x266e, R.ax);
    ds16w((uint16_t)(R.bx + 0x13), R.ax);
    {
        uint16_t ax = R.ax, bx = R.bx;
        Menu_DrawFace();
        R.ax = ds16((uint16_t)(R.bx + 4));
        R.cx = 0x0030;
        Gfx_PresentRows();
        R.bx = bx;
        R.ax = ax;
    }
    R.ax = ds16(0x0160);
    R.cf = false;
}

/* Menu_AskHandicap 1000:0b51
   AX = the driver picked. Only for drivers 0, 1, 2, and not in the championship ([2656] = 1) nor when
   player 2 has no control ([265a] = 6): the row 62h cleared, the question (string 01a0, 01b2 or 01c4
   at x 2ch, font 0b40) and its answer NO/YES (list 01d9 at x bch) from the driver's last answer
   (193c:01d6 + driver: 0 or 80h); left = NO, right = YES, fire or the second button confirm. The answer
   is stored back. Out: AX = 0 or 80h (the handicap flag of the driver's word); 0 when not asked.
   CF = 0. */
void Menu_AskHandicap(void)
{
    if (ds16(0x2656) == 1 || ds16(0x265a) == 6) goto none;
    R.si = 0x01a0;
    R.bx = 0x01d6;
    f_cmp(R.ax, 0);
    if (R.ax != 0) {
        R.si = 0x01b2;
        R.bx = 0x01d7;
        f_cmp(R.ax, 1);
        if (R.ax != 1) {
            R.si = 0x01c4;
            R.bx = 0x01d8;
            f_cmp(R.ax, 2);
            if (R.ax != 2) goto none;
        }
    }
    {
        Cpu s = R;
        R.ax = 0;
        R.bx = 0x0062;
        R.cx = 8;
        R.dx = 0x0100;
        R.si = 0;
        Gfx_FillRect();
        popa(&s);
    }
    SET_AL(ds8(R.bx));
    ds8w(0x01e0, AL);
    uint16_t bx = R.bx;
    R.bx = 0x0062;
    R.dx = 0x0b40;
    R.ax = 0x002c;
    Text_Draw();
    for (;;) {
        R.ax = 0x00bc;
        R.bx = 0x0062;
        R.cx = 8;
        R.dx = 0x001e;
        R.si = 0;
        Gfx_FillRect();
        SET_CL(ds8(0x01e0));
        if (CL == 0x80) SET_CL(1);
        SET_CH(0);
        f_logic8(0);
        R.si = 0x01d9;
        R.ax = 0x00bc;
        R.bx = 0x0062;
        R.dx = 0x0b40;
        Text_DrawNth();
        Gfx_Present();
        for (;;) {
            wait_tick2();
            Input_Read();
            SET_AL(ds8(0x108b));
            if (AL & 0x18) goto done;   /* the second button or fire */
            SET_AH(0);
            if (AL & 0x80) break;
            SET_AH(0x80);
            if (AL & 0x40) break;
        }
        f_logic8(0);   /* the test that found the direction */
        ds8w(0x01e0, AH);
    }
done:
    R.bx = bx;
    SET_AL(ds8(0x01e0));
    ds8w(R.bx, AL);
    SET_AH(0);
    R.cf = false;
    R.zf = true;
    return;
none:
    R.ax = 0;
    R.cf = false;
    R.zf = true;
}

/* Menu_PressAnyKey 1000:0c15
   The press-a-key prompt (string 0241, font 0b40, row 62h, blinking) until fire, a key released or
   2bch ticks of [261f]. CF = 0. */
void Menu_PressAnyKey(void)
{
    ds16w(0x261f, 0);
    ds16w(0x019a, 0x0241);
    ds16w(0x0194, 0x0b40);
    ds16w(0x0196, 0x0062);
    ds8w(0x107f, 0);
    ds8w(0x107e, 0);
    for (;;) {
        if (ds16(0x261f) >= 0x02bc) {
            R.cf = false;
            R.zf = ds16(0x261f) == 0x02bc;
            return;
        }
        f_cmp(ds16(0x261f), 0x02bc);
        Menu_BlinkMessage();
        wait_tick2();
        Input_Read();
        if (ds8(0x107e) != 0) break;
        SET_AL(ds8(0x108b));
        if (AL & 8) break;
    }
    R.cf = false;
    R.zf = false;
}

/* Menu_ShowClassName 1000:0c5d
   The 8 rows from bfh cleared and the vehicle class [09d8] (1-based) written centred there in font
   0b40: from the list at 193c:002f, or at 00ab while the blink flag [26cf] bit 0 is set; the rows shown.
   General registers kept. */
void Menu_ShowClassName(void)
{
    Cpu s = R;
    R.ax = 0;
    R.bx = 0x00bf;
    R.cx = 8;
    Gfx_FillRows();
    R.si = 0x002f;
    if (ds8(0x26cf) & 1) R.si = 0x00ab;
    R.ax = 0xffff;
    R.bx = 0x00bf;
    SET_CL(ds8(0x09d8) - 1);
    SET_CH(0);
    f_logic8(0);
    R.dx = 0x0b40;
    Text_DrawNth();
    R.ax = 0x00bf;
    R.cx = 8;
    Gfx_PresentRows();
    popa(&s);
}

/* Menu_BlinkMessage 1000:0c96
   The menus' message line: string [019a] (none if 0) in font [0194] at row [0196]; the line (8 rows for
   font 0b40, else 16) is cleared and the text drawn only while the blink flag [26cf] bit 0 is set (the
   timer handler toggles it every 32 frames), then the rows are shown. General registers kept. */
void Menu_BlinkMessage(void)
{
    Cpu s = R;
    R.dx = ds16(0x0194);
    R.cx = 8;
    if (R.dx != 0x0b40) R.cx = 0x10;
    R.si = ds16(0x019a);
    if (R.si == 0) {
        popa(&s);
        R.cf = false;   /* cmp si, 0 */
        R.zf = true;
        R.sf = false;
        return;
    }
    R.ax = 0;
    f_logic(0);
    R.bx = ds16(0x0196);
    uint16_t cx = R.cx;
    Gfx_FillRows();
    R.cx = cx;
    f_logic8(ds8(0x26cf) & 1);
    if (ds8(0x26cf) & 1) {
        R.dx = ds16(0x0194);
        cx = R.cx;
        Text_DrawCentred();
        R.cx = cx;
    }
    R.ax = ds16(0x0196);
    Gfx_PresentRows();
    popa(&s);
}

/* Menu_TurnCarousel 1000:0cd3
   Turns the carousel one slot (40h units of [0192], which wraps at 2c0h) towards BX's sign (positive:
   up), in the eased steps of the byte table at 193c:0185 until they add up to 40h; one tick of [261f]
   each, the carousel redrawn and its 51h rows from 76h shown. */
void Menu_TurnCarousel(void)
{
    R.si = 0x0185;
    R.cx = 0;
    do {
        SET_DL(ds8(R.si));
        R.si++;
        uint16_t si = R.si;
        SET_DH(0);
        R.cx = (uint16_t)(R.cx + R.dx);
        uint16_t cx = R.cx;
        R.ax = ds16(0x0192);
        if (!(R.bx & 0x8000)) {
            R.ax = (uint16_t)(R.ax + R.dx);
            if ((int16_t)R.ax >= 0x02c0) R.ax = (uint16_t)(R.ax - 0x02c0);
        } else {
            bool borrow = R.ax < R.dx;
            R.ax = (uint16_t)(R.ax - R.dx);
            if (borrow) R.ax = (uint16_t)(R.ax + 0x02c0);
        }
        ds16w(0x0192, R.ax);
        ds16w(0x261f, 0);
        while ((int16_t)ds16(0x261f) < 1) mach_frame();
        f_cmp(ds16(0x261f), 1);
        Menu_DrawCarousel();
        R.ax = 0x0076;
        R.cx = 0x0051;
        Gfx_PresentRows();
        R.cx = cx;
        R.si = si;
    } while ((int16_t)R.cx < 0x40);
    R.cf = R.cx < 0x40;   /* cmp cx, 40h */
    R.zf = R.cx == 0x40;
}

/* Menu_DrawCarousel 1000:0d1f
   The carousel window: bands of colour 0eh (rows 77h-80h), 12h (7eh), 0 (7fh-aeh), 12h (afh), 0eh
   (b0h-b7h); then the eleven drivers of the pool (bytes 193c:0164-016e, in order) on FCNORMAL.CHR's
   object 0be8 at x = their slot position - d8h, the positions [0192] + 40h n wrapping at 2c0h; the one at
   140h is the driver in front [0160]. Each face with its name (table 0258, row b0h) and rating (02b1,
   row 77h). Then the frame. General registers kept. */
void Menu_DrawCarousel(void)
{
    Cpu s = R;
    R.ax = 0x000e;
    R.bx = 0x0077;
    R.cx = 0x000a;
    Gfx_FillRows();
    R.ax = 0x0012;
    R.bx = 0x007e;
    R.cx = 1;
    Gfx_FillRows();
    R.ax = 0;
    R.bx = 0x007f;
    R.cx = 0x0030;
    Gfx_FillRows();
    R.ax = 0x0012;
    R.bx = 0x00af;
    R.cx = 1;
    Gfx_FillRows();
    R.ax = 0x000e;
    R.bx = 0x00b0;
    R.cx = 8;
    Gfx_FillRows();
    R.dx = ds16(0x0192);
    R.bx = 0x0be8;
    R.cx = 0x000b;
    R.si = 0x0164;
    do {
        uint16_t dx = R.dx;
        SET_AL(ds8(R.si));   /* lodsb */
        R.si = (uint16_t)(R.si + (R.df ? -1 : 1));
        uint16_t si = R.si;
        SET_AH(0);
        if (R.dx == 0x0140) ds16w(0x0160, R.ax);
        f_cmp(R.dx, 0x00d8);   /* sub dx, d8h */
        R.dx = (uint16_t)(R.dx - 0x00d8);
        ds16w((uint16_t)(R.bx + 0x13), R.ax);
        ds16w((uint16_t)(R.bx + 2), R.dx);
        Menu_DrawFace();
        R.cx = 0x00b0;
        R.ax = 0x0258;
        Menu_DrawFaceLabel();
        R.cx = 0x0077;
        R.ax = 0x02b1;
        Menu_DrawFaceLabel();
        R.si = si;
        R.dx = (uint16_t)(dx + 0x40);
        if ((int16_t)R.dx >= 0x02c0) R.dx = (uint16_t)(R.dx - 0x02c0);
    } while (R.si != 0x016f);
    f_cmp(R.si, 0x016f);
    Menu_DrawCarouselFrame();
    popa(&s);
}

/* Menu_DrawFace 1000:0db0
   Draws a face object (BX). Its frame word [BX+13]: low byte the driver (bits 0-3; bits 5 and 6 the
   taken markers, bit 4 the flash), high byte the expression. 0: FCNORMAL.CHR (record 0a50), the
   driver's frame, or 0ch / 0dh for bit 5 / bit 6; 1: record 0a3c, the low byte as it is; 2: record 0a14,
   frame 2 x driver + the flash bit (the low byte shifted left, bit 5 folded into bit 0, five bits kept);
   3 and up: record 0a28, the same frame. The object's image record [BX] and segment [BX+8] point at the
   expression's while drawing; [BX+13] is put back after. CX = the frame word, BX kept. */
void Menu_DrawFace(void)
{
    R.cx = ds16((uint16_t)(R.bx + 0x13));
    uint16_t bx = R.bx, cx = R.cx;
    if (CH == 0) {
        R.si = 0x0a50;
        if (CL & 0x20) SET_CL(0x0c);
        else if (CL & 0x40) SET_CL(0x0d);
    } else {
        R.si = 0x0a3c;
        SET_CH(CH - 1);
        if (CH != 0) {
            SET_CL(CL << 1);
            if (CL & 0x20) SET_CL(CL | 1);
            SET_CL(CL & 0x1f);
            R.si = 0x0a14;
            SET_CH(CH - 1);
            if (CH != 0) {
                R.si = 0x0a28;
                SET_CH(0);
            }
        }
    }
    R.cf = false;   /* every path's last CF: or, test, and or xor (the decs leave it) */
    ds16w((uint16_t)(R.bx + 0x13), R.cx);
    ds16w(R.bx, R.si);
    R.si = ds16((uint16_t)(R.si + 0x12));
    ds16w((uint16_t)(R.bx + 8), R.si);
    Gfx_DrawObject();
    R.cx = cx;
    R.bx = bx;
    ds16w((uint16_t)(R.bx + 0x13), R.cx);
}

/* Menu_DrawCarouselFrame 1000:0e02
   FRAME.CHR (object 0d47) around the carousel window: colour-12h lines at rows 76h and b7h; the corners
   (frames 2 and 1, plain at x 5ch and mirrored at 9ch, rows b6h and 6eh); seven pieces of frame 3 along
   the top (row 6eh, mirrored) and bottom (row b6h) from x 64h in steps of 8; eight pieces of frame 0
   down the sides (x 5ch plain, 9ch mirrored) from row 76h in steps of 8. */
void Menu_DrawCarouselFrame(void)
{
    R.bx = 0x0076;
    R.ax = 0x0012;
    R.cx = 1;
    Gfx_FillRows();
    R.ax = 0x0012;
    R.bx = 0x00b7;
    R.cx = 1;
    Gfx_FillRows();
    R.bx = 0x0d47;
    ds16w((uint16_t)(R.bx + 2), 0x009c);
    ds16w((uint16_t)(R.bx + 4), 0x00b6);
    ds16w((uint16_t)(R.bx + 0x13), 2);
    ds8w((uint16_t)(R.bx + 0x0a), 1);
    Gfx_DrawObject();
    ds8w((uint16_t)(R.bx + 0x0a), 0);
    ds16w((uint16_t)(R.bx + 2), 0x005c);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 4), 0x006e);
    ds16w((uint16_t)(R.bx + 0x13), 1);
    ds8w((uint16_t)(R.bx + 0x0a), 0);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 2), 0x009c);
    ds8w((uint16_t)(R.bx + 0x0a), 1);
    Gfx_DrawObject();
    ds16w((uint16_t)(R.bx + 0x13), 3);
    ds16w((uint16_t)(R.bx + 2), 0x005c);
    R.cx = 7;
    do {
        uint16_t cx = R.cx;
        ds16w((uint16_t)(R.bx + 2), f_add(ds16((uint16_t)(R.bx + 2)), 8));
        ds8w((uint16_t)(R.bx + 0x0a), 1);
        ds16w((uint16_t)(R.bx + 4), 0x006e);
        Gfx_DrawObject();
        ds16w((uint16_t)(R.bx + 4), 0x00b6);
        ds8w((uint16_t)(R.bx + 0x0a), 0);
        Gfx_DrawObject();
        R.cx = (uint16_t)(cx - 1);
    } while (R.cx != 0);
    R.cx = 8;
    R.bx = 0x0d47;
    ds16w((uint16_t)(R.bx + 0x13), 0);
    ds16w((uint16_t)(R.bx + 4), 0x0076);
    do {
        uint16_t cx = R.cx;
        ds16w((uint16_t)(R.bx + 2), 0x005c);
        ds8w((uint16_t)(R.bx + 0x0a), 0);
        Gfx_DrawObject();
        ds8w((uint16_t)(R.bx + 0x0a), 1);
        ds16w((uint16_t)(R.bx + 2), 0x009c);
        Gfx_DrawObject();
        ds16w((uint16_t)(R.bx + 4), f_add(ds16((uint16_t)(R.bx + 4)), 8));   /* the flags returned */
        R.cx = (uint16_t)(cx - 1);
    } while (R.cx != 0);
}

/* Menu_ResetCharacters 1000:0eba
   A new game's drivers: the one in front [0160] = 6, the turn [0192] = 0, [0406]-[0409] = 3, the pool
   193c:0164 = 0..10 (all free), the four players' picks [2668] [266a] [266c] [266e] and their face
   objects' frames [0c16] [0c31] [0c4c] [0c67] = 0bh (none). All registers kept. */
void Menu_ResetCharacters(void)
{
    Cpu s = R;
    R.ax = SEG_DATA;
    R.es = R.ax;
    R.ds = R.ax;
    ds16w(0x0160, 6);
    ds16w(0x0192, 0);
    SET_AL(3);
    ds8w(0x0406, AL);
    ds8w(0x0407, AL);
    ds8w(0x0408, AL);
    ds8w(0x0409, AL);
    R.di = 0x0164;
    R.cx = 0x000b;
    ds16w(0x2668, R.cx);
    ds16w(0x266a, R.cx);
    ds16w(0x266c, R.cx);
    ds16w(0x266e, R.cx);
    ds16w(0x0c16, 0x000b);
    ds16w(0x0c31, 0x000b);
    ds16w(0x0c4c, 0x000b);
    ds16w(0x0c67, 0x000b);
    SET_AL(0);
    R.cf = false;   /* xor al, al; the incs leave CF */
    do {
        mbw(R.es, R.di, AL);   /* stosb */
        R.di = (uint16_t)(R.di + (R.df ? -1 : 1));
        SET_AL(AL + 1);
        flags_zs8(AL);
        R.cx--;
    } while (R.cx != 0);
    R.ds = s.ds;
    R.es = s.es;
    popa(&s);
}

/* Menu_DrawFaceLabel 1000:0f17
   Unless the face object BX was not drawn ([BX+15] = -1): the 8-byte entry (driver = [BX+13] bits 0-3)
   of the table at AX (0258 the names, 02b1 the ratings) at x [0bea] (the carousel's face object's x),
   row CX, font 0b40. AX, BX, CX kept. */
void Menu_DrawFaceLabel(void)
{
    if (ds16((uint16_t)(R.bx + 0x15)) == 0xffff) {
        R.cf = false;   /* cmp [bx+15], -1 */
        R.zf = true;
        R.sf = false;
        return;
    }
    uint16_t ax = R.ax, bx = R.bx, cx = R.cx;
    R.bx = ds16((uint16_t)(R.bx + 0x13)) & 0x0f;
    R.bx = (uint16_t)(R.bx << 3);
    R.ax = f_add(R.ax, R.bx);
    R.si = R.ax;
    R.ax = ds16(0x0bea);
    R.bx = R.cx;
    R.dx = 0x0b40;
    Text_Draw();
    R.cx = cx;
    R.bx = bx;
    R.ax = ax;
}

/* Menu_DrawFaceName 1000:0f3c
   The driver's name (table 0258, entry [BX+13] bits 0-3) at the face object BX's x, 31h rows below its
   y, font 0b40; for no driver (0bh or more) that 30h x 8 strip is filled instead, with the colour the
   caller left in SI's low byte (as the original). General registers kept. */
void Menu_DrawFaceName(void)
{
    Cpu s = R;
    R.cx = ds16((uint16_t)(R.bx + 0x13));
    R.ax = ds16((uint16_t)(R.bx + 2));
    R.bx = (uint16_t)(ds16((uint16_t)(R.bx + 4)) + 0x31);
    R.cx &= 0x0f;
    f_cmp(R.cx, 0x0b);
    if ((int16_t)R.cx >= 0x0b) {
        R.cx = 8;
        R.dx = 0x0030;
        Gfx_FillRect();
    } else {
        R.cx = (uint16_t)(R.cx << 3);
        R.cx = f_add(R.cx, 0x0258);
        R.si = R.cx;
        R.dx = 0x0b40;
        Text_Draw();
    }
    popa(&s);
}
