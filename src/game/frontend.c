/* The front end's set-up screens (docs/re/frontend.md): GAME OPTIONS with SETTINGS.DAT, the credits,
   the joystick calibration, the data-set chooser (GAMEn.LVL) and REDEFINE KEYS.

   These screens wait for the keyboard handler's last key released [107e] in tight loops (no timer
   wait): the port keeps each loop with mach_frame() as its body, taken only while nothing has changed,
   so that the original's spin, which can only end when an interrupt moves [107e], ends at the same
   frame boundary. */
#include "cpu.h"
#include "game.h"
#include "machine.h"
#include "mem.h"
#include "routines.h"

static inline uint8_t ds8(uint16_t o) { return mb(R.ds, o); }
static inline uint16_t ds16(uint16_t o) { return mw(R.ds, o); }
static inline void ds8w(uint16_t o, uint8_t v) { mbw(R.ds, o, v); }
static inline void ds16w(uint16_t o, uint16_t v) { mww(R.ds, o, v); }

static inline void popa(const Cpu *s)
{
    R.ax = s->ax, R.bx = s->bx, R.cx = s->cx, R.dx = s->dx;
    R.si = s->si, R.di = s->di, R.bp = s->bp;
}

/* The flags of the instructions whose results reach a call or an int (the checker compares CF there). */
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
/* shr r, 1 after add r, si: the midpoint, CF = the bit shifted out */
static inline uint16_t f_mid(uint16_t a, uint16_t b)
{
    uint16_t s = (uint16_t)(a + b);
    R.cf = s & 1;
    s >>= 1;
    flags_zs16(s);
    return s;
}

/* The string instructions, with the direction flag. */
static inline int16_t str_step(int16_t n) { return R.df ? (int16_t)-n : n; }
static void lodsw(void)
{
    R.ax = mw(R.ds, R.si);
    R.si = (uint16_t)(R.si + str_step(2));
}
static void stosw(void)
{
    mww(R.es, R.di, R.ax);
    R.di = (uint16_t)(R.di + str_step(2));
}
static void movsw(void)
{
    mww(R.es, R.di, mw(R.ds, R.si));
    R.si = (uint16_t)(R.si + str_step(2));
    R.di = (uint16_t)(R.di + str_step(2));
}
static void movsb(void)
{
    mbw(R.es, R.di, mb(R.ds, R.si));
    R.si = (uint16_t)(R.si + str_step(1));
    R.di = (uint16_t)(R.di + str_step(1));
}
static void rep_movsw(void)
{
    while (R.cx != 0) {
        movsw();
        R.cx--;
    }
}

/* mov al, [107e] / or al, al / je: until a key is released. AL = its scan code. */
static void wait_key_released(void)
{
    for (;;) {
        SET_AL(ds8(0x107e));
        if (AL != 0) return;
        mach_frame();
    }
}

