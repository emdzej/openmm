# Credits

OpenMM is a reimplementation of **Micro Machines** for DOS, © 1994 Codemasters. Its sound drivers are
© 1992-94 Tony Williams / Sound Images. OpenMM is not affiliated with either; it contains none of their
code or data and runs the game from your own copy.

## Tools

- [Ghidra](https://ghidra-sre.org) (NSA) and [ghidra-cli](https://github.com/akiselev/ghidra-cli): the
  disassembly and its names.
- [Capstone](https://www.capstone-engine.org) and [Unicorn](https://www.unicorn-engine.org): the
  exploration disassembler and the emulator the PKLITE stub is run in for checking.
- [deark](https://entropymine.com/deark/) by Jason Summers: an independent PKLITE decompressor, used to
  check the port's.
- [gasm](https://gasm.emdzej.pl) and [wasi-sdk](https://github.com/WebAssembly/wasi-sdk): the runtime and
  the compiler.
- The file layer, the gasm backend and the project layout come from the sibling projects of
  [ENCORE](https://encore.emdzej.pl), OpenCMR2 and OpenI76 above all.

## Prior work

[OpenMM on SourceForge](https://sourceforge.net/projects/openmm/), a GPL remake of Micro Machines (C++,
Qt), shares the name and the subject but no code: it is a new game in the spirit of the original, this
project a port of the original program.

Gaps in these credits are bugs: [tell us](https://github.com/emdzej/openmm/issues).
