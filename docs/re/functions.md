# The code segment's map

Every routine of `MICRO.EXE`'s code segment `1000` (`1000:0000`-`93bf`, 37,824 bytes), grouped into
modules, with what reaches what, the tables, the left-overs and a split of the porting work. Internal notes.
Addresses: seg:off with the image at 1000h. The names, modules and one-line descriptions are the record in
`tools/ghidra/names.tsv` (the full descriptions are there); this page is generated from it and from the map,
then annotated by hand.

## How the map is made

`tools/funcmap.py` disassembles segment 1000 by recursive descent (capstone) from `Start` (`1000:0006`),
following near jumps, conditional jumps, loops and near calls. Its seeds are Start, Ghidra's function list
(`re/micro.exe.funcs.txt`), the three interrupt handlers the program installs (int 08h, 09h, 0Ah), the six
input routines whose addresses `Input_SetSlots` stores, the pointer tables below and the dead routines
(listed so that they are named). A near call's target is a routine; a routine is its entry and everything it
reaches through jumps without crossing another entry (a jump to another entry is a tail jump, running into
the next entry a fall-through). Bytes no instruction covers are the unreached remainder, each region
explained in the tool (`REGIONS`).

`tools/emu/cover.py` runs the original in the harness and records the executed blocks of segment 1000 from
the first entry into Start after the unpack (the packed stub runs in the same segment before), and the
targets taken by every indirect call. Two runs (to the game options screen; then through the menus into a
race, Powerboats on the first bathtub track, 7,000 frames with the accelerator and steering held) executed
33,107 of the 37,616 code bytes (88 %). Every block they executed was already in the static map, and every
indirect target they saw is in the tables below: the static walk misses nothing the game ran. Without
Ghidra's list the walk finds the same bytes and 239 of the 243 entries (the other four are jump targets
Ghidra made functions: `Main_Restart` 00cc, `Joy_WaitButton` 2b93, and two labels inside `Car_StepAll`).

```sh
.deps/emu-venv/bin/python tools/emu/cover.py --frames 3000 --keys 650:1c,800:1c --out out/emu/cov
.deps/emu-venv/bin/python tools/funcmap.py            # tables, routines, unreached regions
.deps/emu-venv/bin/python tools/funcmap.py --modules  # per module: bytes, instructions, uses
.deps/emu-venv/bin/python tools/funcmap.py --routine 1000:3039
```

## Totals

- **243 routines** (entries), 228 of them live and 15 dead: no call, jump, table or stored pointer from
  live code reaches them (a search of the whole image for each dead entry's address as a word finds none;
  two, `1000:22b6` and `1000:3a7b`, are called only from other dead code).
- **37,616 bytes** decoded as code, **13,362 instructions**; **208 bytes** not code: padding, cs:
  variables, one cs: table, a few stray `ret`s (all listed under "Unreached").
- No undecodable bytes and no overlapping instructions: the code never jumps into the middle of an
  instruction.
- Six indirect call sites, all resolved; no indirect jumps, no jump tables, no far jumps. Far calls: 109 to the
  overlay at `2424:0000` (108 to the sound driver, one to FONT.BIN at `1000:3215`) and two through saved
  vectors: `1000:4901` (the old int 08h at `193c:260c`, chained by the timer handler every tick) and the
  int 0Ah handler's chain through `cs:4a8e`.

## Modules

Module sizes count each instruction once (code shared by two entries goes to the lower one). "Uses" lists
the other modules a module calls or jumps into. Ranges are runs of the module's routines in address order;
the program is laid out roughly by subsystem, but the front end and the race interleave.

| Module | Routines | Bytes | Instructions | Uses |
|---|---|---|---|---|
| main | 2 | 220 | 75 | menu, setup, snd, sys, timer, video |
| timer | 8 | 522 | 229 | - |
| keyboard | 1 | 156 | 61 | - |
| joy | 9 | 434 | 188 | menu, video |
| mouse | 1 | 106 | 40 | - |
| input | 5 | 258 | 88 | ai, joy, mouse |
| sys | 8 | 395 | 167 | timer |
| snd | 7 | 687 | 240 | - |
| file | 2 | 87 | 41 | - |
| pal | 7 | 456 | 209 | - |
| unpack | 3 | 648 | 369 | - |
| video | 29 | 1743 | 863 | unpack |
| text | 5 | 262 | 142 | - |
| hud | 4 | 711 | 236 | video |
| menu | 45 | 8460 | 2932 | input, joy, pal, race, setup, snd, sys, text, unpack, video |
| setup | 2 | 494 | 197 | file, input, joy, menu, sys, text, video |
| race | 20 | 5953 | 2025 | car, file, hud, input, main, menu, pal, snd, sys, track, video |
| track | 10 | 1060 | 503 | unpack |
| car | 31 | 9794 | 3024 | coll, hud, race, sys, terrain, track, unpack, video |
| coll | 3 | 830 | 261 | - |
| terrain | 25 | 3223 | 1051 | car, sys, track |
| ai | 1 | 265 | 87 | - |
| dead | 15 | 852 | 334 | hud, menu, pal, text, video |
| **total** | 243 | 37616 | 13362 | |

### main

2 routines, 220 bytes, 75 instructions. Range: 0006-00e2. Uses: menu (7), snd (4), sys (3), setup (2), timer (1), video (1).

| Address | Name | What |
|---|---|---|
| `1000:0006` | Start | the entry: a start-up check, DS = 193c, SS:SP = 7d78:03e8, mode 13h, then the loop of 1000:0100 and 1000:0220 ... |
| `1000:00cc` | Main_Restart | longjmp-style abort to the menus: DS = ES = 193c, SS:SP reloaded from [0004]/[0006] (saved by Start at 0054), 00e2 ... |

### timer

8 routines, 522 bytes, 229 instructions. Ranges: 485b-49af, 4a21-4ade. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:485b` | Timer_Measure | the frame in PIT counts between two retrace starts: AX = CX = first - second count, DX = the second |
| `1000:489c` | Timer_Irq | the IRQ 0 handler, locked to the retrace: unless IRQ 2 works (cs:[4a92]), PIT to FFFFh, wait for the retrace, reload [2614] ... |
| `1000:490d` | Timer_Install | once ([2618]): measures the frame into [2612], reload [2614] = it - 96h, [2610] = 123428h / it, saves the IRQ 0 vector in [260c], installs Timer_Irq ... |
| `1000:4966` | Timer_Remove | the old IRQ 0 vector back, the PIT at 65536 ([2618] left set) |
| `1000:498a` | Irq_Install | Timer_Install, Retrace_IrqInstall, the IRQ 1 vector saved in [2925] and Key_Irq installed |
| `1000:4a21` | Retrace_IrqInstall | saves CRTC register 11h in cs:[4a8d] and the IRQ 2 vector in cs:[4a8e], installs Retrace_Irq, enables the VGA's retrace interrupt; cs:[4ade] = FFh |
| `1000:4a6c` | Retrace_IrqRemove | the old IRQ 2 vector back, CRTC register 11h rewritten as the install writes it |
| `1000:4a94` | Retrace_Irq | the IRQ 2 handler: if 3C2h bit 7 (the VGA's retrace interrupt) clears and acknowledges it, cs:[4a92] = 1, counts cs:[4ade] ... |

### keyboard

1 routines, 156 bytes, 61 instructions. Range: 2efd-2f99. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:2efd` | Key_Irq | the IRQ 1 handler: reads the scan code, acknowledges it, tracks the E0 prefix in cs:[2f99], the last press [107f] and release [107e] ... |

### joy

9 routines, 434 bytes, 188 instructions. Ranges: 2b8b-2be8, 2e6c-2efd, 2f9e-3030, 3a12-3a44. Uses: video (1), menu (1).

