# The vehicles: control, physics, collisions, the computer driver

Internal notes for work package 6 (docs/re/functions.md). Ported in `src/game/car.c`, `src/game/collide.c`,
`src/game/ai.c`. Addresses are seg:off with the image at 1000h; data are in DS = 193c. "Confirmed" means
read in the disassembly and checked by recordings (tools/emu/capture.py, build/mm_fncheck); "inferred"
means named from the effect.

## The vehicle record

Four records of 164h bytes. The routines take `BX` = the record's offset (0, 164h, 2c8h, 42ch) and address
fields as `[bx + 12xx]`, so a field's address below is the one of record 0. The four racers in player
order are the offsets at 193c:2660/2662/2664/2666 (player 1 first); the race order at 193c:2678..267e.

| Field | Meaning |
|---|---|
| 124a | the player number 1..4 (index into the control types 2658..265e) |
| 124c | active (moves, collides) |
| 124e | can be hit by a shell |
| 1250 | driven by a human (sounds play only for these) |
| 1258 / 1264 | x / y fraction (the low byte of an 8.8 sum; high byte always 0) |
| 125a / 1266 | the candidate fractions |
| 125c / 1268 | x / y position in pixels, 0..bffh (the world is 32 x 32 tiles of 96 px and wraps) |
| 125e / 126a | the candidate position (this step's move before it is accepted) |
| 1262 / 126e | the camera's look-ahead (80h, 64h: the car sits at that offset in the view) |
| 1270 / 1274 | the wanted velocity x / y (8.8, from speed and heading) |
| 1272 / 1276 | the actual velocity x / y (8.8) |
| 1278 | heading 0..ffh (only bits 3-7 index the sine table: 32 directions) |
| 127a | speed (signed; negative is reverse) |
| 127c / 127e | grip: the slip threshold and the correction step |
| 1280 | the car can slide (grip applies); 1282 sliding now |
| 1284 / 1286 | slide timers: while one runs the theme's slide values replace the grip |
| 129a | turn step (2 on themes 6 and 7, else 3; set by the race set-up) |
| 129c / 129e | top speed now / its base value (the computer driver lowers 129c) |
| 12a0 / 12a2 / 12a4 / 12a6 | reverse limit, acceleration, braking, drag |
| 12a8 | blocked this step (bounce request) |
| 12aa | a computer car's blocked count (20: put back on the track) |
| 12ac | an impulse from a collision is pending |
| 12ae | the state: 0 driving, 2 just put back, 0dh off the track, 0eh warp, ... (table 193c:278f) |
| 12ba / 12bc | the position where the car left the track |
| 12c4 / 12c6 / 12c8 / 12ca | blocked to the left / right / above / below |
| 12cc / 12ce | the tile under the car (number) / the 8-px cell in it (CH = x, CL = y) |
| 12d6 | height above the track (airborne when > 0) |
| 12da / 12dc | the surface byte now / before (high nibble the terrain class, low nibble the direction) |
| 12de | the tile's orientation (the map byte's top 2 bits) |
| 12e0 | the tile's respawn byte (bit 7: not a safe point; bits 5-6 the lane for the computer driver) |
| 12e1 / 12e3 / 12e5 | the checkpoint before / now / changed this step |
| 12eb | driven by the computer (control type 6) |
| 12ed | laps to go (finished at 0) |
| 12f1 / 12f3, 12f5 / 12f7 | the last safe point and the one before it (respawns) |
| 12fb | bounce on solid cells (cleared by some terrain) |
| 137b | the input byte: 80h left, 40h right, 20h accelerate, 10h brake, 08h fire |
| 137e / 1380 | the camera has arrived / waiting for the camera before driving |
| 1388 | on the upper level of a bridge (theme 3) |
| 138a | halve the velocity on a bounce (set every step; cleared by terrain) |
| 1390 / 13ac | a boost stage / its blocked count (50: put back) |
| 1394 | a shell in flight |
| 1396 | the shell's animation step (0..5) |
| 1398 / 139c | the shell's position |
| 139a / 139e | counters running down from 10 (the arc as drawn, inferred) |
| 13a0 / 13a2 | the shell's step words: low byte the sine, high byte fraction (top nibble) and the car's own step + 8 (low nibble) |
| 13a4 | the shell's timer: 60 at the shot, flying while 28h or more |

