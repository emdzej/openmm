# Porting a routine

How the original's routines become C in this repo, and how each is checked. Read with
docs/re/architecture.md (the machine) and docs/re/functions.md (the map). Addresses: seg:off with the
image at 1000h.

## The convention: register-exact

The original is hand-written assembly. Routines pass arguments in registers, return in registers and in
the carry flag, and callers often use what a routine leaves in the others (a counter run down, a pointer
moved past a table). The port keeps those hand-overs exactly:

- Every routine of `tools/ghidra/names.tsv` is a `void Name(void)` in `src/game/<module>.c`, with the
  citation `/* Name 1000:xxxx */` on the line above. `build/gen/routines.h` declares them all; a routine
  that is not ported yet has a generated stand-in that logs `PENDING_NOTE` once.
- The registers are the global `R` (`src/cpu.h`): `R.ax` ... `R.es`, `R.cf`, `R.zf`, `R.sf`, `R.df`, with
  `AL`, `SET_AL(v)` and the like for the halves. **On return, R holds what the original's registers hold
  at its `ret`**: all of ax, bx, cx, dx, si, di, bp, ds, es and CF. ZF and SF too where a caller tests them
  after the call. Inside the routine, compute on locals freely.
- `push`/`pop` become locals; `pusha`/`popa` around a body mean the general registers come back as they
  were (save `R` in a local and restore the fields the original restores). SP and SS are kept only where
  the original reads or saves them (Start saves SS:SP in [4] and [6] to come back to its loop).
- Memory is the real-mode memory (`src/mem.h`): `mb/mw/mbw/mww(seg, off)` with 64 KB wrap, `d8/d16/d8w/d16w`
  for the data segment (DS = 193c, `SEG_DATA`), `c8/c16/c8w/c16w` for `cs:` variables. Use `R.ds`/`R.es`
  when the original uses whatever DS/ES holds, `SEG_DATA` when it loads the data segment itself. The
  game's tables and pictures are in that memory (loaded from the user's files), so the port reads them
  there, never from constants in the source.
- `call near X` → `X();`. A jump to another routine's entry (tail jump) → `X(); return;`. Falling
  through into the next routine → call it, then return. A jump into the middle of another routine: name
  that spot as its own entry in names.tsv and port it as a routine.
- `int n` → `mach_int(n)` with R set as the original sets the registers. `call far 2424:0000` (the
  sound driver, FONT.BIN) → `mach_drv()`. A far call through a variable (the old timer vector) →
  `mach_call_far(seg, off)`.
- `in`/`out` → `io_in8(port)`, `io_out8(port, v)`, `io_out16(port, v)` for `out dx, ax`.
- Waits: a loop on 3DAh bit 3 "until set" → `vga_wait_retrace_start()`, "while set" →
  `vga_wait_retrace_end()` (`src/vga.h`); a loop waiting for a counter the timer handler moves
  (`[28f7]`, `[0002]`, `[261f]`) → keep the loop, with `mach_frame()` as its body. Never spin without one.
- String instructions (`rep movsb`, `stosw`, `lodsb`, `scasb`): loops over the accessors, with SI/DI/CX
  left as the CPU leaves them, DF honoured if the original sets it, segment wrap kept.
- Self-modifying code: the program patches some of its own instructions (docs/re/functions.md lists
  them, e.g. `Copy_Patch32` writes `movsd` over a loop). The port reads the patched bytes back where the
  original would execute them and branches on them, so a patch has the same effect whenever it is made.
- 8- and 16-bit arithmetic wraps: cast to `uint8_t`/`uint16_t` at every store; signed compares
  (`jl`, `jg`) on `int16_t`/`int8_t`; `idiv`/`div` truncate, and a divide overflow is the original's crash
  (guard it with a comment saying what happens).
- Quirks and bugs are kept and commented "as the original".
- `Main_Restart` (1000:00cc) reloads SS:SP saved by Start and jumps back into Start's loop (a longjmp;
  wasm has none). The port sets `g_restart` (`src/game/game.h`) and returns; every routine between
  Start's loop and a jump to 00cc returns at once after a call when `g_restart` is set
  (`CALL_R(X)`), and Start clears it where the original lands (1000:0054).

No decompiler text in the source: write the routine from the disassembly in your own words, one
statement per step of the original where it matters, comments for what each step means. `tools/dis16.py`
disassembles (`.deps/emu-venv/bin/python tools/dis16.py re/micro.exe 1000:xxxx 60`).

## Checking a routine

