#!/bin/sh
# Decompile functions of sora_1st.exe from the Ghidra project (analysed once, see PROJECT.md).
# Usage: tools/decompile.sh <outFile> exe+30100 [exe+2b6c40 ...] [x:exe+908db8 ...]
#   x:<addr> lists every instruction that references <addr> instead of decompiling.
# The output stays outside git if written to the scratchpad or ~/tools (it is derived from the game exe).
GHIDRA="${GHIDRA:-$HOME/tools/ghidra_12.2_PUBLIC}"
PROJ="${GHIDRA_PROJ:-$HOME/tools/ghidra_projects}"
HERE="$(cd "$(dirname "$0")" && pwd)"
out="$1"; shift
case "$out" in /*) ;; *) out="$(pwd)/$out" ;; esac
MAXMEM=8G "$GHIDRA/support/analyzeHeadless" "$PROJ" sora1st -process sora_1st.exe -noanalysis -readOnly \
    -scriptPath "$HERE/ghidra_scripts" -postScript Decompile.java "$out" "$@" 2>&1 | grep -E "ERROR|Exception|written"