/* GAME OPTIONS, the first screen (and the one Esc on the attract screens goes back to). The joysticks
   counted (Joy_Detect). Once per run ([0eff]): the sound setting [0f64] defaults to 2 (1 if [262d] = 1),
   then SETTINGS.DAT (name at 193c:0ed0) is read, up to 28h bytes into 193c:0da8: if it gave any bytes,
   eight words go to player 1's and 2's control [0f5f] [0f61], the smoothness [263a], the sound [0f64],
   the joystick thresholds [28fd] [28ff] [2905] [2907], and sixteen bytes to the key table 193c:106c.
   The secret sequence restarts ([0f73] = 0f6b) and "changed" [0f63] is cleared.
   The screen: the backdrop, the heading (0f52), six lines of the list at 0e17 (a seventh, calibration,
   only with a joystick), three lines in font 0b40 at rows b0h-c0h; the settings' column (x c8h, rows
   34h-73h cleared) from the lists at 0f02 (controls), 0f3d (sound), 0f22 (smoothness); 0f00 when the
   debug flag [0f69] is set. Then a key released: Esc leaves with CF = 1; F6 the credits, F7 (with a
   joystick) the calibration, F5 redefine keys, each redrawing the whole screen after; F1/F2 step player
   1's/2's control (1 joystick A, 2 joystick B, 3 mouse, 4 keys 1, 5 keys 2): player 1 skips joystick B
   and the mouse always and joystick A without a joystick, player 2 skips the mouse without one ([2627]),
   joystick A without a joystick, B without two, and both skip the other player's; F3 reloads the sound
   driver of the current setting (before stepping it: as the original) and steps the setting 0..2; F4 the
   smoothness 1..5. Every key but these is also matched against the sequence of eight scan codes at
   193c:0f6b: the whole sequence sets [0f69] and [0f6a]. Enter: players 1 and 2's control types into
   [2658] [265a], 6 (computer) for 3 and 4, the input slots set; if anything was changed and the current
   drive is C: or later, SETTINGS.DAT is created and written (8 words and the 16 key codes); smoothness 5
   (auto) runs the speed test and clears the back buffer. CF = 0. */
