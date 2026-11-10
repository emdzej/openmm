# Surface cells (`ROUNDn.DIR`)

One per theme: what the ground is under each part of a track tile, and which way the course runs there.
Read whole by `Track_LoadColDir` (1000:458b) into `193c:35e3` (room for 64 tiles, 900h bytes, up to
193c:3ee3).

## Layout

36 bytes per tile, in tile order, no header: 6 x 6 cells of 16 x 16 pixels, row-major, one byte a cell.

    cell (cx, cy), 0..5 each = (x mod 96) / 16, (y mod 96) / 16
    byte = tile x 36 + 6 cy + cx

The files are 36 x the tile count, with two exceptions: theme 5's ends 18 bytes short of its last
tile (that tile's last three rows of cells are whatever the buffer held there before: an earlier race's
DIR data or the program's initial bytes) and theme 8's is longer than its 49 CT tiles: its maps use tile
numbers up to 58, whose cells are in the extra bytes.

## The cell byte

`Track_SurfaceAt` (1000:585b) stores the byte under a vehicle in `[12da]` (the previous one in `[12dc]`).
Its readers (descriptions in tools/ghidra/names.tsv, to be confirmed by packages 6 and 7):

- the high bits are the terrain class: byte >> 5 on themes 1, 2, 3 and 6 (eight classes), byte >> 4 on
  the others (sixteen), indexing the theme's handler table (`Terrain_Dispatch`, 1000:5e4e);
- the low nibble is the course's direction there (`& 7` on theme 2), which the computer drivers steer to
  (`Ai_Drive`, 1000:5429).
