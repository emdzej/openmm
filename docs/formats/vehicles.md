# Vehicle pictures (`ROUNDnBR.VH0`)

One per theme: the theme's vehicle drawn in a quarter turn of directions, plus other pictures. A packed
file (decoder: `Unpack_Decode` 1000:3333, package 2), loaded by `Car_LoadGfx` (1000:4611) through the
series loader (`Unpack_LoadSeries` 1000:3547, name at `193c:11f3`; only VH0 exists for every theme)
into 1b78:0000, then split and completed by mirroring. All four vehicles of a race share the pictures
(they are told apart by recolouring when drawn, package 8).

## Layout (decoded)

Frames are square, row-major, one byte a pixel (a palette index; colour 0 is the background, inferred
from the masked blits).

| Themes | Frame | Part 1 (to 4d78:0000) | Part 2 (to 5d78:0000) |
|---|---|---|---|
| 1-8 | 24 x 24 (576 bytes) | 1440h bytes: frames 0-8 | 1B00h bytes: twelve more 24 x 24 frames |
| 9 | 40 x 40 (1600 bytes) | 3840h bytes: frames 0-8 | 1F40h bytes: five more 40 x 40 frames |

Part 1's nine frames are one quarter of the turn, both ends included. On theme 8 the first 1400h bytes
of part 2 are also copied to `193c:5ee3`, where `Car_Draw32` (1000:847e) reads them as five 32 x 32
frames (400h bytes each): the same bytes, seen with another frame size. Part 2's meaning on the other
themes is for the drawing package (8).

## The mirrored directions

After the copy, 4d78 holds frames 0-8; the mirrors (`Car_MirrorFrames24` 1000:4696,
`Car_MirrorFrames40` 1000:46f6, the same code with the size changed) append 23 frames, 32 directions
in all (4800h bytes for 24 x 24, C800h for 40 x 40):

| Frames | Made from | Flip |
|---|---|---|
| 9-16 | 7, 6, .., 0 | top to bottom (the rows of frames 0-7 copied last row first, so the frame order reverses too) |
| 17-23 | 15, 14, .., 9 | left to right |
| 24-31 | 8, 7, .., 1 | left to right |

So frame k + 16 is frame 16 - k mirrored left to right for k = 1..15, and frames 0, 8, 16 and 24 are
the four axis directions. The flips read the bytes in place in the same segment, so the order of the
passes matters: the second pass reads frames the first pass wrote.