/* Menu_GameOptions 1000:2770 */
void Menu_GameOptions(void)
{
    R.ax = SEG_DATA;
    R.ds = R.ax;
    Joy_Detect();
    f_cmp(ds8(0x0eff), 0);
    if (ds8(0x0eff) == 0) {
        R.bx = 2;
        f_cmp(ds16(0x262d), 1);
        if (ds16(0x262d) == 1) R.bx = 1;
        ds16w(0x0f64, R.bx);
        R.ax = 0x3d00;
        R.dx = 0x0ed0;
        mach_int(0x21);   /* open SETTINGS.DAT */
        if (!R.cf) {
            R.bx = R.ax;
            SET_AH(0x3f);
            R.cx = 0x0028;
            R.dx = 0x0da8;
            mach_int(0x21);
            uint16_t ax = R.ax;
            SET_AH(0x3e);
            mach_int(0x21);
            R.ax = ax;
            f_cmp(R.ax, 0);
            if (R.ax != 0) {
                static const uint16_t to[8] = {0x0f5f, 0x0f61, 0x263a, 0x0f64, 0x28fd, 0x28ff, 0x2905, 0x2907};
                R.si = 0x0da8;
                for (int i = 0; i < 8; i++) {
                    lodsw();
                    ds16w(to[i], R.ax);
                }
                uint16_t es = R.es;
                R.es = R.ds;
                R.di = 0x106c;
                R.cx = 8;
                rep_movsw();
                R.es = es;
            }
        }
    }
    ds8w(0x0eff, 1);
    ds16w(0x0f73, 0x0f6b);
    ds8w(0x0f63, 0);
screen:
    ds8w(0x0156, 0);
    Menu_DrawBackdrop();
    R.si = 0x0f52;
    R.bx = 0x0023;
    R.dx = 0x0b54;
    Text_DrawCentred();
    R.ax = 0;
    R.bx = 0x0034;
    R.si = 0x0e17;
    R.dx = 0x0b54;
    R.cx = 0;
    do {
        Text_DrawNth();
        R.bx = (uint16_t)(R.bx + 0x10);
        R.cx++;
        f_cmp(R.cx, 6);
    } while ((int16_t)R.cx < 6);
    f_cmp(ds16(0x2625), 0);
    if (ds16(0x2625) != 0) Text_DrawNth();
    R.cx++;   /* inc: CF stays */
    R.ax = 0xffff;
    R.bx = 0x00b0;
    R.dx = 0x0b40;
    Text_DrawNth();
    R.bx = f_add(R.bx, 8);
    R.cx++;
    R.dx = 0x0b40;
    Text_DrawNth();
    R.bx = f_add(R.bx, 8);
    R.cx++;
    R.dx = 0x0b40;
    Text_DrawNth();
values:
    R.ax = 0x00c8;
    R.bx = 0x0034;
    R.cx = 0x0040;
    R.dx = 0x003c;
    R.si = 0;
    Gfx_FillRect();
    R.bx = 0x0034;
    R.ax = 0x00c8;
    R.si = 0x0f02;
    R.cx = (uint16_t)(ds16(0x0f5f) - 1);
    R.dx = 0x0b54;
    Text_DrawNth();
    R.cx = (uint16_t)(ds16(0x0f61) - 1);
    R.bx = f_add(R.bx, 0x10);
    Text_DrawNth();
    R.cx = ds16(0x0f64);
    R.si = 0x0f3d;
    R.bx = f_add(R.bx, 0x10);
    Text_DrawNth();
    R.bx = f_add(R.bx, 0x10);
    R.si = 0x0f22;
    R.cx = ds16(0x263a);
    Text_DrawNth();
    R.bx = (uint16_t)(R.bx + 0x10);
    R.ax = 0;
    f_cmp(ds8(0x0f69), 1);
    if (ds8(0x0f69) == 1) {
        R.si = 0x0f00;
        Text_Draw();
    }
    Gfx_Present();
    ds8w(0x107f, 0);
    ds8w(0x107e, 0);
key:
    wait_key_released();
    if (AL == 1) {   /* Esc */
        R.cf = true;
        return;
    }
    f_cmp(AL, 0x40);
    if (AL == 0x40) {   /* F6 */
        Menu_Credits();
        goto screen;
    }
    if (AL == 0x41) {   /* F7 */
        ds8w(0x0f63, 1);
        f_cmp(ds16(0x2625), 0);
        if (ds16(0x2625) != 0) {
            Setup_CalibrateJoysticks();
            goto screen;
        }
    }
    if (AL == 0x3f) {   /* F5 */
        ds8w(0x0f63, 1);
        f_cmp(AL, 0x3f);
        Menu_RedefineKeys();
        goto screen;
    }
    ds8w(0x107f, 0);
    ds8w(0x107e, 0);
    R.si = ds16(0x0f73);
    if (ds8(R.si) == AL) {
        R.si++;
        ds16w(0x0f73, R.si);
        f_cmp(R.si, 0x0f73);
        if (R.si == 0x0f73) {
            ds8w(0x0f69, 1);
            ds8w(0x0f6a, 1);
            goto values;
        }
    } else {
        ds16w(0x0f73, 0x0f6b);
    }
    SET_BL(ds8(0x0f5f));
    SET_BH(ds8(0x0f61));
    SET_BL(BL - 1);
    SET_BH(BH - 1);
    SET_CL(ds8(0x0f64));
    SET_CH(ds8(0x263a));
    if (AL == 0x1c) goto store;   /* Enter */
    if (AL == 0x3b) {             /* F1: player 1's control, 0-based in BL */
        ds8w(0x0f63, 1);
        for (;;) {
            SET_BL(BL + 1);
            if ((int8_t)BL >= 5) SET_BL(0);
            if (BL == 0 && ds16(0x2625) == 0) continue;
            if (BL == 1 || BL == 2) continue;
            if (BH == BL) continue;
            break;
        }
        goto store;
    }
    if (AL == 0x3c) {   /* F2: player 2's, in BH */
        ds8w(0x0f63, 1);
        for (;;) {
            SET_BH(BH + 1);
            if ((int8_t)BH >= 5) SET_BH(0);
            if (BH == 2 && ds16(0x2627) == 0) continue;
            if (BH == 0 && ds16(0x2625) == 0) continue;
            if (BH == 1 && ds16(0x2625) != 2) continue;
            if (BH == BL) continue;
            break;
        }
        goto store;
    }
    if (AL == 0x3d) {   /* F3: the driver reloaded (with interrupts off: the port has none meanwhile) */
        f_cmp(AL, 0x3d);
        Cpu s = R;
        Snd_LoadDriver();
        popa(&s);
        ds8w(0x0f63, 1);
        SET_CL(CL + 1);
        if ((int8_t)CL >= 3) SET_CL(0);
        goto store;
    }
    if (AL != 0x3e) goto key;
    ds8w(0x0f63, 1);   /* F4 */
    SET_CH(CH + 1);
    if ((int8_t)CH > 5) SET_CH(1);
store:
    SET_BL(BL + 1);
    SET_BH(BH + 1);
    ds8w(0x0f5f, BL);
    ds8w(0x0f61, BH);
    ds8w(0x0f64, CL);
    ds8w(0x263a, CH);
    f_cmp(AL, 0x1c);
    if (AL != 0x1c) goto values;
    SET_AL(BH);
    SET_BH(0);
    SET_AH(0);
    f_logic(0);
    ds16w(0x2658, R.bx);
    ds16w(0x265a, R.ax);
    R.ax = 6;
    ds16w(0x265c, R.ax);
    ds16w(0x265e, R.ax);
    Input_SetSlots();
    SET_AL(ds8(0x263a));
    SET_AH(0);
    f_cmp(ds8(0x0f63), 0);
    if (ds8(0x0f63) != 0) {
        SET_AH(0x19);
        mach_int(0x21);   /* the current drive */
        f_cmp(AL, 2);
        if ((int8_t)AL >= 2) {
            R.dx = 0x0ed0;
            R.cx = 0;
            SET_AH(0x3c);
            mach_int(0x21);   /* create SETTINGS.DAT */
            if (!R.cf) {
                static const uint16_t from[8] = {0x2658, 0x265a, 0x263a, 0x0f64, 0x28fd, 0x28ff, 0x2905, 0x2907};
                uint16_t es = R.es;
                R.es = R.ds;
                uint16_t handle = R.ax;
                R.di = 0x0da8;
                for (int i = 0; i < 8; i++) {
                    R.ax = ds16(from[i]);
                    stosw();
                }
                R.si = 0x106c;
                R.cx = 8;
                rep_movsw();
                R.dx = 0x0da8;
                SET_AH(0x40);
                f_cmp(R.di, R.dx);
                R.cx = (uint16_t)(R.di - R.dx);
                R.bx = handle;
                mach_int(0x21);
                SET_AH(0x3e);
                mach_int(0x21);
                R.es = es;
            }
        }
    }
    R.ax = ds16(0x263a);
    f_cmp(AL, 5);
    if (AL == 5) {
        Sys_SpeedTest();
        R.ax = 0;
        Gfx_ClearBuffer();
    }
    R.cf = false;
}

