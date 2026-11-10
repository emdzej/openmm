# The race loop and the vehicles on screen

How a race runs once it is set up: the loop of game steps, the drawn frame, the pause and its cheats,
the rules that end a race and those of head to head, the vehicles' drawing and state handlers, the
engine notes. Internal notes (package 8 of docs/re/functions.md). Addresses seg:off with the image at
1000h; data in the data segment 193c unless said. "Confirmed" = read in the disassembly and checked
against the original in the harness (captures listed in the commits); "inferred" = from use.

Vehicle records: 164h bytes each from `193c:124c`, the four at offsets 0, 164h, 2C8h, 42Ch (BX in
every per-vehicle routine). The two players' offsets are `[2660]`, `[2662]`. Field meanings are in
re/notes_E.md and re/notes_F.md; the ones the race loop and drawing use are named below.

## The loop (`Race_Main` 1000:3039, confirmed)

1. DS = ES = 193c, the registers cleared (1000:3030), the set-up (`Race_Setup` 1000:37fc; carry: no
   race, straight to step 5), the engine notes (`Snd_InitEngines` 1000:7a97).
2. `[2621]` = FFFFh (no vehicle hidden), the frame tick count `cs:4ade` = 0, the step counter `[2638]`
   = SMOOTHNESS `[263a]` (1..4 from SETTINGS.DAT; 5 "auto" is resolved by the set-up).
3. Each game step, in this order:
   - Esc (`[1096]` = 1, set by the keyboard handler) leaves the loop at once, to step 5.
   - `Input_Read` (1000:2d5b): the input slots, the computer drivers.
   - Space (key-mask `[107c]` bit 1) pauses (`Race_Pause`, below), inside the step.
   - `Car_StepAll` (1000:4aee): steering, physics, collisions, terrain, laps.
   - Once the race is over (`[26c6]` >= 2): in head to head (`[2656]` = 2) the loop ends now; else
     the countdown `[26cc]` is decremented and the loop ends when it reaches 0 (the finish shows for
     that many steps).
   - `Race_DrawFrame` (1000:90c5), which does nothing unless `[2638]` = 1: **the picture is composed
     only on the last step of a presented frame**.
   - Per vehicle (BX = 0 .. 42Ch): `Car_StepAirAndPoints` (1000:7429), `Car_SinkStep` (1000:73e7)
     when the state `[12ae]` is not 0, `Car_HopStep` (1000:51b2; the shells too).
   - `[2638]` - 1; at 0: wait while the signed byte at `[263c + smoothness]` is at least `cs:4ade`
     (the timer handler adds one per frame), present (`Gfx_PresentScrolled` 1000:92bc), `cs:4ade` = 0,
     `[2638]` = smoothness again.

   So a presented frame holds SMOOTHNESS game steps, one drawing (on the last), one wait and one
   present; the limit table at `193c:263d..` sets how many frame ticks a presented frame takes for
   each setting (inferred: the higher settings are for slower machines, steps run without a draw).
4. The race's end (not after Esc): the race-end effect (driver AH = 5, AL = 10h) if the vehicle BX
   left by `Car_StepAll` is visible, the engines silenced (1000:7af8), then 100 times: a tick's wait
   (1000:3165), `Race_StateLostPoint` (1000:855a: the head-to-head winner's banner), a present; the
   driver stopped (AH = 8, 6); AX = 0.
5. Both ways out: if the race was won by the cheat (`[2635]` = 1) the finishing order `[2678]..[267e]`
   becomes 0, 2C8h, 164h, 42Ch and the winner `[26c4]` = 0; if head to head has a winner `[2630]`
   (1 or 2) the first two places are the players in that order. The palette fades out (1000:327a) and
   int 21h AH = 3Bh changes to the parent directory (`[261c]` = ".."). CF is DOS's.

The port keeps both waits (here and in the pause) as loops with `mach_frame()` as their body
(docs/re/porting.md): the original spins on memory its interrupt handlers move. Race_Main cannot be
captured whole (the tick wait needs interrupts); its parts are checked one by one.

## The drawn frame (`Race_DrawFrame` 1000:90c5, confirmed)

Only when `[2638]` = 1. DS and ES are saved and restored; ES = 6d78 (the back buffer, rows of 110h).

1. Tile animations by theme `[28bf]`: 1, 3, 5 the backdrop scroll (1000:8996), 2 the water (89e0),
   8 (8a2b).
2. The map: 17 columns x 14 rows of 16 x 16 tiles starting at the camera's tile (`[264a]`, `[264c]`
   >> 4), the buffer offset `(16 - y & 15) * 110h + (16 - x & 15)`. The map is words at 3b78:0000,
   192 x 192, a row 180h bytes (18h paragraphs): DS steps 18h a row (wrapping by 1200h after row
   C0h), and a row that reaches column C0h steps DS back a row so the offset wraps to the row's start.
   A map word's bits 0-14 are the tile (graphics at paragraph `1b78 + tile * 10h`, 256 bytes), bit 15
   marks a tile with a foreground. The low two bits of the buffer offset go to `cs:8994` (the sprite
   clip and the present subtract them: the scroll is kept at a multiple of 4 for the 32-bit copies).
