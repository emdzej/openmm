# Collision bitmaps (`ROUNDn.COL`)

One per theme: which parts of each track tile are solid. Read whole by `Track_LoadColDir` (1000:458b)
into `193c:3163` (room for 64 tiles, 480h bytes, up to the DIR data at 35e3).

## Layout

18 bytes per tile, in tile order (the file is 18 x the theme's tile count), no header. A tile is cut into
12 x 12 cells of 8 x 8 pixels; the 144 cells are bits, row-major, most significant bit first:

    cell (cx, cy), 0..11 each = (x mod 96) / 8, (y mod 96) / 8
    n    = 12 cy + cx
    byte = tile x 18 + n / 8
    bit  = 80h >> (n mod 8)

## How the program reads it

`Track_TileAt` (1000:589c) takes a world position (x, y in 0..BFFh), finds the tile byte in the map and
returns the cell's bit in the carry (set: solid). The probe that moves the vehicles (package 6) calls it
for the points around a vehicle.
