# Track maps (`ROUNDnm.MAP`)

One per track: n = the theme (1-9), m = the track (1-4). 2,048 bytes, read whole by `Race_LoadTrack`
(1000:3c09) into `193c:2963`. docs/re/race-setup.md has the memory it ends up in.

## Layout

| Offset | Size | What |
|---|---|---|
| 0 | 1,024 | the tiles: 32 x 32 bytes, row-major (row = world y / 96, column = world x / 96) |
| 400h | 1,024 | the checkpoint numbers, one byte per tile in the same order |

A track tile is 96 x 96 world pixels, so the world is 3,072 (C00h) pixels square; the start camera
wraps by C00h.

**Tile byte.** Bits 0-5: the theme's tile (0-63), which indexes the composition in `ROUNDnBR.CT`
(track-ct.md), the collision bitmap in `ROUNDn.COL` (track-col.md), the surface cells in `ROUNDn.DIR`
(track-dir.md) and the flag byte in `ROUNDnBR.LEV` (track-lev.md). Bits 6-7: returned separately by
`Track_TileAt` (1000:589c, in DI); all four values occur on every theme (their meaning: with the vehicle
routines, package 6). The expansion ignores them.

**Checkpoint byte.** The tile's place along the course, returned by `Track_TileAt` (in ES) for the lap
count, the short-cut test and the computer drivers' speed rules (`ROUNDnmB.BRK`, track-brk.md). At
load, `[2652]` = the largest checkpoint byte of the map (an unsigned scan of all 1,024), `[2654]` = half
of it (a jump of more checkpoints than that in one step is a short cut).

## How the program uses it

`Race_LoadTrack` expands the 32 x 32 tiles through the theme's compositions into a 192 x 192 grid of
graphic-tile words (each tile byte becomes a 6 x 6 block of 16-pixel tiles), rows 0-95 at 3b78:0000 and
96-191 at 4478:0000, 384 bytes a row. The tile and checkpoint bytes stay in the data segment for the
lookups.

Tile numbers above the theme's last composition occur (theme 8's maps use tiles up to 58 with 49 in its
CT file, theme 9's up to 60 with 59): the expansion then copies whatever the work segment holds past the
end of the CT file, as the original does (the port reads the same memory).
