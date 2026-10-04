#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# PC-test af CLI-parseren (FEAT-436). Kraever g++ og python (fx MinGW paa Windows).
# Koeres fra projektroden eller herfra:  bash test/cli_host/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/build"
mkdir -p "$OUT"

FLAGS="-std=gnu++17 -w -fpermissive -include stdlib.h -include stdint.h -include clistub.h -I$HERE -I$HERE/stubs -I$ROOT/test/st_host/stubs -I$ROOT/include -DBOARD_ES32D26 -DBOARD_HAS_PSRAM"
SRCS="cli_parser cli_commands counter_config timer_config modbus_fc_read counter_sw"

OBJS=""
for f in $SRCS; do
  g++ $FLAGS -c "$ROOT/src/$f.cpp" -o "$OUT/$f.o"
  OBJS="$OBJS $OUT/$f.o"
done
g++ $FLAGS -c "$HERE/hand_stubs.cpp" -o "$OUT/hand_stubs.o"
g++ $FLAGS -c "$HERE/cli_test.cpp" -o "$OUT/cli_test.o"
BASE="$OUT/cli_test.o $OUT/hand_stubs.o $OBJS"
python "$HERE/gen_stubs.py" "$OUT/auto_stubs.cpp" g++ -o "$OUT/cli_test" $BASE
g++ -std=gnu++17 -w -c "$OUT/auto_stubs.cpp" -o "$OUT/auto_stubs.o"
g++ -o "$OUT/cli_test" $BASE "$OUT/auto_stubs.o"
"$OUT/cli_test"
