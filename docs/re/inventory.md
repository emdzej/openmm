# Inventory

What is on the user's copy and what the program is made of. Internal notes (not published). Facts here
come from the disk and from the program; "inferred" marks what is not yet confirmed in the code.

## The disk

One 3.5" 720 KB floppy, FAT12, volume label `MICROMASTER` (737,280 bytes; boot sector OEM "IBM  5.0").
162 files: 26 in the root and 136 in `GAME1\`, about 600 KB. `tools/fat.py --summary` gives `5b7128d6`
(the same from the extracted folder through `src/vfs.c`, `tests/vfs_test.c`).

Some tools list a 163rd, empty file `MICROMAS.TER`: that is the volume label entry, not a file.

### Programs

| File | Size | CRC-32 | What |
|---|---|---|---|
| `MICRO.COM` | 472 | `b3839449` | the launcher: runs `SM.EXE`, then `MICRO.EXE` (int 21h 4Bh), then text mode; prints a message if either is missing or memory is short |
| `SM.EXE` | 4,832 | `225ae763` | a small Codemasters program run first; reads `GFX1.GFX` and `ANTIFONT.BIN`; has a "T: A: D:" date/time line (purpose to be read) |
| `MICRO.EXE` | 28,955 | `8e84eb22` | **the game**, PKLITE-compressed (docs/formats/pklite.md); dated 1994-09-14 |
| `DRIVER0.BIN` | 8 | | the no-sound driver: two entries that return 0 |
| `DRIVER1.BIN` | 28,625 | | "Sound Images Generation 2 sound drivers (c) 1992/93 Tony Williams/Sound Images": AdLib (port 388h) and the PC speaker |
| `DRIVER2.BIN` | 11,845 | | the same family, 1992-94, "Internal Beeper v1.08x" |
| `INSTALL.EXE` + `INSTALL.DAT` | | | the DOS installer and its script (9/9/94): `MAKEDIR GAME1`, `COPYFILE` of every game file to `C:\MICROM` by default |
| `SETUP.EXE`, `CTL3D.DLL`, `APPSETUP.INF`, `MICRO.PIF`, `MICRO.ICO` | | | a Windows 3.1 installer (InstallWare 3.1) that sets up a Program Manager icon for the DOS game |

An installed copy is the same files in the same layout, so the port reads either.

### Data

| Files | What (inferred from names and sizes until each is read) |
|---|---|
| `SETTINGS.DAT` (32) | written by the game's set-up: the keys as scan codes (the arrows 4B 4D 48 50 visible), sound and joystick choices |
| `INTRO.PAL`, `GAME1\ROUNDn.PAL` (768 each) | VGA palettes, 256 x RGB, 6-bit values |
| `COMPRESS.PI0`-`PI6` | compressed pictures (the front end's screens) |
| `GFX1.GFX`, `ANTIFONT.BIN`, `FONT.BIN`, `BITSFILE.PH0` | graphics and fonts |
| `GAME1\ROUNDnm.MAP` (2,048 each, 29) | the tracks' tile maps: 9 themes (n = 1-9), 2-4 maps each |
| `GAME1\ROUNDnmB.BRK` (26) | per map, small; break points or AI lines (to be read) |
| `GAME1\ROUNDn.COL`, `.DIR` | per theme: tile collision and direction data (inferred) |
| `GAME1\ROUNDnBR.PR0`-`PR2`, `.VH0`, `.CT`, `.LEV` | per theme: tile graphics (packed), vehicle graphics, ... |
| `GAME1\CHEATS.BIN` (360), `STRT_POS.BIN` (144) | cheat codes; the start positions |

The data segment's strings name the game's content: eight vehicle classes (Sportscars, Powerboats,
Formula One, Turbo Wheels, Four by Four, Warriors, Choppers, Rufftrux, and Pro variants of two), the
tracks by theme (for example Desktop Dropoff, Pencil Plateaux and Crayon Canyons; Bermuda Bathtub, Soap
Lake City and Foamy Fjords; Sahara Sandpit; The Breakfast Bends), the modes (one and two players,
qualifying, the championship, a bonus race, beat the clock, time trials) and handicap prompts
naming Walter, Mike and Anne. The port reads them from the program at run time.

`GAME1` and the program's string "THE ORIGINAL MICROMACHINES GAME1" suggest the program was written for
more than one data set; only GAME1 is on this disk.

## The program

`MICRO.EXE` unpacked by its PKLITE stub (the port runs it, `src/pklite.c`): a load image of 82,493 bytes
(CRC-32 `943cfcf8`), 279 relocations, entry `1000:0006`, stack `8d78:0400`. Addresses are seg:off with the
image at segment 1000h, as in Ghidra and `tools/emu` (src/exe.h).

Hand-written assembly: no compiler's prologues or runtime library, near calls within one code segment,
registers rather than a stack frame for arguments. Whether it needs a 386 is to be read.

| Segment | Size | What |
|---|---|---|
| `1000` | 37,824 (`1000:0000`-`93bf`) | the code: 168 functions found by Ghidra's analysis, 26 KB of them (the rest: code reached only through tables, and data, to be mapped) |
| `193c` | 44,669 | the data segment (DS from the first instructions): strings, tables, the game's state |
| `2424` | 30,016, not in the file | the sound driver: `DRIVERn.BIN` is loaded here and called far at `2424:0000` with the function in AH (109 calls) |
| `2b78`-`7cdb` | about 330 KB, not in the file | 27 far buffers at fixed segments (graphics, maps, decompression, inferred); each named once its users are read |
| `8d78` | | the stack (SS:SP 8d78:03e8 after start-up) |

The program needs 7DA9h paragraphs (about 504 KB) of conventional memory from its load segment: the
PKLITE stub refuses to run with less ("Not enough memory").

### The platform it touches

From a sweep of the code segment (counts of instructions, not yet of functions):

- **Video**: int 10h (get and set mode: 13h, 320 x 200 x 256; palette functions 10h), the DAC (3C8h/3C9h),
  CRTC registers (3D4h: start address or a tweak, to be read), retrace waits (3DAh), the attribute
  controller (3C0h).
- **Timer**: the PIT reprogrammed (40h/43h), end-of-interrupt to the PIC (20h), the PIC mask (21h), six
  vectors set (int 21h 25h, three read with 35h), int 1Ah. It also sets the date and time (int 21h 2Bh,
  2Dh), probably to repair the clock after running the PIT fast (to be confirmed).
- **Input**: the keyboard from its own IRQ 1 handler (60h, 61h, 64h), int 16h (typematic rate, on exit),
  the game port's joysticks (201h), the mouse (int 33h).
- **Sound**: the speaker (61h) besides the driver.
- **Files**: int 21h open, read, write, create, close, find first/next, change directory, the current
  drive and the DTA. File names are in the data segment (`193c`), some built at run time (ROUND11.MAP's
  digits).

## Prior work

- [OpenMM on SourceForge](https://sourceforge.net/projects/openmm/): a GPL-2.0 remake of Micro Machines
  (C++, Qt, OpenGL), development halted; a new game in the spirit of the original, not a port of this
  program. No code or data is shared.
- No decompilation or reimplementation of the DOS program was found (searches of 2026-11-10: GitHub,
  general web).
- Tools used for checking: [deark](https://entropymine.com/deark/) unpacks PKLITE independently of the
  port (its result equals the port's and the emulated stub's).
