# Machine-facing routines

The routines of work package 1 (docs/re/functions.md) beyond the timer and the keyboard: the players'
input, the joysticks, the palettes and fades, the sound driver's loading, the speed test, the start-up
check and the exit's clean-up. Internal notes; addresses seg:off with the image at 1000h. Segments in
the port's numbering: the work segment is `7d78` (`6d78` in the file before relocation), the overlay
`2424`. Ported in `src/game/input.c`, `joy.c`, `palette.c`, `sound.c`, `system.c`. "Confirmed" = read in
the disassembly and checked against the harness's recordings; "read" = from the disassembly only.

## Input slots (confirmed)

Each of the four vehicles has a control type (words `193c:2658`, `265a`, `265c`, `265e`; the first two
from `SETTINGS.DAT`): 1 joystick A, 2 joystick B, 3 mouse, 4 keys 1, 5 keys 2, anything else (6) none.
`Input_SetSlots` (2d00) turns them into near routine addresses in `1083`..`1089` and collects in `108c`
which joysticks need timing (bit 0 A, bit 1 B). `Input_Read` (2d5b), once per frame of the menus and once
per game step of the race:

1. times the joysticks when `108c` is exactly 1, 2 or 3 (A, B, both) into `108d`/`108f` (A's X, Y),
   `1091`/`1093` (B's), the port's buttons inverted into `1095`;
2. calls each slot with BX = its vehicle's record (`2660`..`2666`) and stores AL into the input bytes
   `137b`, `14df`, `1643`, `17a7` (the records are 164h apart);
3. `108b` = the first two inputs or'ed when `1080` is 0, else the byte `1080` points to (the menus point
   it at one player's input byte).

The input byte: 80h left, 40h right, 20h and 10h the two drive buttons (accelerate, brake), 08h fire
(select in the menus). Keys 1 is the high byte of the keyboard handler's mask `107c` (scan codes
`106c`..`1073`), keys 2 the low byte (`1074`..`107b`), so the order of the scan codes in `SETTINGS.DAT` is
the order of the bits.

The slot "none" (`Input_None`, 2ded, package 6) answers 0, or the computer driver's input during a race.

## Joysticks (read; the machine has none)

The game port at 201h: a write fires the four one-shots; bits 0-3 (A X, A Y, B X, B Y) read high for a
time growing with each axis's resistance; bits 4-7 are the buttons, active low (4, 5 A's; 6, 7 B's).
The three timing entries (2f9e A, 2fcc B, 2ffe both) share one loop: interrupts off, each pass reads the
port and adds each axis bit into its own counter (SI, DI, BX, BP: A X, A Y, B X, B Y) until the axes of
the joystick wanted are all low or 30000 passes are done (`loopne` on CX = 7530h). A last read of the
port gives the buttons in AL.

Only X is used. The slots (2e6c A, 2eb3 B) compare X with two thresholds (A: `28fd` low, `28ff` high;
B: `2905`, `2907`; saved in `SETTINGS.DAT`): at or below the low one is left, at or above the high one is
right. They also compare Y with `2901`/`2903` (B: `2909`/`290b`) but the results are overwritten before
any branch: the stick's Y does nothing. Button 1 gives 20h, button 2 10h, either 08h (fire), both 30h.

`Joy_Detect` (3a12) asks the BIOS (int 15h 84h, DX = 1: the axes) and counts a joystick when its X > 5 or
its Y >= 5; `[2625]` = 0, 1 or 2. With no BIOS support (CF) it is 0. `Joy_WaitButton` (2b93, entries 2b8b
A and 2b8f B with the button mask in BL) is the calibration's "centre the joystick and press fire":
it waits for the buttons to be up, then redraws the blinking prompt and presents until a button is
pressed or Enter released, and returns the four timings (AX, BX = A's X, Y; CX, DX = B's).

The port's machine and the harness have nothing at 201h (reads FFh): every axis times out at 30000 and
no button is pressed. Both waits of `Joy_WaitButton` spin with interrupts on; in the port each pass that
waits on takes a frame.

## Mouse (read)

`Input_Mouse` (2e02): int 33h 3, then left button 28h, right 10h; X left of `160 - [290d]` is 80h, right
of `160 + [290d]` 40h, Y above `100 - [290d]` 20h, below `100 + [290d]` 10h (signed, the edges included);
if a direction was given the pointer is put back at 160,100 (int 33h 4). The mouse flag `2627` that
offers it is set only by the dead set-up routine 3a44, so the slot is never chosen by the game itself.
With no driver (int 33h returns AX = 0 and leaves BX, CX, DX) the routine reads the caller's registers
as the buttons and the position.

## Palettes and fades (confirmed)

A palette file is 300h bytes, 256 colours of three 6-bit values. `Pal_LoadFile` (07a0) reads one into
`7d78:0000` but does not set the DAC: it loads the block-set function number and then overwrites it byte
by byte, so the BIOS call it makes is AX = 1001h (the overscan colour, BH = 0Fh); then it clears the
text window with int 10h 06h. `Pal_LoadIntro` (26ec) sets the DAC from the buffer after it.
`Pal_LoadRound` (4758) reads `ROUNDn.PAL` into the same buffer; on theme 3 it replaces colours 251-253
with 219-221 (track 2) or 235-237 (track 1); CF on a failed open or read.

The fades work on `7d78`: `Pal_FadeOut` (327a) reads the DAC into `0000` (int 10h 1017h) and runs the
steps 126 down to 1 of `Pal_FadeOutStep` (32ae): on every even step each nonzero value drops by one,
and every step writes all 256 colours to the DAC (3C8h/3C9h). 63 drops: everything reaches black.
`Pal_FadeIn` (32ce) zeroes a working copy at `0300`, then runs steps 0 to 127 of `Pal_FadeInStep`
(3303): on even steps each working value that differs from its target at `0000` goes up by one (a
"differs", so a value above its target would wrap round; from black it does not occur), and every step
writes the working copy. 64 raises. There is no retrace wait anywhere: a fade takes as long as the CPU
needs for 126 or 128 full DAC writes (the port's CPU is instant: a fade is not seen). The flag
`26ce` pairs them: a fade out only when faded in, a fade in only after a fade out.

## Sound driver loading (confirmed)

`Snd_LoadDriver` (321c) patches `'0' + [0f64]` into `DRIVERn.BIN` (`11da`, the digit at `11e0`), holds
off the timer's driver tick (`cs:3279` = 0), reads the file whole to `2424:0000`, calls the driver's
function 0 (DX = 388h, BX and CX = the words `193c:000b`, `000d`, read through CS because DS is the
overlay's then), then function 2 with CX = 4287h until it answers AL = 0, and sets `cs:3279`. With
DRIVER0 (no sound) both return AX = 0. The front end loads the driver once in `Menu_LoadFrontEnd`
(with the initial `[0f64]` = 0) and Start loads the chosen one after the options screen.

`Snd_StopAll` (00e2): unless `[0f64]` is 1, the driver's function 10h with AL = 0.

## The speed test (confirmed)

`Sys_SpeedTest` (3ad0) runs once, when the options screen is left with SMOOTHNESS on AUTO (`[263a]` = 5;
`SETTINGS.DAT` keeps 5, the test overwrites `263a` with the value used). It reads the CRTC base from the
BIOS (0040:0063) into `2643`, and with interrupts off waits for the end of a retrace, then for the start
of the next, and counts loop passes for as long as that retrace lasts (3DAh bit 3 set). The pass stores
a word at A000:0030 under a `rep` prefix with CX = 1000; `rep` on a non-string instruction is ignored,
so it stores once. BX counts the passes (wrapping to 0 at FA00h into CX, which is never looked at).
`[263a]` = 4 below 13 passes, 3 below 20, 2 below 24, else 1 (signed). So it measures the retrace pulse
by CPU speed: the slower the machine, the more game steps per presented frame.

`263a` (SMOOTHNESS) is the number of game steps `Race_Main` runs per presented frame (it draws on the
last only, 1000:3061, 30bd, 30d7) and scales a few speeds elsewhere (1000:2280 the front end's scroll,
6dfc, 84ae in the race).

In the harness (300 PIT counts of retrace, a tick per port read and 0.1 per instruction) the loop counts
**134 passes** (`BX` = 86h) and sets 1. In the port the CPU is instant: the model's retrace never ends
while the loop runs, so the loop is cut after 65536 passes (BX = 600h, CX = 1), which also gives 1;
then the model's retrace is ended. Under the checker the port's loop reads the recording and counts the
same 134. Both give smoothness 1; a model of the pulse in CPU time would be needed to make AUTO pick
anything else.

## Start-up and exit (confirmed)

`Sys_CodeCardCheck` (31f0): `FONT.BIN` (open mode C0h, handle not checked) read whole to `2424:0000` and
called far with AX = 0; AX = 0 back is a pass. `Sys_ReadCrtcBase` (319f): `[2643]` = the word at
0040:0063. `Sys_ClearRegs` (31b6): the 32-bit registers zeroed, then their halves, CLD, CLC, STI.

`Sys_RemoveIrqs` (49af), interrupts off: `Timer_Remove`, the BIOS's IRQ 1 vector back, then the DOS
clock set from the real-time clock because the timer chain ran the BIOS's tick at 70 Hz: int 1Ah 02h
(retried while CF), the BCD hours, minutes, seconds converted (`Sys_BcdToBin`, 4adf: tens = AL / 16,
units = the remainder, BX left as units:0Ah) and set with int 21h 2Dh. The date the same way from int
1Ah 04h, but the year is computed as `100 x CL + CL` where CL is the year within the century (the century
CH was meant): 1994 becomes 9494, which DOS refuses (int 21h 2Bh, AL = FFh), so the date is never set
back. Then the driver's function 6 and `Retrace_IrqRemove`.

## Files

`File_LoadGameSet` (0f99): a `GAMEn.LVL`, named at DS:SI, in two reads into the data segment: 414h bytes
at `040a` and 3cch bytes at `1feb`; no error check. The floppy release has no `GAMEn.LVL` (the chooser's
find-first fails), so it never runs there (read only). `File_SaveScreen` (35bf): the pause's debug F12,
`SCRE0.RAW` with its digit counted up, FFFAh bytes of `7d78:0000`.
