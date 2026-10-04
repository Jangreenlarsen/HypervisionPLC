# ST Logic Compiler Stress Test Results

**Dato:** 2026-03-25
**Target:** ES32D26 @ 10.1.1.30
**Test-script:** `tests/st_size_test.py`

---

## Test Design

Progressivt storre ST-programmer med 16 variable og voksende antal sekventielle IF-blokke.
Hver IF-blok: 1 assignment + 1 IF/THEN/END_IF (4 linjer, ~54 bytes).

---

## Resultater: Pre-fix (Build #1548) — Baseline

**Heap:** 111 KB fri (boot), 47 KB minimum under compile

| Test | Source bytes | IF-blokke | Compile | Execute | Exec time |
|------|------------|-----------|---------|---------|-----------|
| 1-SMALL (baseline) | 111 | 0 | OK | OK | 40 us |
| 2-MEDIUM (8 vars) | 420 | 2 | OK | OK | 49 us |
| 3-LARGE (nested IF) | 1263 | 12 | OK | OK | 102 us |
| 4-XLARGE (15 IF) | 1012 | 15 | OK | OK | 152 us |
| 5-XXLARGE (25 IF) | 1517 | 25 | **FAIL** | - | - |

**Graense:** ~1300 bytes source / 21 IF-blokke

**Fejl:** `Compile error: Memory allocation failed` — `malloc(sizeof(st_bytecode_program_t))`
(~10.5 KB) fejlede pga heap-fragmentation fra mange smaa AST-node allokeringer.

---

## Resultater: Post-fix (Build #1580) — Alle optimeringer

**Heap:** 111 KB fri (boot), 80 KB minimum under compile

| Test | Source bytes | IF-blokke | Compile | Execute | Exec time |
|------|------------|-----------|---------|---------|-----------|
| 1-SMALL (baseline) | 111 | 0 | OK | OK | 53 us |
| 2-MEDIUM (8 vars) | 420 | 2 | OK | OK | 76 us |
| 3-LARGE (nested IF) | 1263 | 12 | OK | OK | 118 us |
| 4-XLARGE (15 IF) | 1012 | 15 | OK | OK | 177 us |
| 5-XXLARGE (25 IF) | 1517 | 25 | OK | OK | 234 us |
| 6-MONSTER (40 IF) | 2291 | 40 | **FAIL** | - | - |

**Praecis graense:** 34 IF-blokke / ~2000 bytes source

**Fejl:** `Parse error: Out of memory` — AST pool kapacitet opbrugt.

---

## Sammenligning

| Metric | Pre-fix (#1548) | Post-fix (#1580) | Forbedring |
|--------|----------------|-----------------|------------|
| Max IF-blokke | 21 | 34 | **+62%** |
| Max source bytes | ~1300 | ~2000 | **+54%** |
| Min heap under compile | 47 KB | 80 KB | **+70%** |
| Heap fragmentation | Alvorlig | Minimal | Pool allokator |

---

## Implementerede optimeringer

### 1. AST node reduktion (st_types.h, st_parser.cpp, st_compiler.cpp)
- `st_function_def_t` flyttet ud af union til heap-pointer
- Ubrugt `char condition[256]` fjernet fra `st_if_stmt_t`
- Node-stoerrelse: **1920 → 140 bytes** (93% reduktion)

### 2. Direct bytecode output (st_compiler.cpp, st_logic_config.cpp)
- Compiler skriver direkte til `prog->bytecode` i stedet for mellemliggende malloc
- Sparer **10.5 KB** under kompilering

### 3. Compiler bytecode pointer (st_compiler.h, st_compiler.cpp)
- `st_compiler_t.bytecode[1024]` (8 KB array) erstattet med pointer til output
- `st_compiler_t` reduceret fra **~12 KB → ~4 KB**

### 4. AST pool allocator (st_parser.cpp)
- En stor contiguous allokering i stedet for mange smaa malloc-kald
- Eliminerer heap-fragmentation
- Dynamisk stoerrelse baseret paa tilgaengelig heap (max 512 noder, 8 KB reserve)
- Frigivet som en blok via `ast_pool_free()`

### 5. Parser early-free (st_logic_config.cpp)
- Parser og source-kopi frigives FoR compiler allokeres
- Reducerer peak heap-forbrug

---

## Filer aendret

| Fil | Aendring |
|-----|---------|
| `include/st_types.h` | function_def ud af union, condition[256] fjernet |
| `include/st_compiler.h` | bytecode[] → pointer, compile() output parameter |
| `src/st_parser.cpp` | Pool allocator, function_def pointer-adgang |
| `src/st_compiler.cpp` | Direct output, bytecode pointer |
| `src/st_logic_config.cpp` | Parser early-free, direct compile output |

---

## Resterende flaskehalse

### 1. AST pool kapacitet
Poolen reserverer 8 KB til compiler, resten gaar til noder.
Med ~100 KB tilgaengelig heap: (100K - 8K) / 156 bytes = ~590 noder max.
34 IF-blokke bruger ~200+ noder. Graensen er reelt ~500+ noder.
Men test viser at graensen rammer ~34 blokke — pga. parser/source stadig er live.

### 2. `st_logic_config_t` er ~15.5 KB per program (statisk)
4 programmer = 62 KB statisk RAM. `program_data[5000]` = 5 KB source per program.

### 3. Max 1024 bytecode instructions
Hver IF-blok bruger ~8-10 instructions. Teoretisk max ~100+ IF-blokke,
men heap-graensen rammer foerst (~34 blokke).

### 4. Error-besked leak
API returnerer `compiled=true` men med error-besked fra forrige kompilering.
`last_error` feltet ryddes ikke mellem kompileringer.
