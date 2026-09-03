# FEAT-003 API Test Resultater

**Dato:** 2026-02-14 (endelig)
**Tester:** Claude Code (automatiseret)
**Target:** ESP32 @ http://10.1.1.201/api
**Auth:** Basic (api_user:ChangeMe123!)
**Build:** #1224 (BUG-211 + BUG-212 fixes)

---

## Executive Summary

### Fase 1: API Endpoint Fixes (DONE)
BUG-207/208/209/210 er fixet. Alle 12 API endpoints virker.

### Fase 2: FEAT-003 FUNCTION/FUNCTION_BLOCK Test (DONE)
BUG-211 og BUG-212 er fixet. Alle 11 tests bestået.

---

## FEAT-003 Test Resultater (Build #1224)

| Test | Beskrivelse | Status | Forventet | Faktisk |
|------|-------------|--------|-----------|---------|
| 1.1 | FUNCTION DOUBLE(val) := val*2 | **PASS** | result=14 | result=14 |
| 1.2 | ADD3(a,b,c) - 3 parametre | **PASS** | result=60 | result=60 |
| 1.3 | CLAMP_POS(-5) - lokal var + IF | **PASS** | result=0 | result=0 |
| 1.4 | DOUBLE(5) + TRIPLE(5) - 2 funktioner | **PASS** | r1=10, r2=15 | r1=10, r2=15 |
| 1.5 | DOUBLE(DOUBLE(3)) - nested calls | **PASS** | result=12 | result=12 |
| 2.1 | FUNCTION_BLOCK COUNTER() | **PASS** | compiled=true | compiled=true |
| 2.2 | FB ACCUM med VAR_INPUT | **PASS** | compiled=true | compiled=true |
| 3.1 | Forkert param-antal | **PASS** | compile error | "expects 1 arguments, got 2" |
| 3.2 | Ukendt funktion | **PASS** | compile error | "Unknown function: UNKNOWN_FUNC" |
| 3.3 | Syntax error i funktion | **PASS** | parse error | "Expected END_FUNCTION" |
| CTRL | FUNCTION uden BEGIN i body | **PASS** | result=14 | result=14 |
| CTRL | FORTYTWO() - no params | **PASS** | result=42 | result=42 |
| CTRL | Delete+re-upload (BUG-212) | **PASS** | compiled=true | compiled=true |

---

## Bugs Fundet og Fixet

### BUG-211: Parser mangler FUNCTION/FUNCTION_BLOCK i PROGRAM body (FIXED Build #1224)

**Root cause:** `st_parser_parse_statement()` havde ingen case for FUNCTION/FUNCTION_BLOCK tokens. Parseren skippede hele funktionsdefinitionen og stoppede ved END_FUNCTION.

**Fix:**
1. `st_parser_parse_program()` - Tilføjet while-loop der parser FUNCTION/FUNCTION_BLOCK mellem VAR og BEGIN
2. `parser_parse_function_definition()` - Tilføjet valgfri BEGIN keyword i funktionskrop
3. `parser_parse_assignment()` - Tilføjet funktionskald-som-statement support (f.eks. `COUNTER();`)

### BUG-212: Source pool null-terminering mangler i compiler path (FIXED Build #1224)

**Root cause:** `st_logic_get_source_code()` returnerer pointer direkte ind i shared source pool UDEN null-terminator. Lexeren bruger `'\0'` til at detektere EOF. Uden null-terminering læser parseren forbi kildekoden ind i tilstødende pool-data fra andre programmer.

**Symptom:** Parse/compile fejl der refererer til variabelnavne fra tidligere programmer (f.eks. "Unknown variable: END_PROGRAMafter"). Kun reproducerbart efter delete+re-upload.

**Fix:**
1. `st_logic_compile()` - Allokerer null-termineret kopi af source code før parsing
2. `st_logic_delete()` - Frigiver `func_registry` heap memory før memset (memory leak fix)

---

## Filer ændret (Build #1224)

- `src/st_parser.cpp` - FUNCTION/FUNCTION_BLOCK parsing + function call as statement
- `src/st_logic_config.cpp` - BUG-212 null-terminering + memory leak fix
- `src/http_server.cpp:463` - stack_size 8192 (Build #1196)
- `src/api_handlers.cpp` - diverse API fixes (Build #1196-1197)
- `BUGS_INDEX.md` - BUG-207 til BUG-212

---

## Environment Notes

- ESP32 IP: 10.1.1.201
- Serial port: COM8
- Auth: Basic `api_user:ChangeMe123!`
- Build tool: PlatformIO
- Test tool: curl med `-u "api_user:ChangeMe123!"`
