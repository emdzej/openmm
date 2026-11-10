# File formats

Internal notes (not published). Each format gets a page once its reader in the port exists and a check
confirms it (a whole-disk summary hash, and an independent reader where one exists).

| Format | Page | Reader | Check |
|---|---|---|---|
| The floppy: FAT12 | standard (Microsoft's FAT specification); no page | `src/vfs.c` | `tests/vfs_test.c`: the image and the extracted folder give the same 162 files; summary `5b7128d6` equals `tools/fat.py --summary` (an independent reader) |
| `MICRO.EXE`: PKLITE | [pklite.md](pklite.md) | `src/pklite.c` (the stub, ported) | `tests/exe_test.c`: 82,493 bytes, CRC-32 `943cfcf8`, equal to deark's result and to the stub run in Unicorn (`tools/emu/pklite_run.py`) |
| VGA palettes (`.PAL`) | to do | | |
| `COMPRESS.PI*` | to do | | |
| Track maps (`.MAP`) and the theme files | to do | | |
