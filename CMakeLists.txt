cmake_minimum_required(VERSION 3.24)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architectures (use \"arm64;x86_64\" for universal)")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET 11.0 CACHE STRING "Minimum macOS version")
project(OpenMM VERSION 0.0.1 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
set(MM_WARN -Wall -Wextra -Wno-unused-parameter)

# The portable core: everything except host backends (no platform headers).
file(GLOB CORE_SRC CONFIGURE_DEPENDS src/*.c src/*/*.c)
set(HOST_SRC src/vfs_host.c)
foreach(f ${HOST_SRC} src/platform_gasm.c src/app.c)
  list(REMOVE_ITEM CORE_SRC "${CMAKE_SOURCE_DIR}/${f}")
endforeach()

# Platform: native (tools and headless tests) or gasm (openmm.wasm, a gasm guest):
#   cmake -S . -B build-gasm -DOPENMM_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
# (tools/fetch-gasm-sdk.sh fetches the released SDK and wasi-sdk into .deps/)
set(OPENMM_PLATFORM native CACHE STRING "Platform: native or gasm")
if(OPENMM_PLATFORM STREQUAL "gasm")
  include(Gasm)
  gasm_add_game(openmm ${CORE_SRC} src/app.c src/platform_gasm.c)
  target_include_directories(openmm PRIVATE src)
  target_compile_options(openmm PRIVATE ${MM_WARN} -O2)
  # wasm-ld's 64 KiB default stack overflows silently.
  target_link_options(openmm PRIVATE -Wl,-z,stack-size=1048576 -Wl,--strip-debug)
  return()
endif()

add_library(mm_core STATIC ${CORE_SRC})
target_include_directories(mm_core PUBLIC src)
target_compile_options(mm_core PRIVATE ${MM_WARN})
if(NOT APPLE)
  target_link_libraries(mm_core PUBLIC m)   # libm is separate from libc on Linux
endif()

# Tests read the game data through the file layer (./game, or OPENMM_DATA=<dir|image>); without it they
# skip (exit 0).
enable_testing()
file(GLOB TEST_SRC CONFIGURE_DEPENDS tests/*_test.c)
foreach(src ${TEST_SRC})
  get_filename_component(t ${src} NAME_WE)
  add_executable(${t} ${src} ${HOST_SRC} tests/plat_stub.c)
  target_link_libraries(${t} PRIVATE mm_core)
  target_compile_options(${t} PRIVATE ${MM_WARN})
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# Tools (native only): the unpacked exe for Ghidra and the emulator (into git-ignored re/).
add_executable(mm_unpack tools/unpack.c ${HOST_SRC} tests/plat_stub.c)
target_link_libraries(mm_unpack PRIVATE mm_core)
target_compile_options(mm_unpack PRIVATE ${MM_WARN})
