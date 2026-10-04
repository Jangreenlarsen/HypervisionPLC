# Arkiv

Historisk materiale fra tidligere firmwareversioner (v2–v7). **Vedligeholdes ikke** og kan være forældet: CLI-syntaks, registeradresser og API'er har ændret sig. Den aktuelle dokumentation er [brugermanualen](../docs/manual/00_INDEKS.md).

Filerne er flyttet hertil med `git mv` (2026-10-04), så deres historik kan følges med `git log --follow`.

| Mappe | Indhold |
|---|---|
| [`docs/`](docs/) | Gamle analyser, designnoter, guides og planer, fx MODBUS_REGISTER_MAP (v4.7), REST_API (v7.2), SSE_USER_GUIDE, ST_USAGE_GUIDE, TIMING_ANALYSIS, FEAT-003/FEAT-031-planer, TODO (v4), den gamle HTML-dokumentation og ældre testplaner |
| [`tests/`](tests/) | Testplaner, testresultater, testscripts og JSON-testdata fra v4–v6 |
| [`scripts/`](scripts/) | Gamle Python-hjælpescripts til tællertest og registerlæsning |

Aktuelle tests: [`test/st_host/`](../test/st_host/README.md).