/* Menu_Credits 1000:2a82
   The backdrop and the ten lines of the list at 193c:0f75 (font 0b54, from row 2dh, 10h apart), shown
   until a key is released. */
void Menu_Credits(void)
{
    Menu_DrawBackdrop();
    R.ax = 0;
    R.bx = 0x002d;
    R.si = 0x0f75;
    R.cx = 0;
    R.dx = 0x0b54;
    do {
        Text_DrawNth();
        R.bx = (uint16_t)(R.bx + 0x10);
        R.cx++;
        f_cmp(R.cx, 0x0a);
    } while (R.cx != 0x0a);
    Gfx_Present();
    ds8w(0x107e, 0);
    ds8w(0x107f, 0);
    while (ds8(0x107e) == 0) mach_frame();
    R.cf = false;   /* cmp byte [107e], 0 */
    R.zf = false;
}

/* Setup_CalibrateJoysticks 1000:2ab5
   On the backdrop: "joystick 1" (string 0ded with the digit at [0df6]) and three prompts (0dd3 centre,
   0dda left, 0ddf right; font 0b54 at x 0ah, rows 1eh-4eh), each answered with a button of joystick A;
   the X thresholds [28fd] = (centre + left) / 2 and [28ff] = (centre + right) / 2 (16-bit sums: an
   overflow is lost, as the original). With two joysticks the same for joystick B at x 6eh into [2905]
   [2907]. Joy_WaitButton returns the stick's X in AX (A) or CX (B). General registers and DS, ES kept
   (as after the backdrop, which comes first). */
