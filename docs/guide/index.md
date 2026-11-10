# Introduction

OpenMM is a reimplementation of **Micro Machines** for DOS, Codemasters' 1994 PC version of the
top-down racing game with toy cars on breakfast tables, desks, pool tables and in the bath. It is ported
function by function from the original program, `MICRO.EXE`, into C11, and compiled once to WebAssembly
for [gasm](https://gasm.emdzej.pl), so the same module runs natively on macOS, Linux and Windows and in a
browser.

It is not an emulator: the game's code is rewritten, function by function, with each function citing the
address of the one it comes from. Nor is it a remake: the goal is the original's behaviour, including its
quirks, and the port is checked against the original program running in an x86 emulator. The machine the
game drove directly, the VGA card, the timer chip, the keyboard and joystick ports, and the AdLib card
through its Sound Images driver, is modelled only as far as the game depends on it.

OpenMM contains no code or data from the game. You bring your own copy; see [Game data](./game-data).

It is the first DOS game in [ENCORE](https://encore.emdzej.pl), and the first in hand-written assembly.
The original program is compressed with PKLITE, so before anything else the port runs PKLITE's own
loader on your file, the same steps the program takes at start-up, and checks the result.

The project is at its very beginning: see the [status](./status).
