/* The program's entry and its outermost loop (docs/re/functions.md, "main"; docs/re/game-structure.md).
   Start checks the code card, takes over the machine (the data segment, its own stack, mode 13h, the
   front end's pictures, the interrupt handlers), runs the options screen and the data-set chooser once,
   then loops between the attract screens and the choice of one or two players. All the games run from
   inside that loop. A race quit with Esc ends in Main_Restart, which in the original reloads the stack
   saved at 1000:0054 and jumps back there: a longjmp out of any depth. The port sets g_restart instead,
   every routine on the way back returns at once (CALL_R), and Start continues at the 0054 point.

   Data: 193c:0001 the video mode found at start-up, 0004/0006 SS:SP saved at 0054, 1082 1 while a race
   runs, 1096 1 once Esc is released (the race quits on it), 107e/107f the last key released / pressed,
   1080 the input record the menus read. */
#include "cpu.h"
#include "game.h"
#include "machine.h"
#include "mem.h"
#include "segs.h"
#include "routines.h"

/* The title tune: AH = 9 asks whether tune AL can play, AH = 4 plays it. AX comes back as it was (the
   original pushes it); the flags are the driver's when it plays, else those of `or al, al`. */
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

/* Start 1000:0006
   Entered as DOS leaves a program (DS = ES = the PSP, SS:SP the exe's). The code card's check first
   (AX = 0 back is a pass, anything else ends the program at once); then DS = 193c, SS:SP = 8d78:03e8
   (7d78h in the file, relocated), the copy loops patched for the 386, the video mode saved and mode
   13h set, the front end loaded, the interrupt handlers installed. 0032: the driver's tune stopped
   (AH = 7), the options screen (carry = Esc: the exit), the key state cleared, the data-set chooser, the
   driver loaded. 0054: SS:SP saved for Main_Restart, the quit and in-race flags cleared, the driver
   loaded again (as the original: it loads it twice on the way in). 0069: the title tune, the attract
   screens (carry: back to the options screen at 0032), then the choice of players, again and again
   (carry: back to the attract screens). The exit (0097): the video mode back, the driver stopped
   (AH = 7, 8, 6), Snd_StopAll, the vectors back, the keyboard's typematic rate set (int 16h 0305h,
   BX = 010ch), int 21h 4C00h. */
void Start(void)
{
    Sys_CodeCardCheck();
    if (R.ax != 0) goto dos_exit;
    R.ax = 0x193c;
    R.ds = SEG_DATA;
    R.ax = 0x8d78;
    R.ss = 0x8d78;
    R.sp = 0x03e8;
    Copy_Patch32();
    SET_AH(0x0f);
    mach_int(0x10);
    d8w(0x0001, AL);
    R.ax = 0x13;
    mach_int(0x10);
    Menu_LoadFrontEnd();
    Irq_Install();
options:                       /* 1000:0032 */
    SET_AH(7);
    mach_drv();
    Menu_GameOptions();
    if (R.cf) goto quit;
    d8w(0x107e, 0);
    d8w(0x107f, 0);
    d16w(0x1080, 0);
    Setup_ChooseGame();
    Snd_LoadDriver();
restart:                       /* 1000:0054, where Main_Restart lands */
    d16w(0x0004, R.ss);
    d16w(0x0006, R.sp);
    d8w(0x1096, 0);
    d8w(0x1082, 0);
    Snd_LoadDriver();
attract:                       /* 1000:0069 */
    play_tune(1);
    d8w(0x1096, 0);
    Menu_Attract();
    if (g_restart) goto restarted;
    if (R.cf) goto options;
    for (;;) {
        d8w(0x1096, 0);
        Menu_SelectPlayers();
        if (g_restart) goto restarted;
        if (R.cf) goto attract;
    }
restarted:
    /* Main_Restart 1000:00cc has set DS, ES and SS:SP; it calls Snd_StopAll, then jumps to 0054 */
    g_restart = false;
    Snd_StopAll();
    goto restart;
quit:                          /* 1000:0097 */
    R.ax = 0x193c;
    R.ds = SEG_DATA;
    SET_AH(0);
    SET_AL(d8(0x0001));
    mach_int(0x10);
    SET_AH(7);
    mach_drv();
    SET_AH(8);
    mach_drv();
    SET_AH(6);
    mach_drv();
    Snd_StopAll();
    Sys_RemoveIrqs();
    R.ax = 0x0305;
    R.bx = 0x010c;
    mach_int(0x16);
dos_exit:                      /* 1000:00c6 */
    R.ax = 0x4c00;
    mach_int(0x21);   /* does not return (dos.c ends the program through plat_exit) */
}

/* Main_Restart 1000:00cc
   Leaving a game: DS = ES = 193c and SS:SP reloaded from 193c:0004/0006 (saved by Start at 0054), so
   that every return address above Start's loop is dropped; then Snd_StopAll and a jump to 1000:0054.
   The port sets the registers, raises g_restart and returns: the routines between return at once
   (CALL_R), and Start, seeing it, calls Snd_StopAll and goes on at 0054. */
void Main_Restart(void)
{
    R.ax = 0x193c;
    R.ds = SEG_DATA;
    R.es = SEG_DATA;
    R.ax = d16(0x0004);
    R.ss = R.ax;
    R.sp = d16(0x0006);
    g_restart = true;
}

/* Menu_LoadFrontEnd 1000:26c0
   With interrupts off (cli ... sti: no interrupt is delivered inside the port's routines anyway, the
   machine runs them only at mach_frame), DS = 193c; the front end's pictures, the series of packed
   files from the name at 193c:09f0 (COMPRESS.PI0 on), unpacked into segment 1b78 on; every picture
   object from 0b7c to 0d47 (1bh bytes apart) set up again from its descriptor; the sound driver loaded
   again; INTRO.PAL loaded and set. Called at start-up and after every race (the race uses the same
   memory). */
void Menu_LoadFrontEnd(void)
{
    R.ax = 0x193c;
    R.ds = SEG_DATA;
    uint16_t ds = R.ds;
    R.si = 0x09f0;
    R.dx = SEG_TILES;
    Unpack_LoadSeries();
    R.ds = ds;
    R.bx = 0x0b7c;
    do {
        uint16_t bx = R.bx;
        ds = R.ds;
        Gfx_InitObject();
        R.ds = ds;
        R.bx = (uint16_t)(bx + 0x1b);
    } while ((int16_t)R.bx < (int16_t)0x0d62);
    Snd_LoadDriver();
    Pal_LoadIntro();
}