void Setup_CalibrateJoysticks(void)
{
    Menu_DrawBackdrop();
    Cpu s = R;
    R.ax = SEG_DATA;
    R.es = R.ax;
    R.ds = R.ax;
    ds8w(0x0df6, '1');
    R.si = 0x0ded;
    R.bx = 0x001e;
    R.dx = 0x0b54;
    R.ax = 0x000a;
    Text_Draw();
    R.si = 0x0dd3;
    R.bx = 0x002e;
    R.dx = 0x0b54;
    R.ax = 0x000a;
    Text_Draw();
    Joy_WaitButtonA();
    uint16_t centre = R.ax, centre_y = R.bx;   /* push bx / push ax: the Y is popped into SI, unused */
    R.si = 0x0dda;
    R.bx = 0x003e;
    R.dx = 0x0b54;
    R.ax = 0x000a;
    Text_Draw();
    Joy_WaitButtonA();
    R.si = centre;
    R.ax = f_mid(R.ax, R.si);
    ds16w(0x28fd, R.ax);
    R.si = 0x0ddf;
    R.bx = 0x004e;
    R.dx = 0x0b54;
    R.ax = 0x000a;
    Text_Draw();
    Joy_WaitButtonA();
    R.si = centre;
    R.ax = f_mid(R.ax, R.si);
    ds16w(0x28ff, R.ax);
    R.si = centre_y;
    f_cmp(ds16(0x2625), 2);   /* the flags returned with one stick */
    if ((int16_t)ds16(0x2625) >= 2) {
        ds8w(0x0df6, '2');
        R.si = 0x0ded;
        R.bx = 0x001e;
        R.dx = 0x0b54;
        R.ax = 0x006e;
        Text_Draw();
        R.si = 0x0dd3;
        R.bx = 0x002e;
        R.dx = 0x0b54;
        R.ax = 0x006e;
        Text_Draw();
        Joy_WaitButtonB();
        centre = R.cx;   /* push dx / push cx */
        R.si = 0x0dda;
        R.bx = 0x003e;
        R.dx = 0x0b54;
        R.ax = 0x006e;
        Text_Draw();
        Joy_WaitButtonB();
        R.si = centre;
        R.cx = f_mid(R.cx, R.si);
        ds16w(0x2905, R.cx);
        R.si = 0x0ddf;
        R.bx = 0x004e;
        R.dx = 0x0b54;
        R.ax = 0x006e;
        Text_Draw();
        Joy_WaitButtonB();
        R.si = centre;
        R.cx = f_mid(R.cx, R.si);   /* the flags returned with two */
        ds16w(0x2907, R.cx);
    }
    R.es = s.es;
    R.ds = s.ds;
    popa(&s);
}

/* Setup_ChooseGame 1000:2be8
   The data sets: on the backdrop, the heading (0ee7, row 20h); the DTA at 193c:0d7d and a search for
   GAME?.LVL (pattern 0d71); none: CF = 1. Each file found: its digit (the name's fifth character, at
   193c:0d9f) appended to the list at 193c:0edd ("1" remembered in [0d7b] as the starting place), the
   file loaded (File_LoadGameSet, which reads its title to 193c:040a) and the title written at x 1,
   rows from 3ch, 8 apart ([0efd]). The list ends with 0. Then a cursor: wait until no input; the entry
   (clamped to the list) recoloured, colour 0 to [0d70] (stepping 1..0ah each pass; not drawn while the
   blink flag [26cf] is 1), a tick (Sys_WaitTick), the screen shown, the colour put back; up (20h, 80h)
   moves back, down (10h, 40h) on, one step a press; fire loads that set's GAMEn.LVL (the digit patched
   into 193c:0889 at 088d). CF = 0. */
