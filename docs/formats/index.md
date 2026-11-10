# File formats

Internal notes (not published). Each format gets a page once its reader in the port exists and a check
confirms it (a whole-disk summary hash, and an independent reader where one exists).

| Format | Page | Reader | Check |
|---|---|---|---|
| The floppy: FAT12 | standard (Microsoft's FAT specification); no page | `src/vfs.c` | `tests/vfs_test.c`: the image and the extracted folder give the same 162 files; summary `5b7128d6` equals `tools/fat.py --summary` (an independent reader) |
| `MICRO.EXE`: PKLITE | [pklite.md](pklite.md) | `src/pklite.c` (the stub, ported) | `tests/exe_test.c`: 82,493 bytes, CRC-32 `943cfcf8`, equal to deark's result and to the stub run in Unicorn (`tools/emu/pklite_run.py`) |
| VGA palettes (`.PAL`) | to do | | |
| `COMPRESS.PI*` | to do | | |
| Track maps (`ROUNDnm.MAP`) | [track-map.md](track-map.md) | `Race_LoadTrack` (`src/game/racesetup.c`) | recordings of the set-up (`mm_fncheck`, docs/re/race-setup.md) |
| Tile compositions (`ROUNDnBR.CT`) | [track-ct.md](track-ct.md) | `Race_LoadTrack` | as above |
| Graphic tiles (`ROUNDnBR.PR*`) | [track-tiles.md](track-tiles.md) | `Track_LoadTileGfx` (`src/game/track.c`; the decoder is package 2) | as above |
| Collision bitmaps (`ROUNDn.COL`) | [track-col.md](track-col.md) | `Track_LoadColDir`; read by `Track_TileAt` (package 6) | as above |
| Surface cells (`ROUNDn.DIR`) | [track-dir.md](track-dir.md) | `Track_LoadColDir`; read by `Track_SurfaceAt` (package 6) | as above |
| Tile flags (`ROUNDnBR.LEV`) | [track-lev.md](track-lev.md) | `Track_SetNames` | as above |
| Computer drivers' speed rules (`ROUNDnmB.BRK`) | [track-brk.md](track-brk.md) | `Track_SetNames`; read by `Ai_Drive` (package 6) | as above |
| Start positions (`STRT_POS.BIN`) | [strt-pos.md](strt-pos.md) | `Race_LoadTrack` | as above |
| Vehicle pictures (`ROUNDnBR.VH0`) | [vehicles.md](vehicles.md) | `Car_LoadGfx`, `Car_MirrorFrames24/40` | as above |