3. Head to head: `[2913]` (both players fell) runs `Race_RespawnPlayers` (78f8), else `[2911]` = 1
   runs `Race_AwardPoint` (7759).
4. `Car_DrawAndRunStates` (7ce0): shadows, ripples, trails, shells, the state handlers (below).
5. The foreground tiles drawn again over the vehicles, from their second graphics C00h bytes further,
   colour 0 transparent. **The original keeps this list on its stack**: SP saved in `cs:90c3`, a push of
   (buffer offset, tile segment) per foreground tile during step 2, and after step 4 pops until SP is
   back at `cs:90c3` (last tile first). Nothing reads those words as memory, so the port keeps a local
   array in the same order; `cs:90c3` is still written with the SP the original has there and BP is
   left holding it. Bound: 238 tiles, 952 bytes, against the 1,000-byte stack Start sets up: a screen
   of nothing but foreground tiles would come within a few words of overflowing it (not seen).
6. The HUD (`Hud_DrawPositions` 8dfc); for each player in state 0Ch `Race_StateLostPoint`, in state
   0Bh `Race_StateScored`; when player 2 is in neither and the banner phase `[26c2]` is set,
   `Race_DrawBannerOut`.
7. The engine notes (`Snd_UpdateEngines` 7b46). No present and no wait here.

## The vehicles on screen (`Car_DrawAndRunStates` 1000:7ce0, confirmed)

In order, each over all four vehicles: the shadows of active vehicles with a height `[12d6]`; the
ripples (8386) and the trail (8083); the shells where the timer `[13a4]` runs; then the state handler,
through the word table `193c:278f` indexed by the state `[12ae]` (read from memory, dispatched by
address: the port looks the address up in the routine table), for active vehicles and for any in
state 0Eh; after it, outside head to head and theme 9, the position label (9076) over a vehicle with
no laps left `[12ed]` or over all once the race is over. In theme 9 the loop returns after the first
handler it ran (one racer).

| State | Handler | What |
|---|---|---|
| 0 | `Car_Draw` 7d73 | driving |
| 1 | `Car_StateFall` 880a | fell off or sank: turn to face 0 or 80h (themes 4, 9), a sequence, then 7 |
| 2 | `Car_StateSplash` 82be | the splash sequence at the saved point, then 0 |
| 3, 6, 8, 9 | `Sys_Ret` 35be | nothing |
| 4 | `Car_StateAnim4` 7f62 | knocked out: a sequence, then switched off in state 0Eh |
| 5 | `Car_StateAnim5` 7efa | a sequence, then 7 |
| 7 | `Car_Respawn` 6feb | back on the track |
| 0Ah | `Race_StateStart` 849b | the start's countdown blink |
| 0Bh, 0Ch | `Car_Draw` | the head-to-head scorer (spinning) and the other |
| 0Dh | `Car_StateSplash` | hit: the sequence, then 7 |
| 0Eh | `Car_Warp` 6ae5 | the warp's travel (also runs while inactive) |
| 0Fh | `Race_StateCourseDone` 8683 | theme 9: the course done in time |
| 10h | `Race_StateTimeUp` 86a8 | theme 9: the time ran out |

