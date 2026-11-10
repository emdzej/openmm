# The front end (part A)

The screens before a race: what each shows, how it reads its input, the menu state it keeps in the data
segment and the settings. Ported in `src/game/menu.c` (the menus, the drivers' carousel) and
`src/game/frontend.c` (the set-up screens). Addresses seg:off with the image at 1000h; data in DS = 193c.
"Confirmed" = read in the disassembly; "inferred" = from use. Package 4 (Start, the one- and two-player
series) is described with its port.

## The order of the screens (confirmed)

Start runs GAME OPTIONS (`Menu_GameOptions` 2770; Esc there quits the game), then the data-set chooser
(`Setup_ChooseGame` 2be8) and the driver's load, then its loop: the attract screens (`Menu_Attract`
0100; Esc goes back to GAME OPTIONS and the chooser) and SELECT GAME (`Menu_SelectPlayers` 0220; Esc or
a 7d0h-tick timeout goes back to the attract screens, a finished game to SELECT GAME again). SELECT GAME leads to the one-player game screen (`Menu_OnePlayerGame` 02e0:
head to head or the challenge) or to the two-player game (1e20); the drivers' carousel
(`Menu_SelectCharacter` 09e0) is called from those games (package 4). A race left with Esc ends in
`Main_Restart`, a jump back into Start's loop from any depth: the port's 0220 and 02e0 return at once
after a game when `g_restart` is set (`CALL_R`).

## Drawing and waiting (confirmed)

Every menu screen is drawn into the back buffer (segment 6d78) and presented whole (`Gfx_Present`) or
in bands of rows (`Gfx_PresentRows`). Pictures are sprite objects (27-byte records in DS: image record,
x, y, save-under segment, ..., frame at +13h, back-buffer offset at +15h, -1 when not drawn); text is
drawn from zero-terminated lists in DS with the 8-pixel fonts (records 0b40, small; 0b54, large).

