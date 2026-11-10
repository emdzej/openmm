# The launcher and the splash

## MICRO.COM

A 472-byte COM program, what the player types. It shrinks its memory block (int 21h 4Ah), has DOS run
`SM.EXE` (int 21h 4B00h, an empty parameter block: no command tail), then `MICRO.EXE`, saving and
restoring SS:SP around each EXEC. If an EXEC fails it sets text mode (int 10h AX = 3) and prints with
the BIOS's teletype: "cannot find" for error 2, else "not enough memory" with the error code's digit;
then it ends (int 21h 4C00h). The exit codes of the two programs are not looked at.

The port (`src/launcher.c`, `Launcher_Main`) runs the two programs in that order. DOS's EXEC is the
port's: each program is placed at 1000:0000 with its PSP at 0FF0 (as the harness loads them; on a real
machine they sit above MICRO.COM, which their relocations make invisible), the rest of memory as the
previous program left it, and its ported entry run until int 21h 4Ch. The messages are read from the
user's MICRO.COM. `--param splash=0` skips SM.EXE (for comparisons with the harness, which runs
MICRO.EXE alone).

## SM.EXE

The Codemasters splash, 4,832 bytes, not packed: code 0788-0dbb, its data in the same segment. It
allocates two 64 KB blocks (int 21h 48h; its header asks for 5DCh paragraphs, not all memory, so DOS
has some to give: the port's DOS and the harness model that), loads `GFX1.GFX` (the logo's pieces) and
`ANTIFONT.BIN` (a proportional font) into them, takes IRQ 1, checks for a VGA (int 10h 1A00h), sets
mode 13h and its palette, and then, one step per retrace: the logo's frames from a table, the two
halves of the slogan sliding in from both sides until they meet, a slanted highlight swept across
(the pixels' colours raised by 10h in a band, lowered again behind it), 250 frames of rest, the end. A
mouse button ends it early. Holding A and B together shows a build string (date and time digits from
the file). It leaves the screen in mode 0Eh and ends with AL = 0Eh.

Three routines are never called (0ad8, 0c78, 0d1a). `src/sm/sm.c` ports the rest.

**Checked** as a whole: the harness's run (`tools/emu/run.py --exe SM.EXE --hashes`) and the port's
(`build/mm_run --hashes`) hash each frame's picture the same way. 99 of the harness's 109 pictures
appear in the port's sequence in the same order, the last held for the 250-frame rest; the ten others
are the harness catching the screen half drawn (its frame boundary falls between instructions, the
port's at the retrace). The harness counts about three of SM.EXE's frames per frame of its own: its
clock advances a tick per port read on top of the instructions, and SM.EXE polls the status register
tens of thousands of times a frame.