**Sequences** (states 1, 2, 4, 5, 0Dh): a table of step durations and, a fixed distance further, the
frame of each step (-2 no frame, -1 the end): 193c:2855 (+12h), 2879 (+12h), 289d (+0Eh), 27c1 and 27dd
(+0Eh, facing 0 and 80h), 27f9 (+1Eh), 2835 (+10h, theme 2). The step `[12b6]` (`[12b8]` for the
splash) advances when the animation counter `[12b0]` has reached its duration. With `[12c2]` set the
vehicle is drawn instead.

**Drawing.** World positions wrap at C00h; a sprite's screen position is world less camera with C00h
added once when it falls at or left of (above) minus its margin. 24 x 24 vehicle frames from segment
4d78 (32 directions, frame = heading / 8, 240h bytes), recoloured by `[1252]` for the team; in theme 9
vehicle 0 is 40 x 40 (640h bytes) and the others are not drawn; lifted by the height (not in theme 8,
where a 32 x 32 rotor cycles over it). `[1250]` = 1 when drawn, 0 when hidden (`[2621]`) or clipped:
the engine notes and the effects test it. Shadows: the same frame offset down-right by the height,
in colour 0. Effects (5d78): 24 x 24 recoloured or 40 x 40 plain.

**Trails** (skid marks, wake): a ring of eight 12-byte entries at `[12fd]` (x1, y1, x2, y2, frame,
sheet), cursor `[1296]` stepping by 0Ch from 60h back to 0Ch (entry 0 is used once). Entries are drawn
as two 8 x 8 frames and age a frame each time the timer `[12b2]` runs out (3 steps), gone at 8. A new
one when skidding (`[1284]` and moving: sheet 43e3), or on request `[1288]` (41e3), `[128a]` (3fe3),
at two points beside the tail: heading + 80h -+ 1Eh through the sine table 193c:10a0 (256 signed
bytes) / 16; theme 2 sways the angles (`[128e]` by `[1290]`, `[1292]` by `[1294]`, turning at +-1Eh).

**Ripples**: a ring of five 6-byte entries at `[135d]` (x, y, frame), cursor `[1298]` from 1Eh back
to 6; 32 x 32 frames (193c:5ee3), five frames; a new one on request `[128c]` every 6 steps. The new
ripple always goes into entry 0 (as the original), so one shows at a time.

**Shells** (`[1394]` set, timer `[13a4]`): above 28h in flight, the 8 x 8 shell (193c:72e3, frame
`[1396]` / 4) and its shadow in colour 0 at the height offset `[139a]`/`[139e]`; 28h..1Eh the burst
(193c:41e3) with frames from the word table at `cs:8780` (0 1 2 3 4 5 4 3 2 1 0); then nothing until
the timer runs out. The motion is in `Car_HopStep` (1000:51b2) with `Car_ShellStepFrac` (87f3), one
signed sub-pixel step into a 4-bit fraction.

## The height (`Car_StepAirAndPoints` 1000:7429, confirmed)

Per vehicle per step: theme 9's time limit `[26c8]` (counts while `[26ca]` = 0; at 0 vehicle 0 goes
to state 10h); the trail and ripple timers; in the air the height grows by the vertical speed / 4,
which falls by 1 a step (gravity); landing: a ripple in theme 2, a pending hit `[1384]` becomes state
0Dh, a bounce (`[12d8]`) reverses the vertical speed less the theme's gravity (word table 193c:24ff)
(the damped value it computes is unused, as the original), else the bounce is spent; the landing
effect (driver AH = 5, AL = 4, theme 2: 7) only with sound device 1.

## Rules

**The start** (state 0Ah, `Race_StateStart` 849b, confirmed): `[26ca]` = 1 while it runs; the player's
count `[26d5]` grows by the steps per frame; below 60h the player blinks with the timer's blink flag
`[26cf]` (toggled every 32 frames) and beeps (effect 9); at 60h all four vehicles go to state 0 and
`[264e]` = `[2650]` = 32h.

**The race's end** (one player): `Car_StepAll`'s lap counting sets `[26c6]` >= 2; the loop then runs
`[26cc]` more steps (position labels over all vehicles) and ends. In theme 9 the course done (state
0Fh) or the time up (state 10h) drops a banner from the top (`Race_DropBanner` 86d2): the race is over
from the banner's first frame (`[26c6]` = 2) and it falls 8 a frame to y = 7Ch while the countdown
runs. `[291d]` = 1 when the course was done.