void Setup_ChooseGame(void)
{
    ds8w(0x0156, 0);
    Menu_DrawBackdrop();
    R.si = 0x0ee7;
    R.bx = 0x0020;
    R.dx = 0x0b54;
    Text_DrawCentred();
    R.dx = 0x0d7d;
    SET_AH(0x1a);
    mach_int(0x21);   /* set the DTA */
    SET_AH(0x4e);
    R.cx = 0;
    R.dx = 0x0d71;
    mach_int(0x21);   /* find first */
    if (R.cf) {
        R.cf = true;
        return;
    }
    ds16w(0x0efd, 0x003c);
    R.di = 0x0edd;
    do {
        R.si = 0x0d9b;
        SET_AL(ds8((uint16_t)(R.si + 4)));
        ds8w(R.di, AL);
        f_cmp(AL, '1');
        if (AL == '1') ds16w(0x0d7b, R.di);
        R.di++;   /* inc: CF stays */
        uint16_t di = R.di;
        File_LoadGameSet();
        R.bx = ds16(0x0efd);
        R.ax = 1;
        R.si = 0x040a;
        R.dx = 0x0b40;
        Text_Draw();
        ds16w(0x0efd, f_add(ds16(0x0efd), 8));
        SET_AH(0x4f);
        mach_int(0x21);   /* find next */
        R.di = di;
    } while (!R.cf);
    ds8w(R.di, 0);
    Gfx_Present();
    R.di = ds16(0x0d7b);
    for (;;) {
        uint16_t di = R.di;
        for (;;) {   /* until all input is let go */
            Input_Read();
            if (ds8(0x108b) == 0) break;
            f_cmp(ds8(0x108b), 0);
            mach_frame();
        }
        R.di = di;
        if (R.di < 0x0edd) R.di = 0x0edd;
        SET_AL(ds8(R.di));
        if (AL == 0) R.di--;
        R.bx = (uint16_t)(R.di - 0x0edd);
        R.bx = (uint16_t)(R.bx << 3);
        R.bx = (uint16_t)(R.bx + 0x3c);
        di = R.di;
        uint16_t row = R.bx;
        R.ax = 0;
        R.cx = 8;
        R.si = 0x0100;
        SET_DH(0);
        SET_DL(ds8(0x0d70) + 1);
        if ((int8_t)DL > 0x0a) SET_DL(1);
        ds8w(0x0d70, DL);
        f_cmp(ds8(0x26cf), 1);
        if (ds8(0x26cf) != 1) Gfx_RecolourRect();
        Sys_WaitTick();
        R.ax = row;
        R.cx = 8;
        Gfx_Present();   /* AX, CX as for Gfx_PresentRows, but the whole screen is shown (as the original) */
        R.bx = row;
        R.ax = 0;
        R.cx = 8;
        R.si = 0x0100;
        SET_DH(ds8(0x0d70));
        SET_DL(0);
        Gfx_RecolourRect();
        R.di = di;
        Input_Read();
        R.di = di;
        SET_AL(ds8(0x108b));
        f_logic(AL & 8);   /* test al, n: every way back to the top, and to the load, has CF clear */
        if (AL & 8) break;
        R.di--;
        if (AL & 0x20) continue;
        if (AL & 0x80) continue;
        R.di = (uint16_t)(R.di + 2);
        if (AL & 0x10) continue;
        if (AL & 0x40) continue;
        R.di--;
    }
    SET_AL(ds8(R.di));
    ds8w(0x088d, AL);
    R.si = 0x0889;
    File_LoadGameSet();
    R.cf = false;
}

