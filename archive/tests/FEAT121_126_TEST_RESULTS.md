# FEAT-121-126 Test Results

**Firmware:** v7.9.4.0.1882
**Date:** 2026-04-09
**ESP32:** 10.1.1.30
**Result:** 23/23 PASS

## Test Summary

| Group | Feature | Tests | Result |
|-------|---------|-------|--------|
| 1 | TIME Type (FEAT-121) | 5/5 | PASS |
| 2 | TON Timer On Delay (FEAT-122) | 3/3 | PASS |
| 3 | TOF Timer Off Delay (FEAT-123) | 2/2 | PASS |
| 4 | CTU Count Up (FEAT-124) | 1/1 | PASS |
| 5 | CTD Count Down (FEAT-125) | 1/1 | PASS |
| 6 | CTUD Count Up/Down (FEAT-126) | 1/1 | PASS |
| 7 | Backward Compatibility | 6/6 | PASS |
| 8 | Error Cases | 2/2 | PASS |
| 9 | DINT Range + Mixed | 2/2 | PASS |

## Detailed Results

### GROUP 1: TIME Type (FEAT-121)
- 1.1 TIME declaration T#5s — PASS
- 1.2 TIME literals ms/s/m/h — PASS
- 1.3 TIME arithmetic 5s+3s=8000 — PASS
- 1.4 TIME comparison 5s>3s — PASS
- 1.5 TIME 60s=60000 (PUSH_DWORD, >32767) — PASS

### GROUP 2: TON Timer On Delay (FEAT-122)
- 2.1 TON named-param syntax compiles — PASS
- 2.2 TON Q=TRUE after PT elapsed (start=TRUE, PT=2s) — PASS
- 2.3 TON ET >= 2000ms — PASS

### GROUP 3: TOF Timer Off Delay (FEAT-123)
- 3.1 TOF named-param syntax compiles — PASS
- 3.2 TOF Q=TRUE while IN=TRUE — PASS

### GROUP 4: CTU Count Up (FEAT-124)
- 4.1 CTU named-param with Q=> and CV=> compiles — PASS

### GROUP 5: CTD Count Down (FEAT-125)
- 5.1 CTD named-param compiles — PASS

### GROUP 6: CTUD Count Up/Down (FEAT-126)
- 6.1 CTUD 3 output bindings (QU=>, QD=>, CV=>) compiles — PASS

### GROUP 7: Backward Compatibility
- 7.1 TON positional syntax — PASS
- 7.2 CTU positional syntax — PASS
- 7.3 TIME literal in positional (T#5s) — PASS
- 7.4 TOF positional syntax — PASS
- 7.5 CTD positional syntax — PASS
- 7.6 CTUD positional syntax — PASS

### GROUP 8: Error Cases
- 8.1 Unknown param BOGUS -> compile error — PASS
- 8.2 Output arrow on input param (IN => y) -> compile error — PASS

### GROUP 9: DINT Range + Mixed
- 9.1 DINT 100000 (auto-promote to 32-bit) — PASS
- 9.2 TON + output binding + expression chain — PASS

## Bugs Found & Fixed During Testing

1. **Function call as statement not compiled** — `ST_AST_FUNCTION_CALL` missing from statement compiler switch, fell to `default: break`. Fixed by adding case with compile_expr + POP.
2. **TIME variable displayed as int16** — API handler missing `ST_TYPE_TIME` case, fell to `default: val.int_val`. Fixed by adding TIME case using `dint_val`.
3. **DINT literal overflow crash** — Parser rejected values >32767 as "INT overflow". DINT literals >32767 caused ESP32 crash. Fixed by auto-promoting to DINT type when value exceeds INT16 range.
4. **DINT literal used PUSH_INT** — Only supports 16-bit range. Changed to PUSH_DWORD for full 32-bit range.
