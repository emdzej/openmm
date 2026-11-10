# Graphic tiles (`ROUNDnBR.PR0`, `.PR1`, `.PR2`)

The theme's 16 x 16 pictures that the map is drawn from. Two or three files per theme, each a packed
file (the decoder: `Unpack_Decode` 1000:3333, package 2). Loaded by `Track_LoadTileGfx` (1000:45ef)
through the series loader `Unpack_LoadSeries` (1000:3547) with the name `ROUNDnBR.PR0` at `193c:11e6`:
the loader rewrites the last character to '0', '1', .. and stops at the first file that does not open.

## Layout (decoded)

Each file decodes to a run of tiles, 256 bytes each: 16 rows of 16 bytes, row-major, one byte a pixel
(an index into the theme's `ROUNDn.PAL`). No header.

The loader copies file k's output (rounded down to a multiple of 256 bytes) to segment 1b78 + C00h k:
48 KB, 192 tiles, per file. Graphic tile number t (the words of track-ct.md) is therefore tile t mod 192
of file t / 192, at segment 1b78 + 10h t.

## Use in the race

`Race_DrawFrame` (1000:90c5) copies 16 rows of 16 bytes from segment 1b78 + 10h (word & 7FFFh) into the
back buffer for each visible map word, and a second time after the vehicles for words with bit 15.
Tile 0 doubles as the scrolling backdrop of themes 1, 3 and 5: `Track_CopyTileTable` (1000:4808) keeps
its 256 bytes at `193c:3ee3` and `Track_ScrollBackdrop` (1000:8996) rewrites tile 0 from that copy each
presented frame, shifted by half the camera position (mod 16 pixels, both ways, with wrap).
