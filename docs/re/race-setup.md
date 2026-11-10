# The race set-up and the track in memory

Internal notes. How a race is set up before its first frame (`Race_Setup` 1000:37fc and what it calls),
where the track and the vehicles' pictures live while it runs, and the tile animations. Ported in
`src/game/racesetup.c` and `src/game/track.c`; the files are described in docs/formats/ (track-map,
track-tiles, track-ct, track-col, track-dir, track-lev, track-brk, strt-pos, vehicles).

## Segment numbering

The buffer segments are operands of the program, relocated like every segment constant of the image
(the MZ relocation table covers them all, `mov ax, 1b78h` and `cmp cx, 1b88h` alike). The notes, the
names and the comments in the source use **the operands' numbering** (1b78, 3b78, 4478, 4d78, 5d78,
6d78), as in the rest of docs/re/; in memory with the image at 1000h each is 1000h higher (1b78 is at
2b78). The data segment is the exception: it is written 193c everywhere (its operand is 093c). The
source writes `LSEG(0x1b78)`. Confirmed: the recordings show DS = 7d78 for the CT read and DX = 2b78
for the loader. The VGA's A000 is not relocated.

## The order of the set-up

`Race_Main` (1000:3039) calls `Race_ClearRegs` and then `Race_Setup`; a carry back ends the race.

| Step | Routine | What |
|---|---|---|
| 1 | (37fc-38fa) | 255 `nop`s: code blanked in place (inferred: a removed check). No effect |
| 2 | `Race_Setup` | `[2630]` = 0; the smoothness `[263a]` at least 1; `Sys_ReadCrtcBase`; grid slots `[137c + rec]` = 3, 2, 1, 0 for vehicles 0..3; `[2682]` = FFFFh |
| 3 | `Track_SetNames` 3b50 | into GAME1; the digits into the names; `.BRK` and `.LEV` read; `[28bd]` the theme's terrain table |
| 4 | `Race_LoadTrack` 3c09 | race globals, STRT_POS.BIN, CHEATS.BIN, the map, the grid, the handling, who drives, every record reset, the CT file and the map expanded, the input slots |
| 5 | `Track_LoadColDir` 458b | ROUNDn.COL and ROUNDn.DIR |
| 6 | `Gfx_LoadBits` 482f | `..\BITSFILE.PH0` via 1b78 into `193c:3fe3` (6e00h bytes) |
| 7 | `Car_LoadGfx` 4611 | ROUNDnBR.VH0.. via 1b78, split into 4d78 and 5d78, mirrored |
| 8 | `Track_LoadTiles` 45e5 | ROUNDnBR.PR0.. into 1b78 (the graphic tiles); tile 0 copied to `193c:3ee3` |
| 9 | `Pal_FadeOut`, the screen | A000 cleared (7D00h words), int 10h 1001h BH = 0 (overscan colour 0) |
| 10 | (397a-39e6) | data set GAME1 only (`[042e]` = '1'): foreground bit on themes 2 and 3 (below) |
| 11 | `Race_DrawFrame`, `Pal_LoadRound`, `Pal_FadeIn`, driver AH = 7 | the first picture under a black palette, then the theme's palette faded in |

Any step's carry: `Snd_SilenceEngines`, driver AH = 8 and 6, AX = 1, CF set. The loaders' failure paths
close the handle in BX, which after a failed open holds DOS's error code (2, 3): the original closes that
number as a handle (a quirk, kept).

The order matters for the buffers: 1b78 is used three times in a row (the BITS file, the vehicle file,
then the tiles), each copy taken out before the next load.

## Memory during a race

