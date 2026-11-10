# Port plan

The order of work. Each step ends in an automated check; a step is done when its check passes and the
notes in docs/re/ and docs/formats/ say what was learnt. Addresses: seg:off with the image at 1000h.

| Step | What | Check |
|---|---|---|
| 0 | **File layer and the exe** (done 2026-11-10). FAT12 images and folders (`src/vfs.c`); `MICRO.EXE` checked (size, CRC-32), its PKLITE stub ported (`src/pklite.c`), the unpacked image checked; the gasm skeleton shows a blank 320 x 200 frame. | `exe_test`, `vfs_test` (summary `5b7128d6` = `tools/fat.py`); `openmm.wasm` without data exits 1 with "game data not found"; with data `video_fnv32=465a5dc5` for 60 frames, the same from the image and the folder |
| 1 | **Read the whole program.** Ghidra names for every function of `1000` (tools/ghidra/names.tsv), the data segment's layout, the far buffers' users, the timer and keyboard handlers, the main loops; choose the frame shape (docs/re/architecture.md). The emulator harness: `MICRO.EXE` run in Unicorn with the DOS and device calls served (`tools/emu/`), as the reference for every later step. | names.tsv covers `1000`; docs/re/architecture.md describes every top-level routine; the harness runs `Start` to its first frame |
| 2 | **The machine.** Real-mode memory with the image, the data segment and the buffers (`src/mem.c`); VGA mode 13h, the DAC, retrace; the PIT and the game's IRQ 0 handler; the keyboard's IRQ 1 handler; DOS file calls over the VFS. | unit tests per device; the timer handler's counts match the emulator's over N ticks |
| 3 | **Formats and drawing.** Palettes, the `COMPRESS.PI*` pictures, fonts, the track tiles and vehicles, each ported from the program's own reader and drawn by the port. | per format: a whole-disk summary hash; the decoded buffers equal the emulator's run of the original reader, byte for byte |
| 4 | **`SM.EXE` and the front end.** What `SM.EXE` shows; the title, menus, player and vehicle selection, the set-up (keys, joysticks, sound) and `SETTINGS.DAT`. | headless runs with scripted keys reach each screen; frame hashes recorded in tests/hashes.txt |
| 5 | **The race.** Track loading, the camera and scrolling, the vehicles' handling and collisions, the computer drivers, the rules (laps, the screen edge in two-player), the HUD. | per subsystem: differential runs against the emulator from the same state; a race plays to its end headless with scripted input |
| 6 | **The game's structure.** Championship, qualifying, bonus races, time trials, head-to-head, the cheats. | a championship plays through headless |
| 7 | **Sound.** The sound driver (`DRIVER1.BIN`, `DRIVER2.BIN`) ported, an OPL2 model and the speaker into the gasm audio stream. Which OPL2 model (written here, or a permissively licensed one vendored with credit) is the user's decision. | `audio_fnv32` stable; the driver's register writes equal the emulator's for the same calls |
| 8 | **Browser player** (done 2026-11-10). `docs/public/play/` from the siblings, the floppy image or folder from the player's own copy. | `tools/web-play-test.mjs` |
| 9 | **Site and release.** Status, game data, playing; bundles in CI. Tags only when the user asks. | `pnpm build` with no dead links; bundle smoke tests |
| 10 | **Theory of operation.** `docs/guide/theory.md`: how Micro Machines was designed and how it works, in prose. | the encore-theory-of-operation checklist |

Opt-in additions (if any) come after step 7 and never change the default hashes.
