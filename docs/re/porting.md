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

No decompiler text in the source: write the routine from the disassembly in your own words, one
statement per step of the original where it matters, comments for what each step means. `tools/dis16.py`
disassembles (`.deps/emu-venv/bin/python tools/dis16.py re/micro.exe 1000:xxxx 60`).

## Checking a routine

1. Record calls of the original: `.deps/emu-venv/bin/python tools/emu/capture.py 1000:xxxx --hit N
   --skip-card [--keys ...]` writes `out/fn/1000_xxxx_N/` (memory before and after, the registers, the
   ports, ints and driver calls in order). Interrupts are held while the routine runs; a routine that
   waits for the timer cannot be recorded alone (check it through its callers or whole runs).
2. `build/mm_fncheck out/fn/1000_xxxx_N ...` runs the port from the same state with the recording
   serving its ports and calls, and compares: each port access and its value, each int's registers, the
   registers at return, all memory except the stack below the entry SP. `ok` or the first differences.
3. Record several hits, chosen so that each branch runs (different screens, keys, states). A routine is
   done when all its recordings pass; list the hits in the commit message.

Whole runs: `build/mm_run --frames N --keys ... --hashes --dump F` runs the port headless; the harness's
`tools/emu/run.py` the original. Their memory at a frame boundary compares the same way.
