#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
# PC-test af filteret mod fejlmaalinger (FEAT-508). Kraever g++ (fx MinGW paa Windows).
# Koeres fra projektroden eller herfra:  bash test/mb_filter_host/run.sh
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/build"
mkdir -p "$OUT"
g++ -std=gnu++17 -Wall -I"$HERE/stubs" -I"$ROOT/include" -o "$OUT/mb_filter_test" "$HERE/mb_filter_test.cpp" "$ROOT/src/mb_filter.cpp"
"$OUT/mb_filter_test"
