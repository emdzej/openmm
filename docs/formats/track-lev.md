# Tile flags (`ROUNDnBR.LEV`)

One per theme: one byte per track tile. Read by `Track_SetNames` (1000:3b50), up to 80h bytes, into
`193c:1b5b`; `[28b9]` = 1b5b, or 0 when the file does not open (all nine themes have one).

## Layout

Byte t = the flags of tile t (the map byte's low six bits). The files are as long as the theme's tile
count or a little longer (theme 2's has three bytes more than its 60 tiles).

## How the program reads it

`Car_Physics` (1000:5be7, package 6) reads `[28b9] + (tile & 3Fh)` at 1000:5dd9, after committing a
vehicle's move in states 0, 2 and 0Eh: the byte goes into `[12e0]` of the vehicle record, and with bit 7
set the rest of that step's tests are skipped (on theme 3 a per-tile byte at 193c:25cc is tested too).
`[12e0]` is read again at 1000:6239 (a terrain handler). The bits' meanings are for packages 6 and 7.

The original does not test the pointer: with no file, `[28b9]` = 0 would make it read the data
segment's first bytes.
