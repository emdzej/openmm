# Tile compositions (`ROUNDnBR.CT`)

One per theme: how each of the theme's track tiles (96 x 96 pixels) is made of 16 x 16 graphic tiles.
Read whole by `Race_LoadTrack` (1000:3c09) into 6d78:0000 (the work segment) for the map's expansion;
not kept after the set-up.

## Layout

72 bytes per tile, no header; the file holds the theme's tiles in order (49 to 64 of them).

| Offset in the tile | What |
|---|---|
| 0 | row 0: six little-endian words, left to right |
| 12 | row 1 |
| ... | |
| 60 | row 5 |

Each word is a graphic tile number: the 256-byte picture at segment 1b78 + 10h x number
(track-tiles.md). The files use bit 15 nowhere and stay below 200h (the 128 KB of tile memory); bit 15
is added in memory for GAME1's themes 2 and 3 (docs/re/race-setup.md, "Foreground tiles").

## How the program reads it

For map byte b, the composition at (b & 3Fh) x 72 is copied as a 6 x 6 block of words into the map grid
(rows of 12 bytes, 384 bytes apart). The tile animations later replace some of those words
(docs/re/race-setup.md).
