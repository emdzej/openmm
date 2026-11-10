# Speed rules of the computer drivers (`ROUNDnmB.BRK`)

One per track (none for theme 9's three tracks). Read by `Track_SetNames` (1000:3b50), up to 200h
bytes, into `193c:195b`; `[28bb]` = 195b, or 0 when the file does not open. The files are short (29 to
183 bytes).

## Layout

One byte per checkpoint number (the map's second kilobyte, track-map.md), from checkpoint 0. No header.

## How the program reads it

`Ai_Drive` (1000:5429, package 6) reads `[28bb] + [12e3]` (the vehicle's checkpoint) each step for a
computer-driven vehicle (description in tools/ghidra/names.tsv, to be confirmed with the port of
`Ai_Drive`):

| High nibble | Rule |
|---|---|
| 0 | full throttle |
| 1 | a speed limit of low nibble x 128 + 380h (adjusted by the race number and on theme 7): brake above it |
| 2 | the top speed capped at low nibble x 64 + 600h |
| other | lift while steering |

Theme 9 has no BRK files: `[28bb]` is 0 there, so a computer driver on theme 9 would read the data
segment from offset 0. Theme 9 races have no computer cars (the set-up clears vehicles 1-3), so the
case does not arise.
