# Sound

The game's sound is a separate program, `DRIVERn.BIN` (n from `SETTINGS.DAT`: 0 none, 1 the AdLib and
the speaker, 2 the speaker alone), loaded by `Snd_LoadDriver` (1000:321c) at 2424:0000 and called far
there with a function number in AH (the callers: `Overlay_Entry` in tools/ghidra/names.tsv). Both
drivers come from one third-party driver package (their banners say so); they share a design.

## The interface

| AH | What | Called from |
|---|---|---|
| 0 | initialise: DX = the AdLib's port (the game passes 388h, so the driver skips its detection), BX/CX = the music and the effects (193c:000b/000d) | `Snd_LoadDriver` |
| 2 | the tick rate (CX = 4287h), repeated until AL = 0 | `Snd_LoadDriver` |
| 3 | the tick: once per frame, from the timer handler while cs:3279 is set | `Timer_Irq` |
| 4 | play tune AL (after 9 asks whether AL is playing) | Start's loop, the menus |
| 5 | start sound effect AL | the race |
| 6, 7, 8 | stop all, stop the music, the speaker off (the exit) | Start's exit |
| 0Ah | is effect AL playing | the race |
| 0Eh-10h | the engine notes | the race |

## The drivers

`src/sound/driver1.c` (DRIVER1, the AdLib) and `driver2.c` (DRIVER2, the beeper), routine by routine,
register-exact, citing `DRIVERn:offset`; names in `tools/ghidra/names-driver1.tsv` and
`names-driver2.tsv` (the two share their addresses, so they are not in names.tsv). The design, the
voice layout and the variables are described at the top of each file. DRIVER1 sequences 16 voices on
the OPL2's nine melodic channels with a copy of the chip's registers (unchanged writes skipped);
DRIVER2 steps four voices and plays one of them per tick on the speaker (an arpeggio of the chord).
Quirk kept: DRIVER1 executes `int 3` (a breakpoint left in) when no voice is free; DOS's handler
returns, so does the port's machine.

`src/drv.c` recognises the program at 2424:0000 by its first bytes and a CRC-32 of its code (FONT.BIN,
DRIVER0, 1 or 2).

## The devices

- **The AdLib** (`src/sound/opl2.c`): a YM3812 model written for the port from the chip's documented
  behaviour (registers, operators, phase and envelope generators with their rates, the waveforms,
  feedback and the two connections, tremolo and vibrato, a simple rhythm mode), run at 49,716 Hz and
  resampled. Not bit-exact with the chip's logarithmic arithmetic; the driver never reads the chip.
- **The speaker** (`src/sound/speaker.c`): the PIT's channel 2 square wave while port 61h has bits 0
  and 1 set. Both drivers only start and stop tones; the state at the end of a frame sounds for the
  next frame.

## Checks

`tools/emu/sndlog.py --driver 1` logs the original's AdLib writes in the harness; `build/mm_run
--opl-log` the port's. Over 1200 frames of the menus and a race (out/race_keys.txt) the port writes the
same 2,255 register values in the same order as the original, one frame later throughout (the port
counts frames from its first wait); at one point an engine call and the timer's tick fall in the other
order within a frame (the harness's interrupts land between instructions, the port's at the frame
boundary).
