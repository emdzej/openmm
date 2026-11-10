# Build from source

You need CMake 3.24 or later and a C compiler for the native build (tests and tools), and the gasm SDK
with wasi-sdk for the WebAssembly module (a script fetches both).

```sh
git clone https://github.com/emdzej/openmm && cd openmm
cmake -S . -B build && cmake --build build && ctest --test-dir build
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENMM_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm
```

The module is `build-gasm/openmm.wasm`. Run it with a gasm runner (`tools/fetch-gasm-runner.sh <platform>`
downloads the released one):

```sh
gasm-run build-gasm/openmm.wasm --asset disk=disk1.img
```

The native tests read the game from `./game` (the floppy's files) or from `OPENMM_DATA=<folder or image>`,
and skip without it.