| Address | Name | What |
|---|---|---|
| `1000:2b8b` | Joy_WaitButtonA | entry of 1000:2b93 with BL = 30h (joystick A's buttons) |
| `1000:2b8f` | Joy_WaitButtonB | entry of 1000:2b93 with BL = C0h (joystick B's buttons) |
| `1000:2b93` | Joy_WaitButton | waits for the buttons in BL (bits of port 201h, active low) to be released, then shows the place-the-joystick prompt ([019a] = 0df8 ... |
| `1000:2e6c` | Input_JoyA | input slot joystick A from the last timing: any button = 08h; X [108d] <= [28fd] = 80h, >= [28ff] = 40h ... |
| `1000:2eb3` | Input_JoyB | the same as 1000:2e6c for joystick B: buttons [1095] bits 6-7, X [1091] against [2905] [2907], Y against [2909] [290b] dropped. Out: AL |
| `1000:2f9e` | Joy_TimeA | starts the game port's one-shots (out 201h) and counts, interrupts off, how long each of the four axis bits stays high (SI, DI, BX ... |
| `1000:2fcc` | Joy_TimeB | the same loop ending on joystick B's axes. Out: SI = B X, DI = B Y, AL = the port |
| `1000:2ffe` | Joy_TimeBoth | the same loop ending when all four axes drop. Out: SI, DI = A X, Y; CX, DX = B X, Y; AL = the port |
| `1000:3a12` | Joy_Detect | [2625] = the number of joysticks: BIOS int 15h 84h DX = 1 reads the axes; A counts when AX > 5 or BX >= 5, B when CX > 5 or DX >= 5 ... |

### mouse

1 routines, 106 bytes, 40 instructions. Range: 2e02-2e6c. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:2e02` | Input_Mouse | input slot mouse (int 33h 3): left button = 28h, right = 10h; X beyond 160 -/+ [290d] = 80h/40h, Y beyond 100 -/+ [290d] = 20h/10h ... |

### input

5 routines, 258 bytes, 88 instructions. Range: 2d00-2e02. Uses: joy (5), mouse (1), ai (1).

| Address | Name | What |
|---|---|---|
| `1000:2d00` | Input_SetSlots | fills the four input slots [1083] [1085] [1087] [1089] from the control types [2658] [265a] [265c] [265e]: 4 keys 1 (1000:2dfa) ... |
| `1000:2d5b` | Input_Read | once per frame: times the joysticks [108c] asks for (1 A: 1000:2f9e, 2 B: 1000:2fcc, 3 both: 1000:2ffe) into [108d] [108f] (A X ... |
| `1000:2ded` | Input_None | input slot for a vehicle with no device: AL = 0; while a race runs ([1082] != 0) the computer drives it (1000:5429, BX = the vehicle record) |
| `1000:2dfa` | Input_Keys1 | input slot keys 1: AL = [107d] (the high byte of the key mask: scan codes [106c]..[1073]) |
| `1000:2dfe` | Input_Keys2 | input slot keys 2: AL = [107c] (the low byte: scan codes [1074]..[107b]) |

### sys

8 routines, 395 bytes, 167 instructions. Ranges: 3165-3171, 319f-321c, 35be-35bf, 3ad0-3b50, 49af-4a21, 4adf-4aee. Uses: timer (2).

| Address | Name | What |
|---|---|---|
| `1000:3165` | Sys_WaitTick | waits until the tick counter cs:[4ade] (advanced by the timer handler 1000:489c) changes. All registers but AL kept |
| `1000:319f` | Sys_ReadCrtcBase | [2643] = the CRTC base port from the BIOS data area (0040:0063). Keeps ES, SI |
| `1000:31b6` | Sys_ClearRegs | zeroes EAX..EDI (32-bit: needs a 386) and their 16-bit halves, CLD, CLC, STI |
| `1000:31f0` | Sys_CodeCardCheck | the start-up protection: reads FONT.BIN (name [1241], open mode C0h ... |
| `1000:35be` | Sys_Ret | a bare ret: the null entry of the terrain and vehicle-state handler tables (193c:26e9.., 193c:278f) |
| `1000:3ad0` | Sys_SpeedTest | the CPU benchmark behind smoothness AUTO: CRTC base from 0040:0063 (into [2643]) ... |
| `1000:49af` | Sys_RemoveIrqs | the timer back (1000:4966), int 09h from [2925]; DOS's time and date set again from the real-time clock (int 1Ah 02h, 04h, BCD through 1000:4adf ... |
| `1000:4adf` | Sys_BcdToBin | AL (BCD) -> AL (binary); BX and AH used |

### snd

7 routines, 687 bytes, 240 instructions. Ranges: 00e2-00f3, 321c-3279, 7a97-7cdb. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:00e2` | Snd_StopAll | unless [0f64] = 1 (no sound driver, inferred), calls the driver with AH=10h AL=0; all registers but AX kept |
| `1000:321c` | Snd_LoadDriver | loads DRIVERn.BIN, n = '0' + [0f64] patched into the name at [11e0], into 2424:0000 (over FONT.BIN), cs:[3279] = 0 meanwhile ... |
| `1000:7a97` | Snd_InitEngines | at the start of a race: with a sound device other than type 1 ([0f64] != 1) sets the engine tones of driver channels 0 and 1 (AH=0Eh, CX=32h) ... |
| `1000:7af8` | Snd_SilenceEngines | silences the engine notes: with device type [0f64] != 1 driver AH=10h with CX=0 on channels 0 and 1 ... |
| `1000:7b46` | Snd_UpdateEngines | once per presented frame (end of 1000:90c5): nothing while any vehicle is in state 0Bh ... |
| `1000:7c5b` | Snd_SetEngineTone | AL = driver channel, BX = vehicle: tone = 2000h - (2 * abs(speed [127a]) capped at 7ffh, + 7f7h airborne ... |
| `1000:7cae` | Snd_Random | pseudo-random AX from a 4-byte state in the code segment (cs:7cdb..7cde, read and written as three overlapping words at 7cdb, 7cdc, 7cdd with xor ... |

### file

2 routines, 87 bytes, 41 instructions. Ranges: 0f99-0fbf, 35bf-35f0. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:0f99` | File_LoadGameSet | opens the file named at DS:SI (a GAMEn.LVL: called from 2be8 with 193c:0889 "GAME1.LVL" and with the names found by its directory scan) and reads its ... |
| `1000:35bf` | File_SaveScreen | debug: bumps the digit [292d] of the name at [2929] and writes 6d78:0000 (FFFAh bytes, the frame buffer) to that new file. All registers kept |

### pal

7 routines, 456 bytes, 209 instructions. Ranges: 07a0-07e5, 26ec-2706, 327a-3333, 4758-4808. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:07a0` | Pal_LoadFile | opens the file named at DS:DX, reads 300h bytes into 6d78:0000 ... |
| `1000:26ec` | Pal_LoadIntro | loads INTRO.PAL ([0d62]) into 6d78:0000 and sets the DAC from it (07a0, which also clears the text screen) ... |
| `1000:327a` | Pal_FadeOut | once (flag [26ce] set to 1): reads the DAC (int 10h 1017h) into 6d78:0000 and runs 126 steps of 1000:32ae ... |
| `1000:32ae` | Pal_FadeOutStep | one fade-out step, CX = the step: on even CX lowers each nonzero byte of 6d78:0000..02ff by one ... |
| `1000:32ce` | Pal_FadeIn | only after a fade out ([26ce] = 1, cleared): zeroes the working palette 6d78:0300..05ff and runs 128 steps of 1000:3303 towards the target palette at ... |
| `1000:3303` | Pal_FadeInStep | one fade-in step, CX = the step: on even CX raises each byte of the working palette (DS:0300, DS = 6d78) that is below its target (DS:0000) by one ... |
| `1000:4758` | Pal_LoadRound | reads ROUNDn.PAL (193c:1200, 300h bytes) into 6d78:0000; for theme 3, track 2 or 1 ([28c0]) ... |

### unpack

3 routines, 648 bytes, 369 instructions. Range: 3333-35be. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:3333` | Unpack_Decode | the COMPRESS.PI* (and other packed files) decoder: from 6d78:c000 into 6d78:0000 ... |
| `1000:3533` | Unpack_SwapState | exchanges AL with cs:[3544] and the flags with cs:[3545]: called in pairs around each code of 1000:3333 so that the control byte and its flags ... |
| `1000:3547` | Unpack_LoadSeries | loads numbered packed files: ES:DI.. of 6d78 is zeroed (FFFFh bytes) first ... |

### video

29 routines, 1743 bytes, 863 instructions. Ranges: 049c-0793, 07e5-08e5, 482f-485b, 7a75-7a94, 8bab-8d9e, 8dc0-8dfc, 9289-92eb. Uses: unpack (1).

| Address | Name | What |
|---|---|---|
| `1000:049c` | Gfx_InitObject | fills a sprite object (BX) from its image record ([BX] -> record: width +0dh, height +0fh, segment +12h): [BX+8] segment, [BX+0f] width ... |
| `1000:04b8` | Gfx_DrawObjectSaved | clips the object (0630; carry = off screen: returns), saves the background under it (05f3) and falls into the transparent draw (04bd); BX = object |
| `1000:04bd` | Gfx_DrawObjectMasked | second entry of 04b8 (clip already done): copies the clipped frame (image segment [BX+8], offset [BX+0d], row skip [BX+17], rows [BX+19] ... |
| `1000:053a` | Gfx_DrawObject | clips the object (0630; carry = off screen) and copies its frame opaquely into the back buffer 6d78 (words then a byte per row ... |
| `1000:05b4` | Gfx_RestoreUnder | if the object was drawn ([BX+15] != -1), copies the saved background back from segment [BX+6] into the back buffer at [BX+15] and marks it undrawn ... |
| `1000:05f3` | Gfx_SaveUnder | if the object is on screen and has a save segment ([BX+6] != 0), copies the back-buffer rectangle at [BX+15] (rows [BX+19] ... |
| `1000:0630` | Gfx_ClipObject | clips an object (BX: x [BX+2], y [BX+4], frame [BX+13], frame size [BX+0b], width [BX+0f] ... |
| `1000:06cc` | Gfx_FrameObject | draws a one-pixel rectangle of colour AL around the object's clipped area (one pixel outside it) in the back buffer; registers kept |
| `1000:0710` | Gfx_DrawTileMap | draws a map of 8 x 8 tiles into the back buffer at DI (+888h): SI = the map's image record (segment +12h holds columns, rows ... |
| `1000:07e5` | Gfx_RecolourRect | in the back buffer rectangle at x AX, y BX, SI wide, CX high, replaces every pixel of colour DH by DL |
| `1000:0823` | Gfx_FillRect | fills the back buffer rectangle at x AX, y BX, DX wide, CX high with colour SI (low byte); registers kept |
| `1000:0862` | Gfx_FillRows | fills CL full rows (256 pixels) of the back buffer from row BX with colour AL (rep stosd: needs a 386); joins 0876's loop |
| `1000:0876` | Gfx_ClearBuffer | fills all 200 rows of the back buffer 6d78 with colour AL; second entry 0862 shares its loop |
| `1000:089c` | Gfx_PresentRows | copies CX rows from row AX of the back buffer (stride 110h) to the VGA screen A000 (stride 140h ... |
| `1000:08bc` | Gfx_Present | copies the whole 256 x 200 window of the back buffer 6d78 to A000 at x 32; registers kept |
| `1000:482f` | Gfx_LoadBits | loads ..\BITSFILE.PH0 (193c:1231) through 1000:3547 into 1b78 and copies its first 6e00h bytes to 193c:3fe3..ade3 (the race's small graphics ... |
| `1000:7a75` | Copy_Patch32 | patches the copy loop at 1000:91a4 to move doublewords (66 a5, movsd, written over its four movsw) and sets cs:[92d9] = 40h, cs:[92dc] = a566h ... |
| `1000:8bab` | Gfx_ClipSprite | DI = x, AX = y (screen, 0..ffh x 0..dfh window), CX = width, DX = height, SI = source: clips the rectangle to the window ... |
| `1000:8c3c` | Gfx_BlitSilhouette | after 1000:8bab: draws the sprite's non-zero pixels in colour 0 into the back buffer (ES = 6d78, stride 110h); shadows and the blink |
| `1000:8c6a` | Gfx_BlitRecolour | after 1000:8bab, AL = colour offset: draws non-zero pixels, adding AL to those whose low nibble is 0..2 (the vehicles' team colours) |
| `1000:8ca4` | Gfx_BlitSprite | after 1000:8bab: draws the sprite's non-zero pixels (colour 0 transparent) into the back buffer, stride 110h |
| `1000:8cd0` | Gfx_DrawSmall8 | CX = frame, SI = sheet, DI/AX = screen centre: an 8 x 8 frame (frame * 40h) through 1000:8ce4 |
| `1000:8ce4` | Gfx_DrawUnclipped | DI = x, AX = y, DL = width, DH = height, SI = source: rejected whole if x outside -17h..100h or y outside -17h..c8h ... |
| `1000:8d09` | Gfx_BlitAt | DI = back-buffer offset (before the cs:[8994] adjustment), DL = width, DH = height, SI = source: transparent copy, stride 110h ... |
| `1000:8d36` | Gfx_DrawSmall8Black | as 1000:8cd0 but in colour 0 (1000:8d4a): the shells' shadows |
| `1000:8d4a` | Gfx_DrawUnclippedBlack | as 1000:8ce4 but writes colour 0 for the non-zero pixels |
| `1000:8dc0` | Gfx_BlitRecolourAt | DI = back-buffer offset, DL = width, DH = height, BL = colour offset, SI = source: transparent copy adding BL to pixels whose low nibble is 1 or 2 |
| `1000:9289` | Gfx_DrawBanner | SI = banner (DS), DI/AX = screen centre: an 88 x 22 sprite (21 rows for 193c:9d63) centred, drawn plain or ... |
| `1000:92bc` | Gfx_PresentScrolled | copies the back buffer to the screen: 200 rows of 256 bytes from 6d78:1110 - cs:[8994] (stride 110h) to A000:0020 (stride 140h ... |

### text

5 routines, 262 bytes, 142 instructions. Ranges: 08f0-09e0, 1a34-1a4a. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:08f0` | Text_DrawNth | skips CX zero-terminated strings from SI and draws the next one: centred (0910) if AX = -1, else at x AX (0929); BX = row, DX = font record ... |
| `1000:0910` | Text_DrawCentred | draws the string at SI centred in the 256-pixel window (x = 127 - 4 * length): BX = row, DX = font record |
| `1000:0929` | Text_Draw | draws the zero-terminated string at SI in the back buffer: AX = x (negative: clipped on the left, whole characters skipped), BX = y ... |
| `1000:0999` | Text_DrawGlyph | draws one 8-pixel-wide glyph (AL = character: 0-9, A-Z, ! and ? map to glyphs 0-37) of AH rows from segment DX at ES:DI, colour 0 transparent |
| `1000:1a34` | Text_TwoDigits | AL = 0..99: two ASCII digits into [SI], [SI+1]; a leading zero becomes a space; registers kept |

### hud

4 routines, 711 bytes, 236 instructions. Range: 8dfc-90c3. Uses: video (5).

| Address | Name | What |
|---|---|---|
| `1000:8dfc` | Hud_DrawPositions | each presented frame: theme 9 shows the time left [26c8] / 16 as digits at the top ... |
| `1000:903f` | Hud_DrawCarIcon | BX = vehicle, DI = buffer offset: its 16 x 16 icon (193c:5363, or 5be3 when out of laps) recoloured by [bx+1252] ... |
| `1000:905f` | Hud_DrawDigit | CX = digit (0Ah a sign), DI = buffer offset: an 8 x 16 glyph from 193c:5463 + digit * 80h |
| `1000:9076` | Hud_DrawCarPosition | vehicle bx: its race position [12ef] as a 16 x 8 label (193c:5ce3 + (position - 1) * 80h) above the vehicle, lifted by its height |

### menu

45 routines, 8460 bytes, 2932 instructions. Ranges: 0100-045b, 09e0-0f6d, 0fbf-10a0, 11f8-1a34, 1a4a-1a82, 1aad-2099, 2193-22b6, 2329-26ec, 2770-2ab5, 92f0-93bf. Uses: video (85), text (42), input (14), pal (7), race (4), sys (4), snd (3), unpack (1), joy (1), setup (1).

| Address | Name | What |
|---|---|---|
| `1000:0100` | Menu_Attract | the attract screens (the first half of Start's loop): DS = ES = 193c, 7af8, driver AH=7, 8, 6, tune 1 (AH=9 AL=1, then AH=4 if it answers nonzero) ... |
| `1000:01de` | Menu_DrawAttractVehicle | draws INTRO.CHR (object 0bb2, frame [bc5], x [bb4] = 50h, y [bb6]) opaquely ... |
| `1000:0220` | Menu_SelectPlayers | the second half of Start's loop: tune 1, clears [0156] and [1096], draws the SELECT GAME / ONE PLAYER / TWO PLAYER screen (LOGO.CHR ... |
| `1000:02e0` | Menu_OnePlayerGame | the one-player game screen: 0400 backdrop, headings, two game types as SELGAM/WORDS frames ... |
| `1000:0382` | Menu_ChooseOfTwo | a two-way choice with THUMB.CHR (object 0c6f) as the pointer: BX = the pointer's row (into [0c73]) ... |
| `1000:0400` | Menu_DrawBackdrop | the menus' common backdrop: back buffer cleared (0876 colour 0), BADGE.CHR (object 0b7c), the WORDS.CHR title (object 0ca5 at 48h,8 frame 0) and ... |
| `1000:09e0` | Menu_SelectCharacter | the "who do you want to be / race" carousel of the eleven drivers: AX = start slot (-1 keeps [0192]), SI = the prompt (into [019a] ... |
| `1000:0b51` | Menu_AskHandicap | for drivers 0-2 (Walter, Mike, Anne; AX = driver) unless [2656] = 1 or [265a] = 6: asks HANDICAP name ? with NO/YES (left/right ... |
| `1000:0c15` | Menu_PressAnyKey | the press-a-key prompt (string 0241 at row 62h, blinking through 0c96) until fire, any key released ([107e]) or 2bch ticks of [261f] |
| `1000:0c5d` | Menu_ShowClassName | writes the vehicle class [09d8] (1-based) centred at row bfh from list 002f or, if [26cf] bit 0 is set, list 00ab, and shows those 8 rows ... |
| `1000:0c96` | Menu_BlinkMessage | redraws the menu message line: string [019a] (none if 0), font [0194] (8 rows for 0b40, else 16) at row [0196] ... |
| `1000:0cd3` | Menu_TurnCarousel | turns the drivers' carousel by one slot (64 units of [0192], modulo 2c0h) in the direction of BX's sign, in 13 eased steps (table at 0185) ... |
| `1000:0d1f` | Menu_DrawCarousel | draws the carousel: background bands (colours 0eh, 12h, 0) at rows 77h-b7h ... |
| `1000:0db0` | Menu_DrawFace | draws a face object (BX): [BX+13] low byte = driver, high byte = expression (0 FCNORMAL, 1 FCFROWN, 2 FCHAPPY, 3 FCSAD ... |
| `1000:0e02` | Menu_DrawCarouselFrame | draws FRAME.CHR (object 0d47) pieces, plain and mirrored ([BX+0a]), around the carousel window (x 5ch-9ch ... |
| `1000:0eba` | Menu_ResetCharacters | resets the drivers' selection: [0160] = 6, [0192] = 0, [0406]-[0409] = 3, the pool [0164] = 0..10 ... |
| `1000:0f17` | Menu_DrawFaceLabel | unless the object BX is off screen, writes the 8-character entry number ([BX+13] and 0fh) of the table at AX (0258 names, 02b1 ratings) at x [0bea] ... |
| `1000:0f3c` | Menu_DrawFaceName | writes the driver's name (table 0258, entry [BX+13] and 0fh) 31h rows below the face object BX, or clears that 48-pixel strip if no driver (>= 0bh) ... |
| `1000:0fbf` | Menu_HeadToHeadGame | one-player "head to head" game (choice 1 of the one-player game screen, from 02e0): [03f8] = 1 (head-to-head flag), [2656] = 2 (two racers) ... |
| `1000:102b` | Menu_ChampionshipGame | one-player championship (choice 2 of the one-player game screen): [03f8] = [03f3] = [0342] = [0310] = 0, [2656] = 1 (four racers), [1080] = 137b ... |
| `1000:11f8` | Menu_PreRaceScreen | the screen before a race: music 4 (5 for race 1ah) through the driver (AH = 9 asks, AH = 4 plays) ... |
| `1000:13e4` | Menu_RaceResults | after a race of the one-player series; carry = game over. Head to head: a lost race costs a life (1c1b cx=2), carry when [0406] = 0 ... |
| `1000:16de` | Menu_RacerIsOut | AX = a racer object (0c1e..0c54): its character's roster byte [0164 + n] gets 20h (out), the object 40h; music 6 ... |
| `1000:179b` | Menu_WaitKey | waits for the fire bit (8) of [108b] to be released and pressed again, or any key released ([107e] <> 0) ... |
| `1000:17ff` | Menu_WaitKeyBlink | CX = ticks: waits up to CX ticks of [0002] (reset) for fire released, then up to CX more for fire pressed ... |
| `1000:1867` | Menu_DrawTrackName | BX = row: the name of track [28c0] of theme [28bf] (entry (theme - 1) * 4 + track - 1 of the zero-separated list at [0460], font 0b54) centred ... |
| `1000:18d8` | Menu_ProgressMap | between championship races (not race 0, not when [043a] = 0): the map of the races so far (198e), presented ... |
| `1000:198e` | Menu_DrawProgressMap | draws the championship map: the tile picture CASE.MAP / CASE.CHR (0710 with descriptors 0ab4, 0aa0 ... |
| `1000:19f2` | Menu_DrawRacers | clears the screen with the title (0400) and draws the racers' portraits in a row at y = 24h, 40h apart from x = 8: objects 0c03..0c54 (four) ... |
| `1000:1a4a` | Menu_PickOpponents | the portraits (19f2); for each racer object 0c03..0c54 whose frame is 0bh (an empty place) the player picks a character for it ("pick ... |
| `1000:1aad` | Menu_ChampionScreen | BX = the winning racer object: music 3; CHAMPIONSHIP WINNER!! ([0384]) slides in from the left ([03aa] from -0b0h to 28h) and the winner's name from ... |
| `1000:1c1b` | Menu_LivesMessage | CX = message: 0 failed-to-qualify, 1 qualified-for-challenge (5 qualified-for-head-to-head when [03f8]), 2 life-lost, 3 EXTRA LIFE ... |
| `1000:1e20` | Menu_TwoPlayerGame | the two-player game (from 0220): the two small pictures (0cdb, 0cf6) placed, [03f3] = [0156] = [03f8] = 1, [2656] = 2, [09d6] = 0, [265a] = [0f61] ... |
| `1000:1ef1` | Menu_TwoPlayerChoose | music 2; the four win tallies [0989..098c] and [08a2] cleared ... |
| `1000:1faf` | Menu_TwoPlayerTournament | the two-player tournament: [0988] = [08a5] = 1, [28c1] = 1; each race a track drawn at random from the eight entries at [09ba] (theme * 4 + track - ... |
| `1000:2193` | Menu_StepVehicleClass | BX = +1 / -1 / 0: music 2; moves [08a3] through 0..9 (wrapping) and takes the pair at [09d9 + 2n]: [09d8] = the vehicle class (1..11 ... |
| `1000:2216` | Menu_ClassBanner | clears the rows under objects 0bec and [0bb6], writes the vehicle class name ([09d8] - 1 of the list at [002f], font 0b40, centred) ... |
| `1000:2329` | Menu_TwoPlayerSingleRace | two-player single races: [08a5] = [0988] = 0, music 2; the two racers' header (240a at y 24h, 2481) and a vehicle picture (object 0c6f) ... |
| `1000:240a` | Menu_TwoPlayerHeader | AX = row: the title (0400) and the two racers' portraits (objects 0c03 at x 20h and 0c1e at x b0h, frames masked to 4fh, boxes 0dh ... |
| `1000:2481` | Menu_TwoPlayerRatings | for both racers: the character's wins [09a4 + n] and losses [09af + n] as WON nn / LOST nn beside the portrait ... |
| `1000:256e` | Menu_TwoPlayerResult | after a two-player race: the winner ([03fc] = 0c03 or 0c1e) gets a tally ([098a] / [098c]) and its character a win ... |
| `1000:26c0` | Menu_LoadFrontEnd | reloads the front end after a race (and at start-up): interrupts off ... |
| `1000:2770` | Menu_GameOptions | the game options screen. Once per run ([0eff]) reads SETTINGS.DAT (name at [0ed0]) into [0da8] and spreads it: [0f5f] player 1 control ... |
| `1000:2a82` | Menu_Credits | clears the screen (1000:0400), draws the 10 credit lines of the text table at [0f75], presents and waits for any released key ([107e]) |
| `1000:92f0` | Menu_RedefineKeys | the REDEFINE KEYS screen: prompts for five keys per player (two prompts, 193c:ae3d / ae44) ... |

### setup

2 routines, 494 bytes, 197 instructions. Ranges: 2ab5-2b8b, 2be8-2d00. Uses: text (3), menu (2), joy (2), video (2), sys (1), file (1), input (1).

| Address | Name | What |
|---|---|---|
| `1000:2ab5` | Setup_CalibrateJoysticks | joystick calibration: prompts centre, left, right for joystick 1 (strings at [0ded] [0dd3] [0dda] [0ddf] ... |
| `1000:2be8` | Setup_ChooseGame | the data-set chooser: lists the GAME?.LVL files (find first/next, DTA at [0d7d], pattern [0d71]) ... |

### race

20 routines, 5953 bytes, 2025 instructions. Ranges: 10a0-11f8, 1a82-1aad, 216c-2193, 3030-3165, 35f0-3a12, 3c09-458b, 7759-79fd, 849b-8711, 90c5-9289. Uses: menu (9), snd (9), video (8), car (8), track (6), sys (4), pal (4), main (2), input (2), file (1), hud (1).

| Address | Name | What |
|---|---|---|
| `1000:10a0` | Race_Series | the one-player series of races (championship or head to head): [03fa] = 3 (wins before a bonus race), [0343] = [28c1] (race number) = [0310] = 0 ... |
| `1000:115c` | Race_RunOne | sets up and runs one race of the series: the theme [28bf] and track [28c0] (1-based) from the race list byte [043c + [28c1]] (theme * 4 + track - 1) ... |
| `1000:11d5` | Race_ResultsToRacers | converts the race's finishing order [2678..267e] (vehicle record offsets ... |
| `1000:1a82` | Race_BonusRace | a bonus race: [0343] = 1 so that 115c runs theme 9, track [0342] + 1 ... |
| `1000:216c` | Race_RunTwoPlayer | runs a two-player race: [1082] = 1, the driver's music stopped (AH = 7), the race (3039, DS saved), [1082] = 0, the front end reloaded (26c0) ... |
| `1000:3030` | Race_ClearRegs | DS = 193c, then 1000:31b6 (zeroes the registers) |
| `1000:3039` | Race_Main | the race: set-up (1000:3030, 1000:37fc; CF there ends it), 1000:7a97, then per frame: Esc ([1096]) ends it; input (1000:2d5b) ... |
| `1000:35f0` | Race_Pause | pause: driver AH = 8 and 6; looks the paused position up in the 12-byte cheat records at [1bdb]..[1fcb] (theme [28bf], track [28c0] ... |
| `1000:37fc` | Race_Setup | starts with 255 nops (37fc..38fa); then [2630] = 0, smoothness at least 1, CRTC base (1000:319f), the vehicles' indices [137c]. ... |
| `1000:3c09` | Race_LoadTrack | the race set-up, called once per race by 1000:37fc: resets the race globals (193c:264e/2650 camera speeds = 8, 26c6, 2911, 2913, 26b4.. ... |
| `1000:7759` | Race_AwardPoint | two-player point when [2911] = 1 (called by the race frame 1000:90c5): picks the scorer (bx) and the loser (si) from the vehicle that dropped out ... |
| `1000:78f8` | Race_RespawnPlayers | when [2913] is set (both players fell at once, 1000:5e4e) or from 1000:7429: if only one of the two players is in state 1 or 5 the other is switched ... |
| `1000:849b` | Race_StateStart | vehicle state 0Ah (set by the race set-up 1000:3c09): [26ca] = 1; for the player (or both in head-to-head) adds [263a] (steps per frame) to [26d5] ... |
| `1000:851f` | Race_StateScored | head-to-head, vehicle state 0Bh (the scorer) and from the race frame: silences engines, spins the scorer [26b8] by 8 per frame (not in theme 9) and ... |
| `1000:855a` | Race_StateLostPoint | head-to-head, vehicle state 0Ch and from the race frame and the race loop's end: silences engines; for the scorer [26b8]: theme 8 spins it ... |
| `1000:8634` | Race_DrawBannerOut | while [26c2] < 3: slides the banner 193c:7423 left by 8 per frame from [26be] ... |
| `1000:8683` | Race_StateCourseDone | vehicle state 0Fh (theme 9: set at 1000:4b13 when vehicle 0 has reached the lap count 2 in [12ed] and stopped ... |
| `1000:86a8` | Race_StateTimeUp | vehicle state 10h (theme 9's time limit ran out, 1000:7429): effect 0Fh, the banner 193c:9523 dropped from the top (1000:86d2), [291d] = 0 ... |
| `1000:86d2` | Race_DropBanner | SI = banner: draws the vehicle, then the banner at x = 80h dropping 8 per frame from the top ([26c0] ... |
| `1000:90c5` | Race_DrawFrame | the race picture, only on the last game step of a presented frame ([2638] = 1): ES = back buffer 6d78 (rows of 110h) ... |

### track

10 routines, 1060 bytes, 503 instructions. Ranges: 3b50-3c09, 458b-4611, 4808-482f, 585b-5921, 8996-8b8e. Uses: unpack (1).

| Address | Name | What |
|---|---|---|
| `1000:3b50` | Track_SetNames | changes directory to GAME1 ([042a]); patches the theme digit ([28bf] + '0') into the per-theme file names and theme + track ([28c0]) into ROUNDnm.MAP ... |
| `1000:458b` | Track_LoadColDir | reads the theme's ROUNDn.COL into 193c:3163 and ROUNDn.DIR into 193c:35e3 (whole files, int 21h 3Dh AL = c0h); carry set on an open failure ... |
| `1000:45e5` | Track_LoadTiles | the theme's tile graphics: 1000:45ef, then (if no carry) 1000:4808 copies the first 256 bytes of the result into 193c:3ee3; carry clear |
| `1000:45ef` | Track_LoadTileGfx | loads ROUNDnBR.PR0.. (193c:11e6) through the packed-file loader 1000:3547 into the buffer at 1b78:0000; registers kept, carry clear |
| `1000:4808` | Track_CopyTileTable | copies 80h words from 1b78:0000 (the unpacked tile file's head) to 193c:3ee3; registers kept |
| `1000:585b` | Track_SurfaceAt | the surface byte under a vehicle: 6 x 6 grid of 16-px cells per tile at 193c:35e3 + tile [12cc] * 24h, cell from [12ce] (bh = x/8, bl = y/8, halved) ... |
| `1000:589c` | Track_TileAt | ax = world x, dx = world y (0..0bffh): tile = (y/96)*32 + x/96 ... |
| `1000:8996` | Track_ScrollBackdrop | themes 1, 3 and 5, each presented frame: rewrites tile 0 of the tile graphics (segment 1b78, 16 x 16) from the 16 x 16 pattern at 193c:3ee3 ... |
| `1000:89e0` | Track_AnimateWater2 | theme 2, each presented frame: counter [26d1]; every four frames writes the next of four tile sets (00h, 10h, 20h ... |
| `1000:8a2b` | Track_AnimateTiles8 | theme 8 unless track [28c0] = 1: counter [26d3] cycles three frames (tile bases 0, 9, 12h, four steps each) ... |

### car

31 routines, 9794 bytes, 3024 instructions. Ranges: 4611-4758, 4aee-5429, 5532-585b, 5be7-5e4e, 6ae5-7759, 7ce0-849b, 8712-8929. Uses: video (16), coll (4), track (4), race (4), terrain (2), unpack (1), hud (1), sys (1).

| Address | Name | What |
|---|---|---|
| `1000:4611` | Car_LoadGfx | loads ROUNDnBR.VH0.. (193c:11f3) through 1000:3547 into 1b78, then splits it: theme 9 ([28bf] = 9): 3840h bytes to 4d78:0000 and 1f40h to 5d78:0000 ... |
| `1000:4696` | Car_MirrorFrames24 | in 4d78 (DS = ES): from the eight 24 x 24 vehicle frames at 4d78:0000 builds at 1440h eight frames flipped top to bottom ... |
| `1000:46f6` | Car_MirrorFrames40 | the same as 1000:4696 for theme 9's 40 x 40 frames: flipped copies from 31d8h written at 3840h in 4d78 |
| `1000:4aee` | Car_StepAll | the vehicles' control step, once per game step from 1000:3039: for each record (bx = 0..42ch, stride 164h): theme 9's special case ... |
| `1000:4edc` | Car_TurnRate | CX = the turn step of vehicle bx: [bx+129a] (2 for themes 6 and 7, else 3, set in 1000:3c09) ... |
| `1000:4f03` | Car_StepAll_Fire | an inner label of 1000:4aee (reached by its jumps; not called): fire pressed ... |
| `1000:4f17` | Car_StepAll_Hop | an inner label of 1000:4aee: start a hop if allowed ([2919] = 1, or theme 7) ... |
| `1000:51b2` | Car_HopStep | vehicle bx's hop: decrements the timer 13a4; while it is 28h or more, moves the hop's x (13a0 fraction, 1398) and y (13a2 ... |
| `1000:525e` | Car_Velocity | vehicle bx (active, state 0, 12d6 <= 0, 1380 = 0): the wanted velocity 1270 (x) and 1274 (y) = speed 127a times the sine and cosine of the heading ... |
| `1000:5532` | Car_MoveProbe | one vehicle (bx = record offset from 193c:124c, stride 164h), state 0 only: integrates velocity [1272]/[1276] plus fractions [1258]/[1264] into the ... |
| `1000:5be7` | Car_Physics | one vehicle's move for the frame (bx): a pending impulse [12ac] recomputes the candidate position; Car_MoveProbe, Terrain_Dispatch ... |
| `1000:6ae5` | Car_Warp | vehicle state 0eh and theme 3 class 4: the warp hole, sub-state [1382]: 0 finds the entry for the vehicle's tile in the list 193c:22e1 (tile x ... |
| `1000:6feb` | Car_Respawn | vehicle state 7, theme 9 class 15: back on the track: themes 2 and 8 count [1382] down first ... |
| `1000:73e7` | Car_SinkStep | per vehicle (bx) from the race step 1000:3039 (theme 9: only bx = 0): counts the animation frame [12b0] ... |
| `1000:7429` | Car_StepAirAndPoints | once per vehicle per game step (bx = vehicle record offset 0..42c ... |
| `1000:7ce0` | Car_DrawAndRunStates | per presented frame, inside the race frame between the map and the foreground tiles: shadows (1000:7e5c) of active airborne vehicles ... |
| `1000:7d73` | Car_Draw | draws vehicle bx (pusha): skipped (and [1250] visible = 0) for the hidden vehicle [2621] ... |
| `1000:7e5c` | Car_DrawShadow | vehicle bx's shadow: the same frame as 1000:7d73 offset down-right by the height [12d6], drawn in colour 0 (1000:8c3c) from segment 4d78 ... |
| `1000:7efa` | Car_StateAnim5 | vehicle state 5: if [12c2] just draws the vehicle; else plays the frame sequence of 193c:2855 (step durations against the counter [12b0] ... |
| `1000:7f62` | Car_StateAnim4 | vehicle state 4 (knocked out): notes the first such vehicle in [2682] for the two-player point; if [12c2] draws the vehicle ... |
| `1000:7fe8` | Car_DrawEffect24 | SI = frame: draws a 24 x 24 frame (frame * 240h) of the effect sprites in segment 5d78 at vehicle bx's position, recoloured by [1252]; clipped |
| `1000:8034` | Car_DrawEffect40 | SI = frame (5 subtracted above 4): draws a 40 x 40 frame (frame * 640h) from segment 5d78 at vehicle bx's position, plain ... |
| `1000:8083` | Car_DrawTrail | vehicle bx's skid marks or wake: a ring of eight 12-byte entries at [bx+12fd] (two points, a frame, a sprite) with the cursor [bx+1296] ... |
| `1000:82be` | Car_StateSplash | vehicle states 2 and 0Dh: plays the sequence 193c:289d (frames at +0Eh) at the saved position [12ba]/[12bc] (1000:8339) ... |
| `1000:8339` | Car_DrawSplashFrame | SI = frame: a 24 x 24 frame from 193c:45e3 + frame * 240h at the saved position [bx+12ba]/[bx+12bc] lifted by the height, plain |
| `1000:8386` | Car_DrawRipples | vehicle bx's ripples: a ring of five 6-byte entries at [bx+135d] (x, y, frame) with cursor [bx+1298] ... |
| `1000:843d` | Car_DrawOverlay8 | theme 8: a 32 x 32 frame (four frames cycling with the counter [bx+1392] / 2) over vehicle bx's position (a rotor, inferred from the theme) ... |
| `1000:847e` | Car_Draw32 | SI = frame, DI/AX = screen x/y of the centre: a 32 x 32 frame from 193c:5ee3 + frame * 400h, clipped, plain |
| `1000:8712` | Car_DrawShell | vehicle bx's shell ([1394] set; [13a4] the timer, [1398]/[139c] position ... |
| `1000:87f3` | Car_ShellStepFrac | one sub-pixel step: adds the signed byte AL to the fraction in AH's high nibble, carrying (or borrowing) into CX ... |
| `1000:880a` | Car_StateFall | vehicle state 1 (fell off, sank: set by the terrain handlers): speed [127a] = 0 ... |

### coll

3 routines, 830 bytes, 261 instructions. Ranges: 5921-5be7, 79fd-7a75. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:5921` | Coll_CarsAll | the six pairs of the four racers (record offsets in 193c:2660/2662/2664/2666) through Coll_CarPair, the pair in 193c:27b1 and 27b3; bx kept |
| `1000:5960` | Coll_CarPair | vehicle bx's candidate position is recomputed (as in Car_MoveProbe), then cars 193c:27b1 and 27b3 ... |
| `1000:79fd` | Coll_ShellHitsCar | for vehicle bx (if it can be hit, [bx+124e]): every other vehicle's shell in flight ([si+1394] set ... |

### terrain

25 routines, 3223 bytes, 1051 instructions. Range: 5e4e-6ae5. Uses: car (7), track (2), sys (1).

| Address | Name | What |
|---|---|---|
| `1000:5e4e` | Terrain_Dispatch | in two-player (193c:2656 = 2) both cars sinking (state 1) or in a hole (state 5) ends the race (193c:2913 = 1) ... |
| `1000:60cb` | Terrain_Floor | class 0 (all themes, also 2.7 3.0 4.15 5.13 6.5 8.3): clears [1390] ... |
| `1000:6169` | Terrain_FallOff | theme 8 class 1: off the edge: position saved in [12ba]/[12bc], state 0dh, timer [1382] = 46h |
| `1000:618c` | Terrain_Boost | theme 8 class 2: two-stage ([1390] 1 then 2); on the second, a vehicle moving right or still ([1272] >= 0) gains 0c8h of x speed |
| `1000:61b0` | Terrain_Rough | theme 5 class 3, theme 6 class 4: a jump when leaving a raised level (/15 + 4) ... |
| `1000:6231` | Terrain_Current | themes 8 (classes 6-8) and 9 (3-5), and theme 2 when the surface has flags 18h: a flow pushes the vehicle: direction from 193c:18fb by the surface's ... |
| `1000:62e3` | Terrain_Plughole | theme 2 only, state 0: inside the box around world (650h, 0b70h) the vehicle is pulled toward that point, harder near it ... |
| `1000:63d6` | Terrain_Splash | theme 2 class 1: requests the splash/puff effect ([128a] = 1; drawn by the effect code at 1000:8139) |
| `1000:63dd` | Terrain_Ramp | theme 2 class 6: on the ground ([12d6] = 0) a jump with [12d4] = max(speed^2, 1ch)/8 + 5, [12d8] = 0 |
| `1000:641a` | Terrain_Bump | theme 2 class 3: unless coming from class 6, a speed^2 of at least 0eh makes a jump ([12d4] = max(speed^2, 1ch)/10 + 5) |
| `1000:6456` | Terrain_Hole | theme 1 class 1, theme 3 class 2, theme 5 class 4: if the vehicle can fall ([12f9]): state 1, [12b0] = 0 ... |
| `1000:64f5` | Terrain_HoleKeepSpeed | theme 4 class 3, theme 8 class 10, theme 9 class 7: as Terrain_Hole but the velocities are left alone |
| `1000:657e` | Terrain_FloorSkid | theme 1 class 2, 3.1, 4.1, 5.14, 6.1: as Terrain_Floor (jump off a raised level, theme 1 ramps) and, when not jumping ... |
| `1000:6622` | Terrain_Splashy | theme 4 class 2, 5.2, 8.9, 9.6: jump off a raised level; when moving ([127a] != 0) requests the splash/puff effect ([128a] = 1) and plays AH=5 AL=12h ... |
| `1000:6674` | Terrain_Wall | theme 1 classes 3 and 4: entered from class 0 or 2 it counts as class 0 and the vehicle bounces back on both axes ([12a8], [12c4], [12c8] = 1) |
| `1000:669b` | Terrain_Slick | theme 1 class 5, theme 6 class 3: grip lost for 20 frames ([1284] = 14h ... |
| `1000:66b2` | Terrain_SlickB | theme 7 class 15: jump off a raised level, then the second low-grip timer [1286] = 14h |
| `1000:66ed` | Terrain_RampBig | theme 5 class 1, 8.4, 9.1: jump off a raised level (/15 + 6); unless coming from class 6 a jump of speed^2/9 + 0bh (theme 2) or + 2 |
| `1000:6768` | Terrain_RampSmall | theme 1 class 6, 2.2, 4.4, 5.15, 6.2, 8.5, 9.2: jump off a raised level; a speed^2 of at least 0eh jumps max(speed^2, 1ch)/7 (+ 7 on theme 2) |
| `1000:67d8` | Terrain_RampSteep | theme 7 class 14: jump off a raised level; unless coming from class 6 a jump of speed^2/5 + 4 |
| `1000:683b` | Terrain_None | theme 1 class 7: a lone ret (nothing happens) |
| `1000:683c` | Terrain_Edge | theme 3 class 7, and Car_MoveProbe when the previous surface had flag 10h: leaving it jumps (speed^2/12 + 4), [1384] = 1 ... |
| `1000:6882` | Terrain_Levels | themes 4 and 5 classes 5-12 (and 13, 14 on theme 4): stepped heights: level = class - 4 (13 -> -1, 14 -> 4 with a jump) ... |
| `1000:69c5` | Terrain_LevelsB | theme 6 classes 6-7, theme 7 classes 1-8: as Terrain_Levels with level = class (class 8 counts as 2) and jumps of speed^2/15 + 4 |
| `1000:6ac2` | Terrain_Layer | theme 3 class 5: surface flag 10h puts the vehicle on the upper layer ([1388] = 1, [1386] = 0), else the lower ([1388] = 0, [1386] = 1) ... |

### ai

1 routines, 265 bytes, 87 instructions. Range: 5429-5532. Uses: nothing (a leaf).

| Address | Name | What |
|---|---|---|
| `1000:5429` | Ai_Drive | a computer driver's input for vehicle bx, returned in AL and in 137b: the wanted heading from the tile's direction (12da low nibble, & 7 on theme 2 ... |

### dead

15 routines, 852 bytes, 334 instructions. Ranges: 0f70-0f99, 2099-216c, 22b6-2329, 2706-2768, 3171-319f, 3a44-3ad0, 864f-8683, 8929-8992, 8b8e-8bab, 8d9e-8dfc. Uses: menu (4), text (3), video (3), pal (1), hud (1).

| Address | Name | What |
|---|---|---|
| `1000:0f70` | Dead_SaveLevelFile | unreferenced: creates GAME1.LVL (name at 0889) and writes DS 040a-081d and 1feb-23b6 into it ... |
| `1000:2099` | Dead_TwoPlayerResult | not reached (no reference): an older two-player result screen: music 8 ... |
| `1000:22b6` | Dead_TwoPlayerCounts | reached only from the dead 2099: for both players ([0c16], [0c31] and 0fh) the character's win and loss counts [09a4 + n] / [09af + n] as WON nn and ... |
| `1000:2706` | Dead_LoadExtraChr | not reached: reads 1plr_xtr.chr ([09fd]) into the picture segment of object 0c8a ... |
| `1000:2736` | Dead_DumpMemory | not reached: creates Paul.dat ([0a0a]) and writes memory from segment 1b78 up to 6d78 into it in 4 KB blocks: a developer's memory dump |
| `1000:3171` | Dead_WaitRetrace | unreferenced: waits for the end and then the start of a vertical retrace on 3DAh |
| `1000:317f` | Dead_SetTextMode | unreferenced: saves the video mode in [2645] and sets mode 3 |
| `1000:3192` | Dead_RestoreMode | unreferenced: sets the video mode saved in [2645] |
| `1000:3a44` | Dead_MouseInit | unreferenced: resets the mouse driver (int 33h 0), hides the pointer, limits it to 0..319 x 0..199 ... |
| `1000:3a7b` | Dead_SbDspReset | only called by the dead 1000:3a8d: pulses the Sound Blaster DSP reset at [2641] + 6 |
| `1000:3a8d` | Dead_SbDetect | unreferenced: Sound Blaster detection: from the base [2641] in steps of 10h up to 270h resets the DSP and waits for AAh on base + 0Ah ... |
| `1000:864f` | Dead_BannerReset | unreachable tail inside 1000:8634 (after its unconditional jump): would reset the banner position and [26c2] when it left the screen at x = -58h ... |
| `1000:8929` | Dead_HudDrawNumber | unreachable: draws AX as four digits right to left (DI moving 8 left per digit, 1000:905f), a minus glyph (digit 0Ah) first if negative |
| `1000:8b8e` | Dead_SetOverscan | unreachable: sets the VGA overscan (border) colour to AH through attribute register 11h (resets the flip-flop at 3dah ... |
| `1000:8d9e` | Dead_DrawUnclippedRecolour | unreachable first entry of 1000:8dc0: the bounds test and offset of 1000:8ce4, then falls into 1000:8dc0 |

## Call tree from Start

Down to module level; the routine named first in each line is the one that leads into the module.
Confirmed from the calls; the meaning of each screen from the strings and pictures it draws.

```
Start (main)
  Sys_CodeCardCheck 31f0 ........ sys: FONT.BIN into 2424:0000, far call with AX = 0 (AX = 0: passed)
  Copy_Patch32 7a75 ............. video: patches the two big copy loops to movsd (self-modifying)
  Menu_LoadFrontEnd 26c0 ........ menu -> unpack (COMPRESS.PI* into the picture segments), snd (driver), pal
  Irq_Install 498a .............. timer (IRQ 0, IRQ 2), keyboard (IRQ 1)
  driver AH = 7; Menu_GameOptions 2770 (carry = Esc: exit)
       -> settings (SETTINGS.DAT), setup (joystick calibration), input (slots), joy (detect),
          sys (speed test), snd (driver reload), menu (credits, redefine keys), text, video
  Setup_ChooseGame 2be8 ......... setup -> file (GAMEn.LVL)
  Snd_LoadDriver 321c; SS:SP saved in 193c:0004/0006
  loop:
    Menu_Attract 0100 ........... menu -> video, text, input, pal, snd        (carry: back to the options)
    Menu_SelectPlayers 0220 ..... menu                                         (carry: back to the attract)
      one player: Menu_OnePlayerGame 02e0 -> Menu_HeadToHeadGame 0fbf / Menu_ChampionshipGame 102b
        -> Race_Series 10a0 -> Race_RunOne 115c -> Race_Main 3039
      two players: Menu_TwoPlayerGame 1e20 -> Menu_TwoPlayerChoose 1ef1
        -> Menu_TwoPlayerTournament 1faf / Menu_TwoPlayerSingleRace 2329 -> Race_RunTwoPlayer 216c -> Race_Main
    Race_Main 3039 (race)
      Race_Setup 37fc -> track (names, maps, tiles, COL/DIR), car (vehicle pictures, mirrored), unpack, pal
      per game step: Input_Read 2d5b (input -> joy, mouse, keys; ai through the empty slot)
                     Race_Pause 35f0 (Space; the pause cheats)
                     Car_StepAll 4aee (car -> Car_Physics -> Terrain_Dispatch (terrain), coll, track)
                     Race_DrawFrame 90c5, last step only (track animation, video blits, hud,
                       Car_DrawAndRunStates 7ce0: car drawing and the vehicle state handlers)
                     per vehicle: Car_StepAirAndPoints 7429, Car_SinkStep 73e7, Car_HopStep 51b2
      per presented frame: wait on cs:4ade, Gfx_PresentScrolled 92bc
      the end: 100 frames of banners (Race_StateLostPoint 855a), driver AH = 8, 6; pal fade
    a quit race: Race_RunOne / Race_RunTwoPlayer tail-jump to Main_Restart 00cc -> back into Start's loop
  exit: video mode back, driver AH = 7, 8, 6, Snd_StopAll 00e2, Sys_RemoveIrqs 49af (vectors, DOS clock
        reset from the RTC), typematic rate (int 16h 0305h), int 21h 4C00h
```

Module dependencies (who calls whom), leaves first:

- Leaves: timer, keyboard, mouse, snd, file, pal, unpack, text, coll, ai.
- video uses unpack (`Gfx_LoadBits` through the packed-file loader). hud uses video. sys uses timer.
- input uses joy, mouse, ai (the computer driver sits in the slot of an empty control); joy uses video and
  menu once (the calibration prompt).
- track uses unpack. terrain uses car, track, sys. car uses coll, terrain, track, video, hud, unpack, race
  (the state handlers that end a race) and sys.
- race uses car, track, hud, input, snd, pal, video, file, sys, menu (the pre-race screens) and main
  (`Main_Restart`).
- menu and setup use everything of the front end: video, text, input, joy, pal, snd, sys, unpack, setup,
  race.

## Indirect calls and their tables

All six indirect sites are near calls through memory; no indirect jumps exist.

| Site | Through | Entries | Targets |
|---|---|---|---|
| `1000:2dad`, `2db8`, `2dc3`, `2dce` (`Input_Read`) | `193c:1083`, `1085`, `1087`, `1089`: one slot per player | filled by `Input_SetSlots` (`1000:2d00`) from the control type at `193c:2658`.. | `2ded` none / computer (type 6), `2dfa` keys 1 (4), `2dfe` keys 2 (5), `2e02` mouse (3), `2e6c` joystick A (1), `2eb3` joystick B (2) |
| `1000:5eea` (`Terrain_Dispatch`) | the word at `193c:28bd` + 2 x class | 9 tables, one per theme, chosen at `1000:3be6` from `193c:26d7`: 26e9, 26f9, 2707, 2717, 2735, 274f, 2759, 2779, 277f; 8 classes (tile byte >> 5) for themes 1, 2, 3, 6, 16 (>> 4) for the others | the 25 `Terrain_*` handlers, `Car_Warp` 6ae5, `Car_Respawn` 6feb, `Sys_Ret` 35be (nothing) |
| `1000:7d45` (`Car_DrawAndRunStates`) | `193c:278f` + 2 x state | 17 (vehicle state `[bx+12ae]` 0..10h) | 7d73 (0, 0Bh, 0Ch), 880a (1), 82be (2, 0Dh), 35be (3, 6, 8, 9), 7f62 (4), 7efa (5), 6feb (7), 849b (0Ah), 6ae5 (0Eh), 8683 (0Fh), 86a8 (10h) |

The nine terrain tables overlap one another in the data segment (each starts inside the one before), and
the tables of themes 8 and 9 run into the state table at `193c:278f` from class 11 (theme 8) and class 8
(theme 9) on. Whether those classes occur on the tracks of those themes is not yet checked (the maps'
tile classes, step 3); if they do, a state handler runs as a terrain handler and the port must do the same.

Observed in the harness (cover.py `indirect.txt`): the input slots 2ded, 2dfa, 2dfe; the terrain handlers
60cb, 63d6, 35be (theme 2); the states 7d73 and 849b. A handful of other "targets" of 7d45 one to four
bytes after the site are interrupts delivered between the call and its first block, not targets.

## Multiple entries, fall-throughs, shared code

| Where | What |
|---|---|
| `0006` / `00cc` | `Main_Restart` reloads SS:SP and jumps into the middle of Start (0054): the two share Start's loop |
| `04b8` -> `04bd` | `Gfx_DrawObjectSaved` saves under the object, then runs into `Gfx_DrawObjectMasked` (also called directly) |
| `0862` -> `0876` | `Gfx_FillRows` jumps into `Gfx_ClearBuffer`'s fill loop |
| `089c` -> `08bc` | `Gfx_PresentRows` jumps into `Gfx_Present`'s copy loop |
| `2b8b`, `2b8f` -> `2b93` | two four-byte entries load BL (joystick A or B buttons) and run into `Joy_WaitButton` |
| `32ae` -> `3303` | `Pal_FadeOutStep` (called by `Pal_FadeOut`) jumps into the tail of `Pal_FadeInStep`: the DAC write both share |
| `4aee`, `4f03`, `4f17` | labels inside `Car_StepAll` (fire, start of a hop), not called; `Car_TurnRate` 4edc is a real subroutine placed in the middle of the body |
| `7b46` -> `7c5b` | `Snd_UpdateEngines` runs into `Snd_SetEngineTone` |
| `8634` / `864f` | an unreachable tail after an unconditional jump, named `Dead_BannerReset` |
| `8ce4` -> `8d09` | `Gfx_DrawUnclipped` jumps into `Gfx_BlitAt` (the blit shared by both) |
| `8d9e` -> `8dc0` | the dead first entry of `Gfx_BlitRecolourAt` |

Hand-written conventions seen throughout: arguments in registers (BX = the vehicle record offset in the
race, SI = an object or a string, DX = a font), results in registers and in the carry flag (the menus and
key waits: carry = Esc or a timeout; the loaders: carry = failure), `pusha`/`popa` around most routines.

## Self-modifying code, cs: variables, the stack

**Self-modifying code** (every write into the code segment that lands on an instruction; all from
`Copy_Patch32`, `1000:7a75`, called once and unconditionally from Start):

| Writer | Patched | What |
|---|---|---|
| `1000:7a8d`..`7a90` (four `stosw`, ES = CS, DI = 91a4) | `1000:91a4`..`91ab` | eight `movsw` of the tile-row copy in `Race_DrawFrame` become four `movsd` (`66 a5`) |
| `1000:7a7c` | `1000:92d9` | the count of `mov cx` in `Gfx_PresentScrolled`: 80h words become 40h dwords |
| `1000:7a83` | `1000:92dc` | `movsw, nop` after the `rep` at 92db become `66 a5`: `rep movsd` |

The unpatched code is what a 286 would run (the `xchg bx,bx` / `nop` fillers before both loops pad for
the patch), but the patch is unconditional and other routines use 386 instructions natively
(`Gfx_ClearBuffer`, `Gfx_FillRows`, `Gfx_Present`, `Gfx_PresentRows`: `rep stosd` / `rep movsd`;
`Sys_ClearRegs`, `Race_ClearRegs`: 32-bit registers): **the game needs a 386**. The port runs the patched
code.

**cs: variables** (data kept in the code segment, mostly where an interrupt handler or a routine with DS
pointing elsewhere needs them):

| Address | What |
|---|---|
| `cs:2f99` | the keyboard handler's E0-prefix flag |
| `cs:3279` | the sound driver is loaded (set by `Snd_LoadDriver`, tested by the timer handler before calling AH = 3) |
| `cs:3544` (byte), `cs:3545` (word) | the decoder's control byte and flags, swapped in and out around each code by `Unpack_SwapState` |
| `cs:4a8d`, `cs:4a8e` (far), `cs:4a92` (word) | CRTC register 11h as found, the old int 0Ah vector, "IRQ 2 works" (set by the int 0Ah handler, tested by the timer handler) |
| `cs:4ade` | the frame tick counter: incremented by the timer handler (or by the int 0Ah handler when IRQ 2 works), waited on by `Sys_WaitTick` and by `Race_Main`'s pacing, cleared after each present |
| `cs:7af5`, `cs:7af6` | the engine-sound routines' channel counter (40h..43h) and a word written but never read |
| `cs:7cdb`..`7cde` | `Snd_Random`'s state: three bytes read and written as the overlapping words 7cdb, 7cdc, 7cdd |
| `cs:8780` | an 11-word table (0 1 2 3 4 5 4 3 2 1 0): the shell burst's frame sequence |
| `cs:8994` | the low two bits of the scroll offset, set by `Race_DrawFrame`, subtracted by the blits and the present |
| `cs:90c3` | SP saved by `Race_DrawFrame` (below) |

Also read through CS while DS points elsewhere: `cs:93c2` (= `193c:0002`, the tick count, in
`Menu_WaitKey`), `cs:93cb`/`93cd` (= `193c:000b`/`000d`, the driver's parameters, in `Snd_LoadDriver`),
`cs:9c62` (= `193c:08a2`, the handicap flag), `cs:a329` (= `193c:0f69`, the cheat flag in `Race_Pause`):
the data segment begins at CS + 93C0h.

**The stack.** Start saves SS:SP after the set-up in `193c:0004`/`0006`; `Main_Restart` (`1000:00cc`)
reloads them and jumps back into Start's loop: a longjmp, taken by `Race_RunOne` and `Race_RunTwoPlayer`
when a race was quit with Esc, from any depth of the front end. `Race_DrawFrame` (`1000:90c5`) saves SP in
`cs:90c3`, pushes (buffer offset, tile segment) for every foreground tile while it draws the map, draws the
vehicles, then pops and draws the foreground tiles until SP is back at the saved value: the stack as a
list. A screen full of foreground tiles would push up to 952 bytes on the 1,000-byte stack (inferred: not
seen). The port needs a real list there, with the same order (last pushed, first drawn).

## Unreached: what the 208 bytes are

| Region | Bytes | What |
|---|---|---|
| `0000`-`0006` | 6 | zeros before Start (nothing reads them) |
| `00f3`-`0100`, `0213`-`0220`, `045b`-`0460`, `0461`-`0470`, `070e`-`0710`, `0793`-`07a0`, `08e5`-`08f0`, `0f6d`-`0f70`, `1e18`-`1e20`, `2768`-`2770`, `7a94`-`7a97`, `92eb`-`92f0`, `93bf`-`93c0` | 117 | zero padding (several routines start on 16-byte boundaries) |
| `296d` | 1 | `nop` (alignment) |
| `0460`, `2408`, `2f9d`, `7cdf`, `8711` | 5 | stray `ret`s after a routine's own `ret` (dead) |
| `8992`-`8994` | 2 | `popa; ret`: a second tail after `Dead_HudDrawNumber`'s |
| `8d6f` | 1 | a `pusha` jumped over by the `jmp` at 8d6d |
| `2f99`-`2f9d`, `3279`, `3544`-`3547`, `4a8d`-`4a94`, `4ade`, `7af5`-`7af8`, `7cdb`-`7cdf`, `8994`-`8996`, `90c3`-`90c5` | 54 | cs: variables (above; some with unused bytes) |
| `8780`-`8796` | 22 | the cs: table of the shell burst |

**Dead code** (named, in the dead module, 852 bytes): `Dead_SaveLevelFile` 0f70 (writes GAME1.LVL: the
writer of the live `File_LoadGameSet`), `Dead_TwoPlayerResult` 2099 and `Dead_TwoPlayerCounts` 22b6 (an
older two-player result screen), `Dead_LoadExtraChr` 2706, `Dead_DumpMemory` 2736 (a developer's dump of
segments 1b78..6d78 to a file), `Dead_WaitRetrace` 3171, `Dead_SetTextMode` 317f and `Dead_RestoreMode`
3192, `Dead_MouseInit` 3a44 (so the mouse control is never set up), `Dead_SbDetect` 3a8d and
`Dead_SbDspReset` 3a7b (Sound Blaster detection), `Dead_BannerReset` 864f, `Dead_HudDrawNumber` 8929,
`Dead_SetOverscan` 8b8e, `Dead_DrawUnclippedRecolour` 8d9e. Also live code blanked in place: `Race_Setup`
begins with 255 `nop`s (`1000:37fc`-`38fa`) and `Car_MoveProbe` has runs of three `nop`s (5786, 57cc),
which the port need not reproduce beyond their (nil) effect.

## The frame shape (evidence for step 1's choice)

The original waits in many places, at many depths:

- the race: `Race_Main` runs [263a] game steps (the SMOOTHNESS setting), draws on the last one only,
  then busy-waits until `cs:4ade` reaches the byte at `193c:263c + [263a]`, presents (`Gfx_PresentScrolled`)
  and clears `cs:4ade`: one wait and one present per presented frame, all inside one loop;
- the front end: 15 inline waits for the next tick (`[0002]` read, compared until it changes) in ten menu
  routines (`Menu_Attract`, `Menu_ChooseOfTwo`, `Menu_SelectCharacter`, `Menu_AskHandicap`,
  `Menu_PressAnyKey`, `Menu_WaitKey`, `Menu_WaitKeyBlink`, `Menu_ChampionScreen`, `Menu_LivesMessage`,
  `Menu_TwoPlayerSingleRace`), `Sys_WaitTick` (6 callers), and timeouts on `[261f]`;
- the fades write the DAC with no wait at all (their speed is the CPU's);
- `Main_Restart` unwinds from any depth.

The timer gives one tick per frame, locked to the retrace (the handler busy-waits for the retrace inside
the interrupt, then reloads the PIT with the measured frame less 150 counts), and chains to DOS's int 08h
every tick. With waits nested deep in the menus and a longjmp out of them, the LOOP build
(`gasm_main` with `gasm_wait_frame` at the original's wait points) fits the program as written; the
alternative, state machines per screen, would have to rebuild every menu's control flow. (Recommendation,
for the decision in docs/re/architecture.md.)

## Work packages

Nine packages, leaves first, balanced by instruction count (each instruction counted once; the dead code is
its own optional package: port it only to keep the record complete). "Needs" are the packages whose
routines a package calls; the leaves need only the machine (step 2: memory, VGA, PIT, keyboard, DOS).

| # | Package | Routines | Instructions | Bytes | Needs |
|---|---|---|---|---|---|
| 1 | **Machine-facing**: timer and IRQ handlers, keyboard, joysticks and their timing, input slots and `Input_Read`, mouse slot, sound driver interface (`Snd_StopAll`, `Snd_LoadDriver`), code-card check, speed test, CRTC base, `Sys_WaitTick`, fades and palette loads (`Pal_*`), `File_LoadGameSet`, `File_SaveScreen` | 42: 00e2 07a0 0f99 26ec 2b8b 2b8f 2b93 2d00 2d5b 2dfa 2dfe 2e02 2e6c 2eb3 2efd 2f9e 2fcc 2ffe 3165 319f 31b6 31f0 321c 327a 32ae 32ce 3303 35be 35bf 3a12 3ad0 4758 485b 489c 490d 4966 498a 49af 4a21 4a6c 4a94 4adf | 1,058 | 2,511 | (machine); 6 (`Input_None` through a slot); 2 and 3 (`Joy_WaitButton`'s prompt) |
| 2 | **Drawing**: sprite objects, clip, fill, tile map, presents, text and fonts, the decoder of the packed files (`Unpack_*`), the race blits, banners, HUD, `Copy_Patch32` | 42: 049c 04b8 04bd 053a 05b4 05f3 0630 06cc 0710 07e5 0823 0862 0876 089c 08bc 08f0 0910 0929 0999 1a34 3333 3533 3547 482f 7a75 8bab 8c3c 8c6a 8ca4 8cd0 8ce4 8d09 8d36 8d4a 8d9e 8dc0 8dfc 903f 905f 9076 9289 92bc | 1,625 | 3,399 | (machine) |
| 3 | **Front end A**: attract, select players, one-player menu, chooser, backdrop, the drivers' carousel and faces, game options, credits, joystick calibration, data-set chooser, redefine keys | 23: 0100 01de 0220 02e0 0382 0400 09e0 0b51 0c15 0c5d 0c96 0cd3 0d1f 0db0 0e02 0eba 0f17 0f3c 2770 2a82 2ab5 2be8 92f0 | 1,399 | 3,804 | 1, 2; 4 (02e0's games) |
| 4 | **Front end B and Start**: Start, `Main_Restart`, the one-player series (head to head, championship, pre-race, results, knock-out, lives, bonus race, champion), the two-player games, the key waits, the progress map, `Menu_LoadFrontEnd` | 32: 0006 00cc 0fbf 102b 10a0 115c 11d5 11f8 13e4 16de 179b 17ff 1867 18d8 198e 19f2 1a4a 1a82 1aad 1c1b 1e20 1ef1 1faf 216c 2193 2216 2329 240a 2481 256e 26c0 | 1,941 | 5,796 | 1, 2, 3; 8 (`Race_Main`) |
| 5 | **Race set-up and track**: `Race_Setup`, `Race_LoadTrack` (splits at 3d53, 3f19, 4134, 446a if needed), the file names, tiles, COL/DIR, vehicle pictures and their mirroring, the tile animations | 13: 37fc 3b50 3c09 458b 45e5 45ef 4611 4696 46f6 4808 8996 89e0 8a2b | 1,615 | 4,157 | 1, 2 |
| 6 | **Vehicle control and physics**: `Car_StepAll` (with its labels and `Car_TurnRate`), hop, velocity and grip, the move probe, track lookups, car-to-car collisions, shell hits, the computer driver and its slot | 15: 2ded 4aee 4edc 4f03 4f17 51b2 525e 5429 5532 585b 589c 5921 5960 5be7 79fd | 1,582 | 5,093 | 7 (`Terrain_Dispatch`); 8 (one call) |
| 7 | **Terrain, warp, respawn**: `Terrain_Dispatch` (checkpoints, laps, short cuts too) and the 25 handlers, `Car_Warp`, `Car_Respawn`, `Car_SinkStep` | 28: 5e4e 60cb 6169 618c 61b0 6231 62e3 63d6 63dd 641a 6456 64f5 657e 6622 6674 669b 66b2 66ed 6768 67d8 683b 683c 6882 69c5 6ac2 6ae5 6feb 73e7 | 1,754 | 5,595 | 6 (track lookups); 8 (`Car_Draw` from the warp) |
| 8 | **Race loop and the vehicles on screen**: `Race_Main`, pause and cheats, `Race_DrawFrame`, the rules and banners of the race end and of head to head (`Race_State*`), vehicle drawing, shadows, trails, ripples, shells, the state handlers, engine sounds | 37: 3030 3039 35f0 7429 7759 78f8 7a97 7af8 7b46 7c5b 7cae 7ce0 7d73 7e5c 7efa 7f62 7fe8 8034 8083 82be 8339 8386 843d 847e 849b 851f 855a 8634 864f 8683 86a8 86d2 8712 87f3 880a 8929 90c5 | 2,127 | 6,581 | 1, 2, 5, 6, 7 |
| 9 | (optional) **Dead code** | 12: 0f70 2099 22b6 2706 2736 3171 317f 3192 3a44 3a7b 3a8d 8b8e | 261 | 680 | 1, 2, 3, 4 |

Total 13,362 instructions. 1 and 2 can start at once and in parallel; 3, 5 and 6+7 next (6 and 7 call each
other: port them together or agree the interface `Terrain_Dispatch` / `Track_TileAt` first); 4 and 8 last,
4 needing 8 only for the call into the race. The dead routines inside other packages (8d9e in 2, 864f and
8929 in 8) are listed there because they share code with live ones.

## Open points (inferred, to be checked)

- The driver function numbers' meanings: AH = 0 initialise (DX = 388h), 2 (repeated until AL = 0), 3 (the
  timer's tick), 4 play a tune (after 9 asks), 5 a sound effect, 6, 7, 8 stop, 0Ah a query, 0Eh a tone,
  10h off. To be confirmed when the driver is read (step 7).
- `193c:0f64`'s value 1: which driver pokes its engine channels directly (`Snd_*` writes into
  `2424:0008 + 10h n`).
- Whether the classes of themes 8 and 9 that would index the state table occur on their tracks.
- The terrain handlers' exact physics, the AI's line, the hop: named from their effects, to be checked
  differentially in step 5.