Global: 193c:10a0 a sine table of 256 signed bytes (unit 128), read as sin(h) at `h & f8h` and cos(h) as
sin(h - 40h). 28bf the theme (1..9: the vehicle class), 28c1 the race number, 262f the catch-up flag,
26c6 the finished-and-stopped count (2: the race is over), 2656 = 2 the two-player game, 2919 = 1 every
car may fire (inferred: a cheat), 2915 = 1 firing disabled.

## The step

Race_Main calls Car_StepAll once per game step (several steps per presented frame with the smoothness
setting). Car_StepAll does, in this order:

1. For every record, the controls (below). Records that are not racing (no input device and not active)
   fall through the tests.
2. For the four racers in player order: Car_Velocity.
3. Coll_CarsAll: the six pairs.
4. For the four racers in player order: Car_Physics (the move).
5. The camera.

Car_HopStep (the shell's flight) is called by Race_Main per vehicle after the step, not by Car_StepAll.

## Control (Car_StepAll)

**Catch-up.** A computer car while record 0 (player 1) heads the race order gets flag 262f: it
accelerates six times as fast and its grip is half as much again. The original's scan of the order list
compares the list pointer with the *value* of its last entry (a record offset), so only the first entry
is ever looked at: the flag means "player 1 is leading", whatever the other cars' places.

**Finishing.** A car with no laps left brakes to a stop (by its braking value each step); once it has
stopped, the step counts the finished, stopped cars into 26c6, and sets it to 2 at once when player 1's
car is finished. 26c6 >= 2 ends the race (Race_Main runs a countdown, then the results). In the
two-player game the end of a point is handled instead (a banner slides in; the order list is re-seeded).

**Steering.** Not steering, the heading snaps to the nearest sixteenth of the circle (a low nibble of 0..4
rounds down, c..f up, 5..b to the half), so a car released from a turn runs straight on one of 16 lines
(32 when the half is kept). Left subtracts the turn step and wraps; right adds it, and the wrap waits for
the next step's mask. On themes 1..6 turning at a crawl (speed within +-ffh) sets the speed to ffh: a
standing car pivots forward. On theme 7 (tanks) a computer car turns one step faster and a human's tank
half as fast at speeds of 320h and up.

**Pedals.** Accelerate adds the acceleration (x6 catching up) up to the top speed; brake subtracts the
braking value down to the reverse limit; neither (or after the race) applies the drag towards 0, not
past it, and none at all while airborne. Both pedals together fire.

**Firing.** Fire comes from the keys, the mouse or the computer (a joystick's fire is the pedals). It
does something only on theme 7, or with 2919 set, and only for a human's car without a shell in flight:
the shell starts an eighth of the sine table's unit ahead of the car along its heading, with the car's own
whole-pixel step kept in its step words so the shell inherits the car's motion. The names Car_StepAll_Hop
and Car_HopStep predate understanding that this is the tank's shot.

## Velocity and grip (Car_Velocity)

For an active car driving on the ground:

    wanted_x = speed * sin(heading) / 128        wanted_y = speed * sin(heading - 40h) / 128

(signed 16 x 8 bit products, scaled by taking bits 7..22). The actual velocity follows per axis:

    if can_slide and |wanted - actual| > threshold:  actual += step toward wanted;  sliding = 1
    else:                                            actual = wanted;             sliding = 0

with threshold 127c and step 127e (x1.5 when catching up), or the theme's slide values (28c2, 28c4)
while a slide timer runs (oil, ice: set by the terrain). So a car keeps its momentum through a turn
until the difference falls under the threshold: the drift. A sliding player 1 plays the skid effect
(not on themes 2 and 8, the water; on theme 6 at most every 50 calls).

## The move probe (Car_MoveProbe, Track_TileAt, Track_SurfaceAt)

The candidate position is the position plus the integer part of velocity + fraction (8.8 fixed point,
the new fraction kept), wrapped into the 3072-px world. Track_TileAt maps a point to the tile
(`y / 96 * 32 + x / 96` into the map at 193c:2963: tile number in the low 6 bits, orientation in the top
2), the checkpoint layer (193c:2d63, the next 1024 bytes), the 8-px cell and the cell's solid bit in the
tile's 12 x 12 mask (18 bytes per tile at 193c:3163). Track_SurfaceAt gives the surface byte of the
16-px cell in the tile's 6 x 6 grid (36 bytes per tile at 193c:35e3): the high nibble is the terrain
class (Terrain_Dispatch's table index, package 7), the low nibble the direction for the computer driver.

A new checkpoint number (non-zero in the second layer) is taken (12e3, the previous in 12e1, 12e5 = 1);
checkpoint ffh means off the track (state 0dh). On a solid cell, unless the surface class lets the car
through (theme 7 classes 1..7, themes 4 and 5 classes 5 and up, theme 3 with all of e0h), the move is
blocked: four probes 8 px left, right, above and below the committed position tell which way the wall
is (none: a corner, both axes). The "below" probe wraps the x coordinate instead of y (as the original),
so near the bottom edge it reads map row 32, the checkpoint layer.

Theme 3 (bridges, the warp holes) has an upper and lower level: tiles marked 1 in 193c:25cc are the
crossing; a checkpoint there counts only on the upper level (1388).

## Commit and bounce (Car_Physics)

After the probe and the terrain's handler: if the move was blocked and the car bounces (12fb), the
velocity is negated on the blocked axes (both for a corner) and halved (arithmetic shift) when 138a is
set; the candidate is recomputed. A computer car blocked 20 times is put off the track (state 0dh, then
respawned by package 7's handlers). In states 0, 2 and 0eh the candidate becomes the position. A tile
whose respawn byte has bit 7 clear is a safe point: the position is recorded (the previous one kept).

## Collisions (Coll_CarsAll, Coll_CarPair)

The pairs (1,2) (1,3) (1,4) (2,4) (2,3) (4,3). Each pair is tested on its candidate positions (the
nearer copy across the world's edge), within 16 px on both axes. The halved offset indexes a 17 x 17 byte
table at 193c:17da: 0 is no contact (the round shape's corners), else the contact direction on the
256-step circle. With s = sin(a), c = sin(a - 40h):

    j = (dvx * s) / 128 - (dvy * c) / 128,   at least 1f4h
    A.v -= (j * s / 128, j * c / 128);   B.v += the same

where dv = A.v - B.v. The floor makes every touch a push of at least 1f4h (about two pixels per step):
cars never stick. A car just put back on the track (state 2) instead pushes and is pushed diagonally
(+-40h on both axes). On theme 6 a hit harder than the floor knocks the human-driven cars of the pair off
the track. Both cars get the impulse flag; Car_Physics then recomputes their candidates. BX at the call
is whatever record the caller had: Coll_CarPair recomputes *that* record's candidate first (each time;
harmless, the same result), as the original.

## Shells (Car_HopStep, Coll_ShellHitsCar)

The shell flies while its timer is 28h..3bh (20 steps): per step, six sub-steps add the sine byte to a
4-bit fraction (Car_ShellStepFrac, package 8), then the car's step at the shot. A shell in flight within
12 px of another vehicle that can be hit ends and knocks that vehicle off (state 0dh, effect 1).

## The computer driver (Ai_Drive, Input_None)

A vehicle with no device gets its input from Ai_Drive during a race. The surface byte's low nibble is a
direction code laid into the track's surface grid (only 0..7 on theme 2). Where the tile is marked (its
orientation's bit 1) the code goes through a per-lane table (193c:191b, 16 bytes per lane; the lane from
the tile's respawn byte bits 5-6), so different lanes take different lines through the same tile. The
code picks a heading from 193c:18fb; the orientation's bit 0 turns it round (tiles placed mirrored). The
car steers left when (wanted - heading) as a signed byte is negative, right when it is 3 or more: it
holds a line within 0..2 heading steps.

Per checkpoint, a byte of the level ([28bb] + checkpoint) is the speed rule: high nibble 0 flat out
(the top speed restored), 1 a speed limit of (low nibble) * 128 + 380h (more on race 17h, less for the
tanks, less again on the late races) braking above it, 2 a lower top speed (low nibble) * 64 + 600h,
anything else lifts while steering. Fire is always held (it only matters for the tanks, and computer cars
cannot fire: the shot needs a human's car).
