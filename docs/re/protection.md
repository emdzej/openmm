# The code card

`FONT.BIN` (27 KB, despite its name a program) is the copy protection. `Sys_CodeCardCheck` (1000:31f0)
reads it whole into the overlay segment 2424:0000 and calls it far with AX = 0, before anything else of
the game; AX = 0 back is a pass, FFFFh a failure (the game exits). The sound driver is loaded over it
afterwards.

It switches to the 640 x 350 16-colour mode (10h), sets the attribute palette (int 10h AX = 1000h),
writes its instructions in English, French and German with the BIOS's write-string call (int 10h
AX = 1301h, the BIOS's 8 x 14 characters) and draws a grid of symbols through the VGA's planes. The
question, a column letter and a row number on the card in the box, comes from the BIOS tick at
0040:006C (the column from its low four bits, the row from bits 3-6). The player moves a box over the
grid with the cursor keys (int 16h) and presses Enter; the chosen symbol is compared with the card's
table in the file (2424:00ab, 00e9, 013a). Three tries.

## In the port

`src/protect/font.c` ports it routine by routine (names in `tools/ghidra/names-font.tsv`, addresses
FONT:offset). The machine gains the planes of mode 10h (`src/vga_planar.c`: map mask, read map, bit
mask, write modes and latches, as the program uses them) and the BIOS's write-string call, drawn with
the X11 misc-fixed 7x14 font (public domain, `tools/font8x14.py` makes `src/font8x14.c`), not IBM's
ROM font. The frame is shown at 640 x 350.

By default the port passes the check at once (as `--skip-card` does in the harness); the real check
runs with `--param codecard=1`. Off by default: the user's decision (AGENTS.md).