| Where | Size | What | Written by |
|---|---|---|---|
| 1b78:0000 .. 3b78 | 128 KB | the graphic tiles: tile n (16 x 16, 256 bytes, row-major, palette indexes) at segment 1b78 + 10h n. PR0 at 1b78, PR1 at 2778, PR2 at 3378 (the loader advances 0C00h paragraphs, 192 tiles, per file) | `Track_LoadTileGfx` |
| 3b78:0000 .. 4d78 | 72 KB | the map: 192 x 192 words, 384 bytes a row; rows 0-95 in 3b78, 96-191 in 4478 (3b78 + 900h: the two blocks are contiguous). Entry = graphic tile number, bit 15 = drawn over the vehicles | `Race_LoadTrack`, `Race_Setup` (bit 15), the animations |
| 4d78:0000 | 4800h / C800h | the vehicle's 32 directions: 24 x 24 frames (576 bytes) or, theme 9, 40 x 40 (1600 bytes) | `Car_LoadGfx`, the mirrors |
| 5d78:0000 | 1B00h / 1F40h | the rest of the vehicle file: twelve more 24 x 24 frames, or five 40 x 40 (inferred: the other pictures of the vehicles; read by the drawing routines, package 8) | `Car_LoadGfx` |
| 6d78:0000 | 64 KB | the work segment: decoder output and packed input (c000), the CT file during the set-up, the palettes of the fades (0000, 0300), the back buffer (rows of 110h) | many |
| 193c:1b5b | <= 80h | the theme's `.LEV` (pointer in `[28b9]`, 0 if absent) | `Track_SetNames` |
| 193c:195b | <= 200h | the track's `.BRK` (pointer in `[28bb]`, 0 if absent) | `Track_SetNames` |
| 193c:1bdb | file | CHEATS.BIN (12-byte records, `Race_Pause`) | `Race_LoadTrack` |
| 193c:1eab | 144 | STRT_POS.BIN | `Race_LoadTrack` |
| 193c:2963 | 2 KB | the track's `.MAP`: tile bytes (2963), checkpoint numbers (2d63) | `Race_LoadTrack` |
| 193c:3163 | <= 480h | the theme's `.COL` (18 bytes a tile) | `Track_LoadColDir` |
| 193c:35e3 | <= 900h | the theme's `.DIR` (36 bytes a tile) | `Track_LoadColDir` |
| 193c:3ee3 | 100h | a copy of graphic tile 0 as loaded (the backdrop pattern) | `Track_CopyTileTable` |
| 193c:3fe3 .. ade3 | 6E00h | BITSFILE's small graphics | `Gfx_LoadBits` |
| 193c:5ee3 | 1400h | theme 8 only: the first 1400h bytes of 5d78 (five 32 x 32 frames, read by `Car_Draw32` 1000:847e), written over the middle of the BITS graphics | `Car_LoadGfx` |

COL and DIR each fit their space exactly for 64 tiles (64 x 18 = 480h, 64 x 36 = 900h): the six-bit
tile number of the map. The theme-8 copy at 5ee3 overwrites BITSFILE's bytes 1F00h..32FFh; whether the
race reads those on theme 8 is for package 8 to see (inferred: it does not).

## The vehicle records (as the set-up leaves them)