/* Menu_RedefineKeys 1000:92f0
   REDEFINE KEYS: the backdrop, then for each player (the headings 193c:ae3d and ae44, font 0b54, x 0ah,
   11h rows down) five prompts (the list at ae4b, font 0b40, x 14h, entries 0-4), each answered with a
   key released: Esc abandons (the key table unchanged), space is ignored, and so is a key already taken
   in this round (the ten codes collected at 193c:ae73, cleared first). The key's name, one character
   from the pairs (scan code, character) at 193c:adf0 up to a 0 code, '?' if not there, is written at x
   6eh (the string at ae3a). At the end the codes go to the key table: five to 193c:106c (player 1's left,
   right, two drive buttons, fire), five to 1074. All registers kept; CF = 0. */
void Menu_RedefineKeys(void)
{
    Menu_DrawBackdrop();   /* a call whose displacement wraps around the segment's end to 0400 */
    Cpu s = R;
    R.ax = SEG_DATA;
    R.es = R.ax;
    R.ds = R.ax;
    R.di = 0xae73;
    R.cx = 0x000a;
    SET_AL(0);
    while (R.cx != 0) {   /* rep stosb */
        mbw(R.es, R.di, AL);
        R.di = (uint16_t)(R.di + str_step(1));
        R.cx--;
    }
    ds8w(0xae3c, 1);
    R.di = 0xae73;
    R.cx = 0x0063;
    R.bx = 0x0028;
    for (;;) {
        uint16_t di = R.di;
        R.cx++;
        if ((int16_t)R.cx >= 5) {   /* the next player's heading */
            R.si = 0xae3d;
            if (ds8(0xae3c) != 1) R.si = 0xae44;
            ds8w(0xae3c, (uint8_t)(ds8(0xae3c) + 1));
            R.ax = 0x000a;
            R.dx = 0x0b54;
            Text_Draw();
            R.bx = (uint16_t)(R.bx + 0x11);
            R.cx = 0;
        }
        R.dx = 0x0b40;
        R.ax = 0x0014;
        R.si = 0xae4b;
        Text_DrawNth();
        Gfx_Present();
        ds8w(0x107e, 0);
        ds8w(0x107f, 0);
        R.dx = R.cx;
        R.di = di;
        for (;;) {   /* [107e] is not cleared for a refused key: the loop spins until another release */
            wait_key_released();
            if (AL == 1) {   /* Esc */
                R.es = s.es;
                R.ds = s.ds;
                popa(&s);
                R.cf = false;   /* cmp al, 1 */
                R.zf = true;
                return;
            }
            if (AL == 0x39) {
                mach_frame();
                continue;
            }
            SET_AH(AL);
            R.si = 0xae73;
            R.cx = 0x000a;
            bool taken = false;
            do {
                SET_AL(mb(R.ds, R.si));   /* lodsb */
                R.si = (uint16_t)(R.si + str_step(1));
                if (AL == AH) {
                    taken = true;
                    break;
                }
                R.cx--;
            } while (R.cx != 0);
            if (taken) {
                mach_frame();
                continue;
            }
            break;
        }
        R.cx = R.dx;
        SET_AL(AH);
        mbw(R.es, R.di, AL);   /* stosb */
        R.di = (uint16_t)(R.di + str_step(1));
        di = R.di;
        SET_DL(AL);
        R.si = 0xadf0;
        SET_DH('?');
        for (;;) {
            lodsw();
            if (AL == 0) break;
            if (AL == DL) {
                SET_DH(AH);
                break;
            }
        }
        ds8w(0xae3a, DH);
        uint16_t bx = R.bx;
        R.ax = 0x006e;
        R.si = 0xae3a;
        R.dx = 0x0b40;
        Text_Draw();
        R.bx = (uint16_t)(bx + 0x0a);
        R.di = di;
        if ((int16_t)R.di >= (int16_t)0xae7d) break;
    }
    R.si = 0xae73;
    R.di = 0x106c;
    movsw();
    movsw();
    movsb();
    R.di = (uint16_t)(R.di + 3);
    movsw();
    movsw();
    movsb();
    R.es = s.es;
    R.ds = s.ds;
    popa(&s);
    R.cf = false;   /* add di, 3 */
    R.zf = false;
}
