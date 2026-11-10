# Architecture

How the original is built and how the port is shaped after it. Internal notes; grows as the program is
read. Addresses: seg:off with the image at 1000h (src/exe.h).

## The original

A real-mode DOS program in hand-written assembly. `MICRO.COM` runs `SM.EXE`, then `MICRO.EXE`. Its PKLITE
stub unpacks it (docs/formats/pklite.md) and enters `Start` (`1000:0006`):

1. The code-card check (`Sys_CodeCardCheck`, `1000:31f0`): `FONT.BIN` is code, read into `2424:0000` and
   called far with AX = 0; AX = 0 back is a pass, anything else goes straight to int 21h 4C00h.
2. `DS` = `193c` (the data segment), `SS:SP` = `7d78:03e8` relative to the load (8d78 in the port's
   numbering), `Copy_Patch32` (`1000:7a75`: patches two copy loops to `movsd`, unconditionally; with other
   32-bit instructions used directly, the game needs a 386), the video mode saved (int 10h 0Fh), mode 13h.
3. `Menu_LoadFrontEnd` (`1000:26c0`: the `COMPRESS.PI*` pictures, the driver, the palette), `Irq_Install`
   (`1000:498a`: timer, retrace, keyboard), the driver AH = 7, the game options screen (`Menu_GameOptions`,
   `1000:2770`, which reads `SETTINGS.DAT` once; carry = Esc: the exit), flags cleared, the data-set
   chooser (`Setup_ChooseGame`, `1000:2be8`: `GAMEn.LVL`), the driver loaded (`1000:321c`), and SS:SP saved
   in `193c:0004`/`0006`.
4. The loop, with the driver's title tune asked for and played between (AH = 9 then 4): `Menu_Attract`
   (`1000:0100`, the attract screens; carry = Esc: back to step 3's options screen) and `Menu_SelectPlayers`
   (`1000:0220`, one or two players; carry = a timeout or Esc: back to the attract screens). Both are front
   end: the races run from inside the menus (`Race_RunOne` `1000:115c` and `Race_RunTwoPlayer` `1000:216c`
   call `Race_Main` `1000:3039`). A race quit with Esc ends in `Main_Restart` (`1000:00cc`), which reloads
   SS:SP from `193c:0004`/`0006` and jumps back into Start's loop: a longjmp out of any depth.
5. The exit: the video mode back, the driver stopped (AH = 7, 8, 6), `Snd_StopAll` (`1000:00e2`),
   `Sys_RemoveIrqs` (`1000:49af`: the vectors back, and DOS's date and time set again from the real-time
   clock, because the timer ran the BIOS clock fast), the keyboard's typematic rate (int 16h 0305h),
   int 21h 4C00h.

The timer (`Timer_Irq`, `1000:489c`) ticks once per frame, locked to the retrace: it busy-waits for the
retrace inside the interrupt and reloads the PIT with the measured frame less 150 counts; it counts
`cs:4ade` (the frame ticks the race waits on), `193c:0002`, `261f` and `28f7` (the front end's waits and
timeouts), calls the driver (AH = 3) once it is loaded, and chains to DOS's int 08h every tick. An IRQ 2
handler for the VGA's retrace interrupt is installed too and takes over the counting only if that
interrupt really arrives (it does not on most cards; the harness takes the timer path).

The race (`Race_Main`) runs SMOOTHNESS (`193c:263a`) game steps per presented frame, draws on the last one
only, waits for `cs:4ade` to reach a per-setting count, presents and clears the count. The front end waits
for single ticks of `193c:0002` inline, in more than a dozen places at different depths. The map of every
routine is docs/re/functions.md.

The sound driver is a separate binary (`DRIVERn.BIN`, n from the set-up) loaded at `2424:0000` and called
far with a function number in AH (0, 2-10, 0Eh, 10h are used). Everything else (VGA, the PIT, the
keyboard, the joysticks) the program drives itself through its ports and interrupt handlers
(docs/re/inventory.md).

## The port

C11, one module per subsystem of the original, every function citing its original address. Not an
emulator: the code is rewritten, the machine underneath is modelled only as far as the game's behaviour
depends on it.

**Memory.** The game's code works on segments and offsets: the data segment, 27 far buffers at fixed
segments, the sound driver's segment, the stack. The port keeps one real-mode memory, 1 MB as DOS
addresses it, with the unpacked image at 1000:0000 and the data segment initialised from it, so that
segment arithmetic, buffer overlaps and out-of-bounds reads behave as in the original. Ported functions
read and write it through typed accessors (`mb(seg, off)`, `d16(0x107e)` for the data segment, `c8` for
the code segment's own variables: `src/mem.h`). The registers that carry values between hand-written
routines are kept exactly: every routine leaves the register file `R` (`src/cpu.h`) as the original
leaves the CPU's registers, so routines port and check one at a time (docs/re/porting.md).

**The machine.** Ported as device models in the core, behind `src/platform.h`:

- VGA mode 13h: a 64 KB A000 segment shown as 320 x 200 through the DAC's palette (6-bit values widened
  as the DAC does); the CRTC's start address and the retrace status as far as the game uses them.
- The PIT and IRQ 0: the game's timer handler runs at the rate it programs; the frame rate of the port
  follows from it (to be read: the rate and what the handler counts).
- The keyboard and IRQ 1: the handler gets set-1 scan codes in order (`plat_key_events`).
- The game port at 201h: gamepad slots 0 and 1 as joysticks A and B. The axes are timed in polls (a
  write starts them; each axis bit reads high for 20 to 420 reads by the stick's position, the game
  calibrates the range), the south and east buttons are the stick's two, and the BIOS's int 15h 84h
  answers with the same counts. With no gamepad 201h reads FFh and int 15h has no services, as in the
  harness.
- DOS: the int 21h file calls the game makes, over `src/vfs.h` (and the user's storage for the files it
  writes: `SETTINGS.DAT`).
- Sound: the driver's interface (AH functions) with the driver ported too, over a model of the AdLib's
  OPL2 and of the speaker.

**The frame.** The original waits in its own loops, for its timer's counters and for the retrace, and its
timer handler is locked to the retrace (it reprograms the PIT on every tick to fire just before it). So
the port keeps the loops (gasm's LOOP build: `gasm_main` runs `Start`, `plat_wait_frame` returns to the
runner): the frame boundary is the retrace's start, 70.086 times a second, where the screen is shown and
the program's IRQ 0 handler runs, then its IRQ 1 handler once per key event (`src/machine.c`). Each wait
of the original is a `mach_frame()` or one of the retrace waits of `src/vga.h`. The CPU is instant: time
passes only at the boundaries, so the PIT counts the game measures are whole frames.
Two stretches of work the original does without any wait are what the player sees, so they take time
(`mach_busy`, in PIT counts of the reference machine, the harness's): a fade (126 or 128 full palettes
to the DAC, 0.6 counts a write: 3 to 4 frames, as in the harness) and the slide of the champion's
screen (`Menu_ChampionScreen` 1000:1aad, a third of a frame a turn: 108 turns in 36 frames, the harness
34). When the work adds up to a frame, the boundary passes inside it, as the retrace would. Nothing else
is costed: the draws and presents between waits stay instant (in the harness a present is a tenth of a
frame), so the number of presents per wait is the original's.
