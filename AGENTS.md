# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md) and
[openmm.emdzej.pl](https://openmm.emdzej.pl).

## What this is

OpenMM is a faithful, from-scratch reimplementation of **Micro Machines** (Codemasters, DOS, 1994), the
program `MICRO.EXE` of the 720 KB floppy release (28,955 bytes, CRC-32 `8e84eb22`, dated 1994-09-14), in
C11. It targets [gasm](https://github.com/emdzej/gasm) **0.16.1** (`openmm.wasm`: native runners on macOS,
Linux and Windows, and the browser). A native build exists for tools and headless tests only. Sibling
projects with the same conventions: `../openi76` (Interstate '76, the newest), `../opencmr2` (Colin McRae
Rally 2.0, whose file layer and gasm backend this repo started from); copy their proven pieces (bundles,
play page, launchers) rather than reinventing them.

It is the first DOS game in ENCORE: `MICRO.EXE` is a PKLITE-compressed real-mode program in hand-written
assembly. The port runs the PKLITE stub on the user's file (`src/pklite.c`) and ports the unpacked program
function by function; the machine it drove (VGA, PIT, keyboard, game port, DOS, the sound driver and the
AdLib) is modelled in the core only as far as the game depends on it (docs/re/architecture.md).

It is not an emulator or a wrapper. **Fidelity is the product.** Reproduce the original's behaviour,
including its quirks and bugs. If something looks wrong, check the disassembly; if the original does it,
keep it and add a comment. Only fix deviations *of the port* from the original. Real undefined behaviour
(reads past a buffer, an uninitialised register) is reproduced through the real-mode memory model where it
is defined there, else guarded with a comment describing what the original reads.

**What the port adds** comes after the faithful game and is always opt-in: the default is the original.
Each addition gets a `--param`, a play-page setting and a line on the public status page, and must leave
the default hashes unchanged.

## Decisions by the user

- 2026-11-10: the standard ENCORE approach, a function-by-function port of `MICRO.EXE` (and of `SM.EXE`,
  `MICRO.COM`'s order and the sound driver), site at openmm.emdzej.pl, an exhibit on the ENCORE site.

## State

Step 0 of `docs/re/port-plan.md` is done: the floppy image and folders (`src/vfs.c`), the exe check, the
PKLITE stub (`src/pklite.c`), the gasm skeleton (a blank 320 x 200 frame). Next: step 1, reading the
whole program. `grep -rn PENDING_NOTE src` is the to-do list; `docs/guide/status.md` is the public
record. Read `docs/re/port-plan.md`, `docs/re/architecture.md` and `docs/re/inventory.md`.

## Hard rules

1. **Never commit original data or anything derived from it.** `game/` (the floppy's files), `re/`
   (the unpacked program, decompiler dumps, emulator memory), `out/` (decoded assets) are git-ignored:
   keep it that way. No disk images, no unpacked or packed program copies, no extracted pictures, maps,
   fonts, palettes, sounds. The one exception, once there are any: the site's screenshots in
   `docs/public/screenshots/`, renders of the port made headless with `gasm-run --screenshot`, never the
   game's own files.
2. **No data copied from the executable into the source tree.** Tables, strings and constants are read at
   run time from the user's `MICRO.EXE` through `src/exe.h` (`exe_u8/u16/u32`, `exe_str` by image offset,
   `EXE_LIN(seg, off)`; size + CRC checked, the unpacked image too). The PKLITE stub's tables likewise
   (`src/pklite.c` reads them from the decrypted stub). Small algorithmic constants and instruction
   operands are fine. Tests check strings by length + CRC.
3. **Cite the original.** Every ported function names its source: `/* Name 1000:xxxx */`; globals keep
   their address in the declaration comment (`193c:xxxx`). Addresses are seg:off with the image at
   segment 1000h, the same in the port, in Ghidra and in `tools/emu`. Names match
   `tools/ghidra/names.tsv`, the record; no `FUN_1000_xxxx` in `src/`.
4. **Don't paste decompiler output** into source or docs. Write the port and the notes in your own words;
   describe layouts, formulas and addresses.
5. Prior art (the unrelated SourceForge remake also called OpenMM, community notes) may inform format
   understanding; the program is the authority and no code is copied.
6. **All data access goes through `src/vfs.h`**: no `fopen` or `opendir` in the core. `src/platform_gasm.c`
   is the only file that includes `gasm.h`.
7. **Never add a blocking loop or a thread** in the core; keep the number and order of presents and timer
   reads the original has (the frame shape is chosen in step 1: docs/re/architecture.md).
8. **Explain the game, not only port it.** Keep `docs/re/` and `docs/formats/` accurate as each subsystem
   is ported; when the game plays through, write `docs/guide/theory.md`, the theory of operation: how the
   game was designed and how it works, in prose, for someone who wants to build such a game. No
   decompiler text, no game data, no addresses in the main text.
9. Credit every outside source you lean on in `docs/credits.md` as you go.

## Layout

| Path | What |
|---|---|
| `src/` | Portable core. `exe.c` (the exe check, the unpacked image), `pklite.c` (the stub, ported), `vfs.c` (folders, FAT12/16 images), `app.c` (the frame), `pending.h` |
| `src/platform.h`, `src/platform_gasm.c` | The platform contract and its only gasm implementation |
| `tests/` | Native tests against the user's data; each skips (exit 0) without it |
| `tools/` | `fat.py` (an independent FAT reader), `dis16.py` (16-bit disassembly), `unpack.c` (`build/mm_unpack`: the unpacked exe for Ghidra), `emu/` (Unicorn: the DOS loader, the stub run), `ghidra/` (names.tsv, scripts), `fetch-gasm-sdk.sh` (the one `GASM_VERSION` pin), `fetch-gasm-runner.sh` |
| `docs/` | VitePress site (`guide/`, `howto/`). `docs/re/` and `docs/formats/` are internal notes, excluded from the build |

## Game data

The user's copy: `~/Downloads/003508_micro_machines/disk1.img` (737,280 bytes, FAT12, label MICROMASTER).
Development copy: `./game` (`tools/fat.py extract <image> game`; not 7-Zip, which adds the volume label
as an empty `MICROMAS.TER`). Tests and tools read `OPENMM_DATA=<dir|image>` (default `./game`);
`vfs_test` compares the folder with `OPENMM_IMAGE=<image>`. The folder and the image must give identical
results. An installed copy (`C:\MICROM`, INSTALL.DAT) has the same files and layout.

## Build and test

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build      # native: tests and tools
tools/fetch-gasm-sdk.sh                         # here .deps may be a symlink to ../opencmr2/.deps (the same pins)
cmake -S . -B build-gasm -DOPENMM_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm                                                  # -> build-gasm/openmm.wasm
```

Sources are globbed: re-run both configure lines after adding a file. Both builds must be warning-free
(CI builds with `-Werror`, with GCC too).

**Before claiming a change works:**

```sh
IMG=~/Downloads/003508_micro_machines/disk1.img
OPENMM_IMAGE=$IMG OPENMM_FAT_SUMMARY=$(tools/fat.py --summary $IMG) ctest --test-dir build --output-on-failure
RUN=.deps/gasm-runner-macos-universal/gasm-run
$RUN build-gasm/openmm.wasm --asset disk=$IMG --headless 60 --mute       # folder: --asset-dir game
```

Expected (step 0): `video_fnv32=465a5dc5 audio_fnv32=1689af25 audio_frames=37799` for 60 frames, identical
for the folder and the image. A change that alters a hash must be explained by the change: say which
hash moved and why in the commit body, check it with a screenshot, and re-baseline in the same commit.

## Reverse engineering

- Ghidra project `mm`, program `micro.exe` (`build/mm_unpack` writes it to `re/micro.exe` from the user's
  file with the port's own unpacker; Ghidra's MZ loader puts it at 1000:0000). Via ghidra-cli:
  `ghidra decompile 1000:0100 --project mm --program micro.exe`, plus `x-ref to`, `disasm`, `memory`.
- `tools/ghidra/apply-names.sh` applies `names.tsv` (addr seg:off, name, module, confidence, description)
  and regenerates `re/micro.exe.c`, `re/micro.exe.meta.tsv` (size, callers, callees, strings) and
  `re/micro.exe.funcs.txt`. Rename in the TSV, never only in Ghidra. Functions reached only through
  tables go in `tools/ghidra/entries.txt` (one seg:off per line).
- Hand-written assembly: read the disassembly, not the decompiler, for anything with flags as results
  (carry returns), string instructions, segment loads and self-modifying code. `tools/dis16.py` prints any
  range.
- `tools/emu/`: Unicorn with a minimal DOS (`mzload.py`); `pklite_run.py` runs the stub. The reference
  harness for differential checks grows from it (step 1).
- Record findings in the matching `docs/re/` page with exact layouts and addresses; mark confirmed vs
  inferred.
- Unported callees go to `PENDING_NOTE(0x1000xxxx, "Name")` (logs once, neutral return); never a silent
  stub.

## Unattended runs

- Always `--headless`; never open a runner window, never use macOS `screencapture`.
- One run at a time, niced: `pgrep -fl gasm-run` first; `nice -n 19` for builds (`-j2`), tests (`-j1`),
  regressions and Ghidra scripts. Kill hung runs (a log that stopped growing).
- Hold scripted keys several frames (`100-104:KEY(Enter)`): a one-frame tap can land in a frame without a
  game step.
- Long `--screenshot` runs grow GPU memory: add a `--param drawfrom=<N-50>` before the first long run.

## Traps (already solved: don't reintroduce)

- PKLITE's literals are xor'ed with the count of bits left *after* the flag bit was taken (and the word
  reloaded); the bit read at a reload is the one shifted out before it (`src/pklite.c`).
- 7-Zip lists the volume label as a file (`MICROMAS.TER`, empty): extract with `tools/fat.py`, or the
  folder and the image differ.
- Ghidra's MZ loader uses segment 1000h: that is why the port numbers addresses from 1000h too.
- ghidra-cli: `ghidra project create mm` before the first import; without `java_home` in its config it
  waits on stdin forever.
- In zsh, keep option lists in an array (`P=(--project mm --program micro.exe)`, then `$P`).

## Docs site

VitePress in `docs/` (`cd docs && pnpm install && pnpm build`), published by `.github/workflows/pages.yml`
to openmm.emdzej.pl. No emojis. Don't overstate the state (`docs/guide/status.md`). Say "bring your own
copy"; never call the game free or freeware without an official source; never host or link copies. The
"Built for gasm" badge is hotlinked unmodified from `https://gasm.emdzej.pl/badge/`. New pages go in the
sidebar; dead links fail the build.

## Releases

Plain semver tags, no `v`; the tag must equal `project(OpenMM VERSION ...)`. **Ask before tagging or
pushing.** Until the game is ported: no bundle tooling and no tags.

## Commits

Imperative subject. The body says why, names the original function and address, what the port did wrong
before, and which check now passes (which hashes moved and why). Never commit `game/`, `re/`, `out/`,
`build*/`, `.deps/`, `.claude/`, `docs/node_modules/`.