The menus wait for the timer's counters, advanced once a frame by the timer handler: `[0002]` (cleared
by the menus themselves before a timed wait; a loop "read it, spin until it differs" is one tick),
`[261f]` (the carousel's steps, the press-a-key timeout). The set-up screens instead spin on the
keyboard handler's last released key `[107e]` with no timer wait at all; REDEFINE KEYS spins on a
refused key's code until another key is released. The port keeps each loop with `mach_frame()` as its
body. Such routines cannot be recorded alone (the recorder holds interrupts); their leaves are checked
by recordings, the rest by reading.

Input: `Input_Read` (2d5b) builds the menus' byte `[108b]`: bit 3 fire, bit 4 the second button
(brake), bit 5 accelerate, bit 6 right, bit 7 left (for the keys, the order of the key table at 106c:
left, right, accelerate, brake, select). `[1080]` = 0 makes the menus read players 1 and 2 together.
`[107e]` is the last key released (scan code), `[107f]` the last pressed, both cleared by the screens
before they wait; Esc's release also sets `[1096]`.

## The screens

**Attract** (0100). The engines silenced, the driver's music stopped (AH 7, 8, 6) and the title tune
(1) asked for (AH 9; AH 4 starts it if the driver answers non-zero), the players' input bytes cleared.
LOGO.CHR (object 0b97) over a cleared buffer, the copyright line at row b7h; then the nine vehicle
classes in turn, each its INTRO.CHR frame (object 0bb2 at 50h, [0bb6] = 64h) with the class name below
(01de), faded in and held 118h ticks (4 s). Player 1's fire or any key released but Esc: on to SELECT
GAME (CF = 0); Esc: CF = 1. The title tune is asked for again for every class.

**SELECT GAME** (0220). LOGO.CHR, the heading, the labels ONE PLAYER / TWO PLAYER and two pictures
(SELGAM.CHR frames 0, 1 and WORDS.CHR frames 2, 1). The two-way chooser at row 80h, starting on the
previous choice `[0130]`.

**The two-way chooser** (0382). THUMB.CHR (object 0c6f) points at a choice (its frame = the choice 1
or 2; BX = its row into `[0c73]`). It first waits for fire to be let go (and clears every other bit of
`[108b]` while doing so), then each tick: left = 1, right = 2 (redraw, the timeout restarted), fire
returns CX = the choice (fire with no choice yet just redraws), Esc or 7d0h ticks (28.5 s) without a
change return CX = 0. The thumb is drawn over a saved background, 38h rows shown from the object's +2
word, the background put back at once (the screen keeps it, the buffer does not).

**One-player game** (02e0). The backdrop, the headings ONE PLAYER / GAME / SELECT GAME, the two game
types (SELGAM.CHR frames 2, 3: head to head and the challenge), the chooser at row 6eh from `[0132]`.
The choice becomes the backdrop's sub-heading `[0156]` (WORDS.CHR frame) and `[0132]`; the drivers are
reset (0eba) and the backdrop redrawn; 1 runs head to head (0fbf), 2 the challenge (102b). A cancelled
choice stores 0 in `[0132]`, so the chooser then starts with no choice (as the original).

**The backdrop** (0400). Cleared buffer, BADGE.CHR (0b7c), the title (WORDS.CHR frame 0 at 48h,8) and,
when `[0156]` is set, that frame 5ah further right, underlined by rows 1eh (colour 12h), 1fh-20h (0eh),
21h (12h).

**The drivers' carousel** (09e0, 0cd3, 0d1f, 0db0, 0e02, 0f17, 0f3c). Eleven drivers (0-10) on a ring
of positions 0..2bfh, 40h apart; the ring's turn `[0192]`; the face whose position is 140h sits in the
frame (x 5ch-a4h, rows 6eh-beh) and is the driver in front `[0160]`. The pool, bytes 193c:0164-016e,
holds the driver numbers in ring order, bit 6 set once taken (shown with FCNORMAL.CHR frame 0dh, bit 5
with 0ch). Each face is drawn on object 0be8 at x = position - d8h, its name (8-byte entries at 0258)
below at row b0h and its rating (02b1) above at row 77h, both at the object's x as clipped `[0bea]`,
skipped when the face is off screen. AX = a slot to turn to first (positions table at 016f: slot n puts
driver n in front), SI = the prompt (blinking, row 62h). A turn (0cd3) moves 40h in 13 eased steps
(byte table at 0185, 2, 2, 2, 2, 4, ... summing to 40h), one tick of `[261f]` each, the band of rows
76h-c6h shown each step. Fire on a free driver: taken (bit 6), the player's face object above the
carousel (`[019e]`: 0c03, 0c1e, 0c39, 0c54 for players 1-4) shows the driver happy and flashes it five
times (15 ticks each, bit 4 of the frame word toggling between the two happy frames), the pick is
stored (`[2668]` `[266a]` `[266c]` `[266e]`), for players 1 and 2 with the handicap answer's 80h, and
the face shown normal. Out: AX = the driver, CF = 0; Esc: CF = 1. A player's previous pick goes back
into the pool on entry (its face cleared, the name strip filled with whatever colour SI's low byte
holds after the face's opaque draw, as the original).

The face object's frame word (+13h): low byte the driver (bits 0-3), bit 4 the flash, bits 5 and 6 the
markers; high byte the expression: 0 normal (FCNORMAL.CHR, record 0a50), 1 (record 0a3c), 2 happy
(record 0a14, frame 2 x driver + flash), 3 and up (record 0a28, the same frame). Records 0a14-0a50 are
FCHAPPY, FCSAD/FCFROWN, FCNORMAL (inferred from the file names in the records).

**Handicap** (0b51). After players 1 and 2 pick Walter, Mike or Anne (drivers 0, 1, 2), unless in the
challenge (`[2656]` = 1) or with no second player (`[265a]` = 6): "HANDICAP <name> ?" with NO/YES on row
62h, left = NO, right = YES, fire or the second button confirm. The answer (0 or 80h) is remembered per
driver at 193c:01d6-01d8 and returned in AX.

**Press a key** (0c15). PRESS ANY KEY TO START blinking at row 62h until fire, a key released or 2bch
ticks (10 s) of `[261f]`.

**The blinking line** (0c96): string `[019a]`, font `[0194]`, row `[0196]`; drawn only while the
timer's blink flag `[26cf]` bit 0 is set (it toggles every 32 frames), cleared otherwise. The class name
line (0c5d, row bfh) alternates in the same way between two lists (002f and 00ab; their texts are the
same in this version).

## GAME OPTIONS and SETTINGS.DAT (confirmed)

The screen: the heading, F1 PLAYER 1 CONTROL ... F6 CREDITS (six lines, from row 34h, 10h apart), F7
CONFIGURE JOYSTICK only when a joystick was found (`[2625]` from `Joy_Detect`, called on every entry),
three lines of help at rows b0h-c0h, and the current values at x c8h (rows 34h-73h cleared and redrawn
after each change). `[0f69]` set adds a "!" there.

Keys (released): F1/F2 step player 1's/2's control, F3 the sound, F4 the smoothness, F5 REDEFINE KEYS,
F6 the credits, F7 the calibration (with a joystick), Enter plays, Esc quits (CF = 1). Any key but F5-F7
and Esc also advances or restarts the secret sequence: eight digit keys' scan codes at 193c:0f6b,
`[0f73]` the next expected; completed, it sets `[0f69]` and `[0f6a]` (the pause's debug keys).

| Setting | Variable | Values (as the screen names them) |
|---|---|---|
| player 1 control | `[0f5f]` | 1 JOY 1, 4 KEYS 1, 5 KEYS 2 (2 JOY 2 and 3 MOUSE are always skipped; 1 needs a joystick) |
| player 2 control | `[0f61]` | 1 JOY 1 (a joystick), 2 JOY 2 (two), 3 MOUSE (`[2627]`), 4, 5; never player 1's |
| sound | `[0f64]` | 0 NONE, 1 BLASTER, 2 SPEAKER: the n of DRIVERn.BIN. Default 2, or 1 if `[262d]` = 1 |
| smoothness | `[263a]` | 1 HIGH, 2 GOOD, 3 MEDIUM, 4 LOW, 5 AUTO (the speed test chooses) |

F3 reloads the driver (`Snd_LoadDriver`, interrupts off) *before* stepping the setting, so the driver
loaded is the previous setting's; the new one is loaded by Start after the screen (as the original).
Any change sets `[0f63]`.

SETTINGS.DAT (193c:0ed0), read once per run (`[0eff]`) up to 28h bytes into 193c:0da8; written on
Enter when `[0f63]` is set and the current drive (int 21h 19h) is C: or later, 32 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | word | player 1 control (read into `[0f5f]`, written from `[2658]`) |
| 2 | word | player 2 control (`[0f61]` / `[265a]`) |
| 4 | word | smoothness `[263a]` |
| 6 | word | sound `[0f64]` |
| 8 | 4 words | joystick A X low, high (`[28fd]` `[28ff]`), B (`[2905]` `[2907]`) |
| 10h | 16 bytes | the key table 193c:106c: keys 1 (left, right, accelerate, brake, select, three unused), keys 2 |

A file shorter than 32 bytes fills only as far as it goes (the spreading runs regardless, from the
buffer's old contents). Enter also sets the control types `[2658]` `[265a]`, 6 (computer) for players
3 and 4 (`[265c]` `[265e]`), the input slots (`Input_SetSlots`) and, with smoothness 5, runs the speed
test and clears the back buffer.

**Credits** (2a82): ten lines at 193c:0f75 on the backdrop until a key is released.

**Joystick calibration** (2ab5): "JOYSTICK 1" then CENTRE, LEFT, RIGHT, each answered with a button
(`Joy_WaitButtonA`, which shows "PLACE JOYSTICK THEN PRESS FIRE" blinking); the X thresholds are the
midpoints centre-left and centre-right. The same for joystick 2 (B) with two sticks. Y is read and
dropped.

**REDEFINE KEYS** (92f0): KEYS 1 then KEYS 2, five prompts each (LEFT, RIGHT, ACCELERATE, BRAKE,
SELECT); a key released answers (Esc abandons, nothing stored; space and a key already given in this
round are refused). The key's name is one character from the pairs at 193c:adf0 (scan code, character),
"?" when not listed. The ten codes go to 106c-1070 and 1074-1078; bytes 1071-1073 and 1079-107b keep
their values.

**The data-set chooser** (2be8): PLAY WHICH GAME SET ?, then every GAME?.LVL found (int 21h 4Eh/4Fh,
DTA 193c:0d7d) loaded once to show its title (from 193c:040a) at rows 3ch + 8n; the digits collected at
193c:0edd. A cursor recolours the chosen line (colour 0 to a colour cycling 1..0ah, not while the blink
flag is 1), one `Sys_WaitTick` a pass; up/down step once per press (it waits for all input to be let go
first); fire patches the digit into GAME1.LVL's name (193c:088d) and loads it. CF = 1 if no file is
found. The cursor's present passes a row and a count but calls the whole-screen present (as the
original).

## Checks

Recorded with `tools/emu/capture.py` and passed with `build/mm_fncheck` (key script below): 0400, 01de,
0eba, 0d1f, 0db0, 0e02, 0f17, 0f3c, 0c96 (see the commit messages for the hits). The routines that
wait (0100, 0220, 02e0, 0382, 09e0, 0b51, 0c15, 0cd3, 2770, 2a82, 2ab5, 2be8, 92f0) are checked by
reading; 0c5d is called from package 4's screens.

Key script that reaches the carousel (the reference harness, `--skip-card`): `40:1c,46:9c` (GAME
OPTIONS: Enter), `120:1c,126:9c` (attract: on), `400:1c,406:9c` (ONE PLAYER), `500:1c,506:9c` (the
challenge), `700:e0,701:4d,706:e0,707:cd` (turn right), `850:1c,856:9c` (pick: Walter), then PRESS ANY
KEY (about frame 1100).
