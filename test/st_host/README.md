# PC-test af ST Logic-compiler og VM

Kompilerer ST-compileren og VM'en (`src/st_*.cpp`) med g++ på PC'en og kører rigtige ST-programmer igennem dem. Hardware-afhængigheder (Modbus, tællere, registre, NVS) er erstattet af stubs i `host_stubs.cpp` og `stubs/`.

```bash
bash test/st_host/run.sh
```

Kræver g++ (på Windows fx MinGW). Output: én `PASS`/`FAIL`-linje pr. test og `ALLE TESTS OK` til sidst. Byggefiler havner i `test/st_host/build/` (ignoreret af git).

**Dækning:** matematik, typekonvertering, bit-operationer, TON/TOF/TP, CTU/CTD/CTUD, R_TRIG/F_TRIG, SR/RS, SCALE/HYSTERESIS/BLINK/FILTER og strengfunktioner — inkl. automatisk argumentkonvertering (fx `SQRT(16)` med en INT). Modbus- (`MB_*`/`MBX_*`) og tællerfunktioner (`CNT_*`) er stubbet og testes ikke her.

**Hvornår:** kør testen efter ændringer i `st_compiler.cpp`, `st_vm.cpp`, `st_builtins*.cpp` eller `st_stateful.cpp`. Tilføj en test i `st_test.cpp` for hver ny indbygget funktion. Bruger en ny funktion en ukendt hardware-funktion, skal der en stub i `host_stubs.cpp`.

Opstod under BUG-433 — se `BUGS_INDEX.md`.
