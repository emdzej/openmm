#!/usr/bin/env bash
# Applies tools/ghidra/names.tsv to the Ghidra project `mm` and regenerates the grep dumps in re/
# (git-ignored): micro.exe.c (decompiled, named), micro.exe.funcs.txt, micro.exe.meta.tsv (sizes, calls,
# strings). Addresses are seg:off with the program at segment 1000h (src/exe.h).
# First run on a fresh project (the unpacked program comes from the port's own PKLITE stub):
#   cmake --build build && build/mm_unpack          -> re/micro.exe
#   ghidra project create mm
#   ghidra import "$PWD/re/micro.exe" --project mm --program micro.exe
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p re
P=(--project mm --program micro.exe)
names=tools/ghidra/names.tsv
if [ -f tools/ghidra/entries.txt ]; then   # functions only reached through tables or computed jumps
  ghidra script run tools/ghidra/CreateFunctions.java "${P[@]}" -- "$PWD/tools/ghidra/entries.txt"
fi
ghidra script run tools/ghidra/ApplyNames.java "${P[@]}" -- "$PWD/$names"
ghidra script run tools/ghidra/DumpAllNamed.java "${P[@]}" -- "$PWD/re/micro.exe.c"
ghidra script run tools/ghidra/DumpMeta.java "${P[@]}" -- "$PWD/re/micro.exe.meta.tsv"
grep -o '^// ==== [^ ]* @ [0-9a-f:]*' re/micro.exe.c | awk '{print $5, $3}' | sort > re/micro.exe.funcs.txt
echo "re/micro.exe.c: $(wc -l < re/micro.exe.c) lines, $(wc -l < re/micro.exe.funcs.txt) functions"
