# Status

OpenMM is in its **groundwork**: nothing of the game runs yet. As of 10 November 2026:

## What works

- **The data.** The 720 KB floppy as a disk image (FAT12), its files copied to a folder, or the game
  installed from it. The floppy image and the folder give the same 162 files, byte for byte, checked
  against an independent reader.
- **The program.** `MICRO.EXE` is found and checked (size and CRC-32), and unpacked by the port's own
  version of its PKLITE loader. The result is checked against two independent unpackers: an emulated run
  of the original loader and the deark tool.
- **The runtime.** `openmm.wasm` builds for gasm, finds the data, checks the program and shows a blank
  320 x 200 screen. Without data it stops with a message saying what to pass.

## What is next

Reading the whole program in Ghidra and naming every function, then the machine underneath it (memory,
VGA, timer, keyboard, DOS files), then the game's formats, the front end and the race. The
[port plan](https://github.com/emdzej/openmm/blob/main/docs/re/port-plan.md) lists the steps and the
check that ends each one.

There are no releases and no browser player yet.
