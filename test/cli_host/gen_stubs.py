# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2025-2026 Jan Green Larsen
"""Genererer auto-stubs til PC-testen af CLI-parseren.

Linker testen uden stubs, samler linkerens "undefined reference"-liste og
skriver en stub pr. manglende symbol: en funktion, der registrerer kaldet
(cli_host_stub_hit) og returnerer 0. Symbolerne er de raa (mangled) navne, saa
stubben bindes via en asm-label. Funktioner, der returnerer en struct, eller
hvis output testen skal kigge paa, skal i stedet staa i hand_stubs.cpp.

Brug: python gen_stubs.py <out.cpp> <linker-kommando ...>
"""
import re
import subprocess
import sys

out_path, cmd = sys.argv[1], sys.argv[2:]
res = subprocess.run(cmd + ['-Wl,--no-demangle'], capture_output=True, text=True)
syms = sorted(set(re.findall(r"undefined reference to `([^']+)'", res.stdout + res.stderr)))
with open(out_path, 'w', encoding='utf-8') as f:
    f.write('// AUTO-GENERERET af gen_stubs.py — redigér ikke\n')
    f.write('extern "C" void cli_host_stub_hit(const char *name);\n')
    for i, s in enumerate(syms):
        f.write('extern "C" long long cli_stub_%d(void) __asm__("%s");\n' % (i, s))
        f.write('long long cli_stub_%d(void) { cli_host_stub_hit("%s"); return 0; }\n' % (i, s))
print('%d auto-stubs' % len(syms))
