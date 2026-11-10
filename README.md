# OpenMM

A faithful reimplementation of **Micro Machines** (Codemasters, 1994), the DOS version, in C11, ported
function by function from the original program `MICRO.EXE`. It runs the game from your own floppy, as one
WebAssembly module for the [gasm](https://gasm.emdzej.pl) game runtime: natively on macOS, Linux and
Windows, and in the browser.

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

**Status: groundwork.** The file layer reads the floppy image and its files, the program is checked and
unpacked by the port's own PKLITE loader, and the gasm module starts and shows a blank screen. Nothing of
the game runs yet; see the [status page](docs/guide/status.md) and the [port plan](docs/re/port-plan.md).

- [What is on the disk](docs/re/inventory.md), [architecture](docs/re/architecture.md),
  [port plan](docs/re/port-plan.md)
- [File formats](docs/formats/index.md)

## Data

OpenMM contains no code or data from the game and never will. You need your own copy of Micro Machines for
DOS: the 720 KB floppy (as a disk image, or its files) or the game installed from it. The supported
program is `MICRO.EXE` of 28,955 bytes (CRC-32 `8e84eb22`); other versions are refused with a message.

## Build

    cmake -S . -B build && cmake --build build && ctest --test-dir build
    tools/fetch-gasm-sdk.sh
    cmake -S . -B build-gasm -DOPENMM_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
    cmake --build build-gasm
    gasm-run build-gasm/openmm.wasm --asset disk=disk1.img      # or --asset-dir <the floppy's files>

## Part of ENCORE

OpenMM is one of the [ENCORE](https://encore.emdzej.pl) reimplementations, with
[OpenI76](https://openi76.emdzej.pl), [OpenCMR2](https://opencmr2.emdzej.pl) and the others, sharing their
conventions.

## License

GPL-3.0 ([LICENSE](LICENSE)). Micro Machines is © 1994 Codemasters. OpenMM is not affiliated with
Codemasters or with the unrelated projects of the same name.
