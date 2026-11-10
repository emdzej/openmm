# Game data

OpenMM contains no code or data from Micro Machines. To run it you need your own copy of the DOS game.

## The supported release

The 3.5" 720 KB floppy of the DOS version (volume label `MICROMASTER`), whose program `MICRO.EXE` is
28,955 bytes with CRC-32 `8e84eb22` (dated 14 September 1994). Other versions of the program are refused
with a message naming the expected one.

## The forms it accepts

- **A disk image** of the floppy (`.img` or `.ima`, 737,280 bytes), passed to gasm as the asset `disk`:
  `gasm-run openmm.wasm --asset disk=disk1.img`.
- **The floppy's files** in a folder: `gasm-run openmm.wasm --asset-dir <folder>`. The folder must hold
  `MICRO.EXE` and the `GAME1` directory.
- **The installed game.** The DOS installer copies the same files to `C:\MICROM`; that folder works the
  same way.

When copying the files from an image with an archive tool, leave out an empty file called
`MICROMAS.TER` if one appears: it is the disk's label, not part of the game.
