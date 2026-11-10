# Architecture

How the original is built and how the port is shaped after it. Internal notes; grows as the program is
read. Addresses: seg:off with the image at 1000h (src/exe.h).

## The original

A real-mode DOS program in hand-written assembly. `MICRO.COM` runs `SM.EXE`, then `MICRO.EXE`. Its PKLITE
stub unpacks it (docs/formats/pklite.md) and enters `Start` (`1000:0006`):

1. A start-up check (`1000:31f0`); if it fails, straight to the exit.
2. `DS` = `193c` (the data segment), `SS:SP` = `7d78:03e8` relative to the load (8d78 in the port's
   numbering), `1000:7a75`, the video mode saved (int 10h 0Fh) and mode 13h set.
3. Set-up calls (`1000:26c0`, `1000:498a`), the sound driver (AH = 7), loading (`1000:2770`; carry = fail),
   flags cleared, more set-up (`1000:2be8`, `1000:321c`), the stack saved in the data segment.
4. The loop: `1000:0100` and `1000:0220` alternate (carry from each decides; a front end and a race,
   inferred), with the driver called between (AH = 9, 4).
5. The exit: the video mode back, the driver stopped (AH = 7, 8, 6), `1000:00e2`, `1000:49af`, the
   keyboard's typematic rate restored (int 16h 0305h), int 21h 4C00h.

The sound driver is a separate binary (`DRIVERn.BIN`, n from the set-up) loaded at `2424:0000` and called
far with a function number in AH. Everything else (VGA, the PIT, the keyboard, the joysticks) the program
drives itself through its ports and interrupt handlers (docs/re/inventory.md).

## The port

C11, one module per subsystem of the original, every function citing its original address. Not an
emulator: the code is rewritten, the machine underneath is modelled only as far as the game's behaviour
depends on it.

**Memory.** The game's code works on segments and offsets: the data segment, 27 far buffers at fixed
segments, the sound driver's segment, the stack. The port keeps one real-mode memory, 1 MB as DOS
addresses it, with the unpacked image at 1000:0000 and the data segment initialised from it, so that
segment arithmetic, buffer overlaps and out-of-bounds reads behave as in the original. Ported functions
read and write it through typed accessors (`mb(seg, off)`, `d16(0x107e)` for the data segment, `c8` for
the code segment's own variables: `src/mem.h`). The registers that carry values between hand-written
routines are kept exactly: every routine leaves the register file `R` (`src/cpu.h`) as the original
leaves the CPU's registers, so routines port and check one at a time (docs/re/porting.md).

**The machine.** Ported as device models in the core, behind `src/platform.h`:

- VGA mode 13h: a 64 KB A000 segment shown as 320 x 200 through the DAC's palette (6-bit values widened
  as the DAC does); the CRTC's start address and the retrace status as far as the game uses them.
- The PIT and IRQ 0: the game's timer handler runs at the rate it programs; the frame rate of the port
  follows from it (to be read: the rate and what the handler counts).
- The keyboard and IRQ 1: the handler gets set-1 scan codes in order (`plat_key_events`).
- The game port at 201h: timed axes from a controller's sticks, the buttons.
- DOS: the int 21h file calls the game makes, over `src/vfs.h` (and the user's storage for the files it
  writes: `SETTINGS.DAT`).
- Sound: the driver's interface (AH functions) with the driver ported too, over a model of the AdLib's
  OPL2 and of the speaker.

**The frame.** The original waits in its own loops, for its timer's counters and for the retrace, and its
timer handler is locked to the retrace (it reprograms the PIT on every tick to fire just before it). So
the port keeps the loops (gasm's LOOP build: `gasm_main` runs `Start`, `plat_wait_frame` returns to the
runner): the frame boundary is the retrace's start, 70.086 times a second, where the screen is shown and
the program's IRQ 0 handler runs, then its IRQ 1 handler once per key event (`src/machine.c`). Each wait
of the original is a `mach_frame()` or one of the retrace waits of `src/vga.h`. The CPU is instant: time
passes only at the boundaries, so the PIT counts the game measures are whole frames.
