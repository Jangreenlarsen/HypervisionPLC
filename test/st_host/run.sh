#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# PC-test af ST Logic-compiler + VM (BUG-433). Kraever g++ (fx MinGW paa Windows).
# Koeres fra projektroden eller herfra:  bash test/st_host/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/build"
mkdir -p "$OUT"

FLAGS="-std=gnu++17 -w -include stdlib.h -include stdint.h -I$HERE/stubs -I$ROOT/include -DBOARD_ES32D26 -DBOARD_HAS_PSRAM"
SRCS="st_lexer st_parser st_compiler st_vm st_builtins st_stateful st_builtin_timers st_builtin_counters st_builtin_edge st_builtin_latch st_builtin_signal"

OBJS=""
for f in $SRCS; do
  g++ $FLAGS -c "$ROOT/src/$f.cpp" -o "$OUT/$f.o"
  OBJS="$OBJS $OUT/$f.o"
done
g++ $FLAGS -c "$HERE/host_stubs.cpp" -o "$OUT/host_stubs.o"
g++ $FLAGS -c "$HERE/st_test.cpp" -o "$OUT/st_test.o"
g++ -o "$OUT/st_test" "$OUT/st_test.o" "$OUT/host_stubs.o" $OBJS
"$OUT/st_test"
