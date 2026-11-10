# Status

OpenMM **runs the game's menus and races, with sound**. As of 10 November 2026:

## What works

- **The data.** The 720 KB floppy as a disk image (FAT12), its files copied to a folder, or the game
  installed from it. Both give the same files, byte for byte, and the same pictures.
- **The start.** The launcher the player types runs the Codemasters splash screen first, then the game,
  as the original does. Both are rewritten in C. `MICRO.EXE` is found, checked and unpacked by the
  port's own version of its PKLITE loader.
- **The game.** All of the program's code that runs has been rewritten in C: the title and attract
  screens, the game options, the one- and two-player menus, the character picker, the race set-up and
  track loading, the vehicles, the computer drivers, the terrain, the race loop and its rules. In a
  headless run the port plays a whole one-player championship, from the qualifier through the
  challenge races, the bonus races and the results to CHAMPIONSHIP WINNER, and two-player races.
- **Sound.** The game's two sound drivers, for the Sound Blaster's AdLib chip and for the PC speaker,
  are rewritten too. They drive a model of the AdLib's sound chip written for the port, and a model of
  the speaker. The music and effects the drivers send to the chip match the original's, write for write.
- **How it is checked.** Each routine is run against recordings of the original program: the same
  memory, registers, port accesses and DOS calls. 151 routines pass so far; the others are not yet
  reached by the recorded runs, or wait for the timer. Those are checked by comparing whole runs picture
  by picture: a two-player race, a two-player tournament, a qualifying race, a failed qualifier and
  a whole championship give the original's pictures in the original's order, apart from differences
  traced to the reference emulator's timing. That check found fades and the champion's screen running
  in no time; they now take the time a PC of the day needs.

## In the browser

The [play page](/play/) runs the same module in the browser, from your own floppy image or its files,
with the options below as settings. A gamepad is the PC's joystick (joystick A, a second one B):
the game finds it, calibrates it with F7 on GAME OPTIONS and plays with it as JOY 1 (F1).

## Opt-in

- **The code card.** The original asks for a symbol from its code card before it starts. The check is
  rewritten, but off by default; `--param codecard=1` turns it on.
- **No splash.** `--param splash=0` starts the game without the splash screen.

## Not yet

- The AdLib model is the port's own; it has not yet been compared with a real chip's recordings.
- There are no releases yet.

The [port plan](https://github.com/emdzej/openmm/blob/main/docs/re/port-plan.md) lists the steps and the
check that ends each one.