**Head to head** (`[2656]` = 2; confirmed by reading, the two-player game was not reached in the
harness): the point bar `[26b4]` runs 0..8 from 4. When a vehicle is knocked out (state 4 notes it in
`[2682]`) or a player falls behind (`[2911]` = 1, set by the vehicles' step), `Race_AwardPoint` picks the
scorer: the knocked-out vehicle (as the original) or the player ahead in the race order (unless not in
state 0); the scorer goes to state 0Bh (it spins) and the other to 0Ch at the scorer's checkpoint,
all channels stop and effect 0Ah plays. Then `Car_StepAirAndPoints` for the scorer: a 40h-step pause
in which the bar flashes between its old and new value every 8 steps, then both players respawn
(state 0Dh, `Race_RespawnPlayers` 78f8), `[2911]` = 2, the bar moves one towards the scorer, and at 8
or 0 (or when a player has no laps left) the winner `[26c4]` is set and the race is over (`[26c6]` = 2,
effect 10h). The banners: 193c:7423 at the centre while the scorer spins (bar 2..6), slid out left
afterwards (`Race_DrawBannerOut` 8634, `[26c2]` < 3); the winner's banner 193c:7c63 slides down from
the top when the bar is at an end (`Race_StateLostPoint` 855a), the loser hidden (`[2621]`) and the
winner 1/2 in `[2630]`. The block 1000:864f..866e (a reset of the banner when it has left the screen)
is unreachable.

## The pause and the cheats (`Race_Pause` 1000:35f0, confirmed by reading)

Space: the driver stopped (AH = 8, 6), then a search of the cheat records, 12 bytes each from 193c:1bdb
to 1fcb: theme, track, x, y, effect, argument (words). The first record for this theme `[28bf]` and
track `[28c0]` whose point is within 24 pixels (signed, each axis) of player 1's vehicle applies its
effect and flashes the play area white (straight on the screen at A000:0020, 256 x 200):

| Effect | What |
|---|---|
| 0 | the byte `[0406]` less 1 (lives, inferred) |
| 1 | the race won: `[26c6]` = 4, `[2635]` = 1, the order 0, 2C8h, 164h, 42Ch, `[2670..2676]` = 7D00h |
| 2, 3, 4 | player 1's `[127c]`, `[127e]`, `[12a2]` = the argument |
| 5, 6 | `[2915]`, `[2917]` = 1 |
| 7 | player 1's `[12f9]` = 0 (no falling into holes) |
| 8 | player 1's top speed `[129c]` = 800h |
| 9 | `[2915]` = `[2919]` = 1, `[291b]` = 4 |

Then the pause proper: `[2633]` = 1, the engines silenced, the PAUSE banner (193c:9d63) drawn plain into
the back buffer and presented; a wait for a key (`[107e]`) or 8Ch frames (`[261f]`); `[2633]` = 2, the
frame redrawn (`Race_DrawFrame` with `[2638]` forced to 1: the banner disappears), the driver stopped
again, a wait for a key. With the debug flag (193c:0f69, set by the menu's secret sequence; read as
`cs:a329`) and not in theme 9: F12 (scan code 58h) saves the screen (1000:35bf); else, until 8Ch frames
have passed, holding the keys of key-mask 0600h applies effect 9, 0300h effect 1, and the pause runs
again from the flash. `[2633]` = 0 at the end.

## Engine notes (confirmed by reading; device 1 not reached)

Sound device `[0f64]` (DRIVERn.BIN). Devices other than 1: driver AH = 0Eh sets a channel's tone (CX),
AH = 10h with CX = 0 silences it; vehicles 0 and 1 on channels 0 and 1, tone 2000h - (2 |speed| capped
at FFEh, + 7F7h in the air, 100h when hidden or `[1382]`) with a jitter of -8..7. Device 1: the
program writes the driver's four channel blocks itself (2424:0008 + 10h n; +0Ah the pitch |speed| / 10
(+30h in the air, 14h, 0Ah hidden) with a jitter of -2..1, +0Ch 98h on / 91h off) and starts effect
40h + n when the driver allows (AH = 0Ah then 5); silencing zeroes the vehicles' speeds instead.
Nothing is updated while a vehicle is in state 0Bh. The jitter comes from `Snd_Random` (7cae): four
bytes at `cs:7cdb` updated as three overlapping words with xor and add constants.