Four records, 164h bytes apart from `193c:124c` (vehicle 0 at offset 0, then 164h, 2c8h, 42ch); `[2660 +
2i]` holds the record offset of player i. The fields the set-up writes, by offset from the record
(confirmed: written here; meanings inferred from their readers' names in tools/ghidra/names.tsv):

| Offset | Set to | Meaning |
|---|---|---|
| 124c, 124e | [124e] = 1 for every vehicle, then 0 for vehicles 2 and 3 unless the game type `[2656]` is 1 (four racers), and for vehicles 1-3 on theme 9; copied to [124c] | present |
| 125c/125e, 1268/126a | the start x and y (both copies) | position, and the previous one |
| 127c, 127e | from the handling row | grip |
| 129c, 129e | top speed (and a copy to restore it) | |
| 12a0, 12a2, 12a4, 12a6 | from the handling row | reverse limit, acceleration, braking, drag |
| 129a | 2 on themes 6 and 7, else 3 | |
| 12ae | 0Ah | the state: waiting for the start (`Race_StateStart`) |
| 12eb | 1 if the control type is 6 | driven by the computer |
| 12ed | 3 | laps to go |
| 12f1, 12f3 | the start x, y | |
| 12fd (30h words), 135d (0Fh words) | FFFFh | two lists (read in the race; package 8) |
| 137c | the grid slot (0..3; bit 0: x + 1Ah, bit 1: y + 1Ah) | |
| 1380 | 1 | |
| the rest listed in `Race_LoadTrack` | 0 (12d8 and 138a = 1; 128e..1294 = -1Eh, 14h, 1Eh, -14h) | |

## The handling

Nine words per theme at `193c:252a + 12h (theme - 1)`: seven per vehicle (top speed, reverse,
acceleration, braking, drag, grip x, grip y) and two slide rates (`[28c2]`, `[28c4]`). The race number
`[28c1]` (0-based), the theme and the drivers' handicap bytes `[2668 + 2i]` adjust them:

- With the HANDICAP option (`193c:08a2`, read as `cs:9c62`): a vehicle whose byte has bit 7 gets a level
  L = (byte & 7Fh) + 1; its top speed loses C0h - 40h (L - 1) and its acceleration 0Ch - 4 (L - 1).
  Grips get `[24e0]` added for every vehicle.
- Without it: vehicle 0 is unchanged (level 0, grips + `[24e0]`). The others get a level c: the word of
  `193c:23dc` by the driver's byte, or in race 0 by grid place (0, 12, 6 for vehicles 1, 2, 3); then c -
  0Fh + race, + 4 on theme 7, + 8 on theme 8, + 7 on theme 2, + 3 in race 8, + 8 from race 13h on, - 1,
  - 8, + 14h, - 5 in races 15h, 16h, 17h, 18h. Top speed + 11c less the word of `193c:2462` by race;
  reverse + 4c; acceleration + c (and in head to head without handicap, from race 12h on, less a
  sixteenth, a quarter in race 17h); braking + c + 28h; grips + 2c and + 3c, + 28h.
- Race 17h: after each computer car vehicle 0's top speed loses 4Bh and its acceleration 2 (three times:
  as the original).

## Foreground tiles (GAME1, themes 2 and 3)

After the expansion `Race_Setup` sets bit 15 of every map word in a theme's range: 40h..13Ch on theme 2,
1..51h on theme 3 (signed compares; 0 excluded). `Race_DrawFrame` draws such tiles a second time after
the vehicles, so the vehicles pass under them. Only the first 4706h words of each 4800h-word block are
patched (rows 0-93 and the first 134 words of row 94, the same in rows 96-191): the last 58 words of
row 94 and all of row 95, and of rows 190-191, keep their tiles unflagged (a quirk, kept). Other data sets (GAME2..) get no flags.

## The tile animations (from `Race_DrawFrame`, once per presented frame)

| Theme | Routine | What |
|---|---|---|
| 1, 3, 5 | `Track_ScrollBackdrop` 8996 | graphic tile 0 rewritten from the copy at 3ee3, shifted by (camera mod 32) / 2 in x and y with wrap: a backdrop at half speed |
| 2 | `Track_AnimateWater2` 89e0 | `[26d1]` + 1; every four frames one of four sets of 16 tiles (00h, 10h, 20h, 30h) into the 4 x 4 block of map words at 4478:8046 |
| 8 (not track 1) | `Track_AnimateTiles8` 8a2b | `[26d3]` + 1; frames 0, 9, 12h (bits 2-3; 3 resets the counter) into 3 x 3 blocks of map words: one at 3b78:6624, or on track 3 six (3b78:5db4, 66b4, 78b4, 81b4, 4478:0cb4, 15b4) |

The animations write map words without bit 15, so a flagged tile there loses its flag (none of these
blocks are on themes 2 or 3's flagged ranges except theme 2's water, whose tiles 00h..3Fh are below the
range).

## Checks

Recorded in the harness (first race of the one-player game: theme 2, track 1; key script in the commit
and below) and passing `build/mm_fncheck`: see the commit messages. The key script that reaches the race:
Enter pressed at frames 100, 200, ..., 900 and released 5 frames later
(`100:1c,105:9c,200:1c,205:9c,...,900:1c,905:9c`, `--skip-card`); `Race_Setup` runs at frame ~714.
Other themes and tracks are recorded with the state poked into the original at `Race_Main` 1000:3039,
before `Race_Setup` loads the track: `--poke 193c:28bf=05,193c:28c0=01` (theme, track; `--poke-at`
chooses another address) on `tools/emu/run.py` and `tools/emu/capture.py`.