1. Record calls of the original: `.deps/emu-venv/bin/python tools/emu/capture.py 1000:xxxx --hit N
   --skip-card [--keys ...]` writes `out/fn/1000_xxxx_N/` (memory before and after, the registers, the
   ports, ints and driver calls in order). Interrupts are held while the routine runs; a routine that
   waits for the timer cannot be recorded alone (check it through its callers or whole runs).
   Calls the routine makes to routines **not ported yet** (`build/gen/unported.txt`, read by default)
   are recorded whole, like the driver's: the generated stand-in replays them. So any routine can be
   checked before its callees are ported. Record again after porting a callee (the recording then has
   the callee's own steps, which the port now runs).
2. `build/mm_fncheck out/fn/1000_xxxx_N ...` runs the port from the same state with the recording
   serving its ports and calls, and compares: each port access and its value, each int's registers, the
   registers at return, all memory except the stack below the entry SP. `ok` or the first differences.
3. Record several hits, chosen so that each branch runs (different screens, keys, states). A routine is
   done when all its recordings pass; list the hits in the commit message.

Whole runs: `build/mm_run --frames N --keys ... --hashes --dump F` runs the port headless; the harness's
`tools/emu/run.py` the original. Their memory at a frame boundary compares the same way.

## Checking whole runs

The routines that wait on the timer cannot be recorded alone; they are checked by running the original
(`tools/emu/run.py --skip-card --hashes`) and the port (`build/mm_run --param splash=0 --hashes`) on the
same key script and comparing the pictures (`tools/emu/seqcmp.py`: the sequence of distinct pictures,
frame numbers alongside). `run.py --shot-at` and `mm_run --shot` take the same frame numbers, for a
pixel comparison of any picture that differs.

The two-player route (`out/h2h_keys.txt`, 4,200 frames: options, the menus, both pickers, CHOOSE GAME,
SELECT VEHICLE, a Formula One race on the bath with scoring): 443 of the original's 503 pictures appear
in the port in order, and the picture changes fall on the same frame or within a frame or two. Every
difference seen comes from the harness's timing, none from the port:

- loads take emulated time in the harness (the black screen before a race lasts 179 frames there, 172
  in the port, which loads at once), so the race starts 7 frames later and runs 14 behind;
- the harness's timer and its frames beat every 169 frames (the timer handler measures the frame by
  polling the retrace, and the harness's clock moves with each port read): on the beat a second tick
  falls within a frame, the race presents after one frame instead of two, and screens copied in bands
  are caught half-copied;
- the blink flag, which the timer toggles every 32 ticks, drifts a few frames against the port's by
  the same beat: a key that lands at a flip (the carousel's prompt left drawn or not after a pick, the
  start's blinking cars) gives a picture of the other phase.

The one-player route with accelerate held from before the start and no steering
(`out/race_hold_keys.txt`, 3,000 frames: the menus and the Powerboats qualifier, the computer drivers
racing): 829 of the port's 833 pictures appear in the original's sequence in order; the original's
extras are the beat's half-copied pictures every 169 frames and the start's blink in the other phase.
A key script that steers does not compare this way: its taps are tied to frames, and the race runs 13
frames later in the harness and gains a present on each beat, so the same tap meets another moment of
the race. Each present follows a fixed number of game steps, so a constant input gives the same
pictures whatever the timing.

The one-player series (`out/series_keys.txt`: accelerate held, Enter every 200 frames from 2000 to
3200, 6,000 frames; `--poke-when 193c:26ca=01/193c:12ed=0000` on both, so player 1 has no laps left
while the start runs and the qualifier ends at once): the qualifier, QUALIFIED FOR CHALLENGE, the
opponents' picks and the whole first challenge race. 1004 of the port's 1025 pictures appear in the
original's sequence in order; the rest are the beat, the load time and the start's blink in the other
phase (player 1's car shown in one and not the other, checked with `--shot-at`). Keys must not fall
inside a race: Enter is player 1's select key there, and it lands at another moment of each.
`--poke-when cond&cond.../writes` (run.py and mm_run alike, repeatable, tested in order) tests the
conditions at each frame's end and writes each time they come to hold.

GAME OPTIONS' screens (`out/options_keys.txt`: F6 the credits and a key back, F4 twice, F3, F5 REDEFINE
KEYS with ten letters 60 frames apart, Enter; 2,600 frames): all 21 of the port's distinct pictures
appear in the original's sequence in order (`Menu_GameOptions` 1000:2770, `Menu_Credits` 1000:2a82,
`Menu_RedefineKeys` 1000:92f0).
The poke is tested at frame ends, which fall at another phase of the game steps in the harness than in
the port, so a race ended by it can end one step apart: one labelled picture more or less before the
race's 100-step end countdown runs out (seen at the second race of the series). Enter during a race
changes nothing in either (checked both ways).

A whole championship (`out/champ_keys.txt`: accelerate held, Enter every 200 frames from 2000) with three
pokes, so that every race ends at its start and no key reaches a race:

    --poke-when "193c:12ed=0300&193c:28bf=09/193c:12ed=0200,193c:1070=00"   # a bonus race: course done
    --poke-when "193c:12ed=0300/193c:12ed=0000,193c:1070=00"                # a race: player 1 finished
    --poke-when "193c:26cc=0000/193c:1070=1c"                               # after it: select is Enter again

(laps-to-go 3 is written by the set-up and holds until a lap, so both runners see it at a frame end;
`[1070]` is player 1's select key, which in a race is fire and skips the pedals: an Enter that lands
while a car accelerates changes the race.) Over 16,000 frames, 2613 of the port's 2850 pictures appear
in the original's sequence in order: the qualifier, the challenge races, the bonus race, the results
and EXTRA LIFE screens.
The rest traced: the beat, the load time and the start's blink, as above, and one effect of the port's
instant CPU. `Menu_LivesMessage` (1000:1c1b) steps every five `Sys_WaitTick`s and flips the face's
flash when `[0002]` has passed 0Fh. In the harness, as on a real machine, a tick can fall while a step
draws and presents (a 64,000-byte copy to the VGA takes a good part of a frame on an ISA card): `[0002]`
counts it, the next wait still waits for a new tick, and the face flips a step early (EXTRA LIFE: one
picture in four differs). The port's steps take no time, so its ticks all fall in the waits. Any loop
that mixes a tick wait with a free-running counter has this; it is the frame model's (architecture.md),
not a port error.

A failed qualifier (`out/over_keys.txt`: no pedal at all, Enter every 200 frames from 2000; 12,000
frames): the Powerboats qualifier with player 1 standing at the start, FAILED TO QUALIFY
(`Menu_LivesMessage` 1000:1c1b), back to the character picker and round again. 2024 of the port's 2111
pictures appear in the original's sequence in order. Besides the load time and the beat, one Enter is
lost in the original: it lands on the frame where FAILED TO QUALIFY flips its face, and
`Menu_WaitKeyBlink` (1000:17ff) clears the last-pressed byte `[107f]` at each call, while `Key_Irq`
records a release only for the key last pressed since then. A press that falls in the redraw between two
calls is dropped (the original's rule; the harness's redraw takes emulated time, the port's none), so
the original waits for the next Enter and runs 200 frames behind from there. After the next pick the
character picker's WHO DO YOU WANT TO BE ? is left drawn in the original and cleared in the port: the
blink flag's phase again (checked with `--shot-at`).

The port alone plays the whole championship this way (`out/full_keys.txt`, 60,000 frames): the races to
the last, "WIN THIS RACE TO BE CHAMPION", CHAMPIONSHIP WINNER!! and back to SELECT GAME, with no
PENDING_NOTE.

The two-player tournament (`out/tour_keys.txt`: the two-player keys to CHOOSE GAME, TOURNAMENT, up held
from 3300, Enter and G in turn every 100 frames; 6,000 frames) with four pokes on both runners:

    --poke-when "193c:1070=1c/193c:09ba=0a0a0a0a0a0a0a0a"      # every race on one track
    --poke-when "193c:26b4=0400/193c:26b4=0700"                # the point bar at 7: player 1's first point wins
    --poke-when "193c:26c6=0000&193c:26cc=6400/193c:1070=00,193c:1078=00,193c:107c=0020"
    --poke-when "193c:26c6=0200/193c:1070=1c,193c:1078=22"     # select keys off while a race runs

985 of the port's 1090 pictures appear in the original's sequence in order: four races, the results
between them and the last race's end. What each poke is for:

- `Menu_TwoPlayerTournament` (1000:1faf) draws each race's track from the low three bits of the tick
  counter `[0002]`, which counts from the last key wait: a timing-dependent draw, so the two runners
  choose different tracks, and a race on another track leaves other state for the next. The table at
  `[09ba]` (eight bytes, theme << 2 | track - 1) is static; filled with one entry, every draw gives it.
- Enter and G are the players' select keys, fire in a race. The key-off poke also sets the held-key bits
  `[107c]` to up alone: a select key cleared while held keeps its bit, as `Key_Irq` (1000:2efd) matches
  the release against the table and finds nothing, so fire stays held and the pedals are skipped. In a
  two-player race the end countdown `[26cc]` is not run (it stays 64h from the set-up), so the race's
  start is the over flag `[26c6]` going back to 0 with it, and its end `[26c6]` = 2. A condition that
  holds through from one race to the next (laps-to-go 3 with 64h) never comes to hold again and fires
  at another moment in each runner.

The remaining differences are the load time, the beat and the blink flag: "Winner" flashes black and
white with it, and the character picker after the tournament keeps or clears its prompt by it, after
which the menu keys meet other moments and the runs part.
