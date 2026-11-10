# Start positions (`STRT_POS.BIN`)

One file in the data set (GAME1), 144 bytes: the start of every track. Read whole by `Race_LoadTrack`
(1000:3c09) into `193c:1eab`.

## Layout

Nine themes of 16 bytes, four tracks of 4 bytes each: two little-endian words, x and y in world pixels
(0..BFFh). The entry of theme n, track m is at (n - 1) x 16 + (m - 1) x 4 (computed in 8 bits). Tracks
that do not exist (theme 9 has three) leave their four bytes unused.

## How the program reads it

The four vehicles start on a 2 x 2 grid around the point: x + 14h, y - 0Ah, and 1Ah more in x for
the vehicles whose grid slot (`[137c + record]`, 3, 2, 1, 0 for vehicles 0..3) has bit 0 set, 1Ah more
in y for bit 1. Both the position and the previous position get the value. The camera starts at x - FAh
and y - FAh of the grid's corner (the values after the +14h / -0Ah), plus C00h when that is not above 0
(the world wraps at C00h).
