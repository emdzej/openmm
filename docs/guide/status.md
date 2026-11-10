# Status

OpenMM **runs the game's menus and races**, without sound yet. As of 10 November 2026:

## What works

- **The data.** The 720 KB floppy as a disk image (FAT12), its files copied to a folder, or the game
  installed from it. Both give the same files, byte for byte, and the same pictures.
- **The program.** `MICRO.EXE` is found, checked and unpacked by the port's own version of its PKLITE
  loader.
- **The game.** All of the program's code that runs has been rewritten in C: the title and attract
  screens, the game options, the one- and two-player menus, the character picker, the race set-up and
  track loading, the vehicles, the computer drivers, the terrain, the race loop and its rules. In a
  headless run the port goes from the title through the menus into a qualifying race and drives in it.
- **How it is checked.** Each routine is run against recordings of the original program: the same
  memory, registers, port accesses and DOS calls. 104 routines pass so far; the others are not yet
  reached by the recorded runs, or wait for the timer and are checked through whole runs.

## Not yet

- **Sound.** The game's AdLib and PC-speaker drivers are separate programs; they are next, with a
  model of the AdLib's sound chip.
- **The code card.** The original asks for a symbol from its code card before it starts. The port skips
  that check for now.
- Championships, two-player races and every track theme still need checking against the original.
- There are no releases and no browser player yet.

The [port plan](https://github.com/emdzej/openmm/blob/main/docs/re/port-plan.md) lists the steps and the
check that ends each one.
