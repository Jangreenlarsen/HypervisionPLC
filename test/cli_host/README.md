# PC-test af CLI-parseren

Kompilerer den rigtige CLI-parser (`src/cli_parser.cpp`, `src/cli_commands.cpp`, `src/counter_config.cpp` og `src/timer_config.cpp`) med g++ på PC'en. Derefter kører den rigtige kommandolinjer gennem `cli_parser_execute()`.

```bash
bash test/cli_host/run.sh
```

Kræver g++ og python (på Windows fx MinGW). Output: én `PASS`/`FAIL`-linje pr. test og `ALLE TESTS OK` til sidst. Byggefiler havner i `test/cli_host/build/` (ignoreret af git).

**Sådan virker det**
- `hand_stubs.cpp` fanger CLI-output (`debug_*`) og de konfigurationer, parseren sender videre (`counter_engine_configure`, `timer_engine_configure`). Testen kan derfor tjekke de parsede værdier.
- Alle øvrige eksterne funktioner (~200) stubbes automatisk af `gen_stubs.py`. Scriptet linker først uden stubs og læser linkerens liste over manglende symboler. Hver stub registrerer kaldet og returnerer 0, så testen kan tjekke *hvilken* funktion en kommando nåede frem til, fx `show ota` → `cli_cmd_show_ota`.
- En funktion, der returnerer en struct, eller hvor 0 betyder noget forkert for parseren, skal have en håndskrevet stub, fx `register_allocator_check` (0 = optaget).
- `stubs/` indeholder minimale ESP-IDF-headere ud over dem i `test/st_host/stubs/`.

**Dækning:** show-kommandoer og aliaser, `set counter` i både `show config`-formatet og med de nye nøglenavne (BUG-446), `set timer`, afvisning af manuelle registre, "mente du …?"-forslag (FEAT-437) og at hvert foreslået ord accepteres af parseren.

**Hvornår:** kør testen efter ændringer i `cli_parser.cpp`, `cli_commands.cpp` eller tæller-/timer-konfigurationen. Tilføj en test for hver ny kommando, og tilføj ordet til `CLI_WORDS_*` i `cli_parser.cpp`.

Opstod under FEAT-436 — se `BUGS_INDEX.md`.
