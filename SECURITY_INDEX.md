# SECURITY Index - Kendte sikkerhedsproblemer

**Formål:** Kompakt tracking af sikkerhedsanalysen udført 2026-09-02 (fuld kodebase-review i to faser: kortlægning + kildekode-verificering). Ligesom [`BUGS_INDEX.md`](BUGS_INDEX.md) skal denne fil tjekkes **før enhver kodeændring** der rører netværk/API/auth/CLI/Modbus-protokol/ST Logic — både for at undgå at genindføre et fixet problem, og for at opdage om en ændring påvirker et stadig-åbent punkt.

## Hvordan Claude skal bruge denne fil

1. **Før kodeændringer i berørte lag** (web/API, CLI/telnet, Modbus RTU/TCP-parsing, ST Logic VM/compiler, NVS/persistence): skim tabellen (~10 sekunder)
2. Tjek om dit område har åbne (❌ OPEN) eller fixede (✅ FIXED) punkter der er relevante
3. Fixede punkter: **genindfør ikke samme fejl** — kommentaren i koden (søg efter "SECURITY FIX") forklarer hvorfor
4. Åbne punkter: hvis din ændring rører samme kode, overvej at fixe det som del af opgaven, eller flag det til brugeren
5. Ved nyt fix: opdater status her OG i [`BUGS_INDEX.md`](BUGS_INDEX.md) (dette projekts fixes logges begge steder — BUGS_INDEX for kode-historik, denne fil for sikkerheds-overblik)
6. Ved nyt sikkerhedsfund (eget eller brugerens): tilføj en ny række i samme format

## Status Legend

| Symbol | Meaning |
|--------|---------|
| ✅ FIXED | Verificeret rettet i koden (se BUG-ID for detaljer) |
| ❌ OPEN | Identificeret, ikke rettet endnu |
| 🔴 KRITISK | Memory corruption, credential-lækage, eller fuld adgangskontrol-bypass |
| 🟡 HØJ | Betydelig impact, bør rettes snart |
| 🟠 MEDIUM | Mærkbar impact, godt at rette |

## Fund

| # | Titel | Status | Alvor | BUG-ID | Beskrivelse |
|---|-------|--------|-------|--------|-------------|
| 1 | `/api/system/backup` lækker credentials til read-only brugere | ✅ FIXED | 🔴 KRITISK | BUG-328 | Endpointet returnerede WiFi/telnet/HTTP/RBAC-passwords i klartekst til enhver autentificeret bruger (kun `CHECK_AUTH`, ingen rollekontrol). FIX: kræver nu skriverettighed (`CHECK_AUTH_WRITE`) — api_handlers.cpp:4058 |
| 2 | OTA firmware-upload/rollback kræver kun login, ikke skriverettighed | ❌ OPEN | 🔴 KRITISK | — | `CHECK_AUTH_OTA` bruger `http_server_check_auth()` (blot gyldig session), i modsætning til alle andre write-endpoints som bruger `CHECK_AUTH_WRITE`+RBAC. Read-only bruger kan flashe vilkårlig firmware. Ingen kryptografisk signaturverifikation. ota_handler.cpp:43-54, 96, 329 |
| 3 | HTTP-auth slået fra som fabriksdefault + svage default-credentials | ✅ FIXED | 🔴 KRITISK | BUG-328 | `http.auth_enabled = 0` var fabriksdefault — nulstillet enhed havde fuldt åbent REST API. FIX: default ændret til 1 — network_config.cpp:54. (Default-passwords `admin/modbus123` og `admin/telnet123` er stadig svage — bør ændres af bruger ved opsætning, ikke selvstændigt rettet) |
| 4 | Global buffer overflow via `MB_WRITE_HOLDINGS`/`MB_READ_HOLDINGS` (INT count uklampet) | ✅ FIXED | 🔴 KRITISK | BUG-324 | Count-arg blev kun clampet for DINT/DWORD (og ufuldstændigt selv der). Et almindeligt ST-script kunne overskrive op til 32 bytes ud over det 16-element `g_mb_multi_reg_buf`. FIX: begge bounds (1-16) håndhævet for alle typer — st_vm.cpp:1273-1284 |
| 5 | ST-parserens rekursionsdybde-guard dækkede ikke parenteser/statement-nesting | ✅ FIXED | 🔴 KRITISK | BUG-325 | BUG-157's oprindelige fix dækkede kun unær-operator-kæder. Dybt nestede parenteser eller IF/FOR/WHILE/CASE-bodies gav ubegrænset C-stack-rekursion → stack overflow ved upload af ST-program. FIX: guard udbredt til `parser_parse_expression()`, `st_parser_parse_statements()`, `st_parser_parse_statements_for_case()` — st_parser.cpp |
| 6 | `LOAD()` (ST Logic) og CLI `load registers` stack-allokerede ~30KB struct | ✅ FIXED | 🔴 KRITISK | BUG-326 | `PersistConfig temp_config;` som stack-lokal — samme struct som `config_save.cpp` bevidst heap-allokerer ("too large for ESP32 stack"). Ethvert script med `LOAD(0);` gav deterministisk stack overflow/reboot. FIX: heap-allokeret (malloc/free) — st_builtin_persist.cpp:104, cli_commands.cpp:1406+1437 |
| 7 | Ingen rate-limiting på fejlede REST API login-forsøg | ❌ OPEN | 🟡 HØJ | — | `CHECK_AUTH`-makroen tjekker auth FØR rate-limit — et forkert login returnerer 401 før rate-limit-tjek nås. Ubegrænsede Basic-Auth-gæt muligt. api_handlers.cpp:418-427 |
| 8 | Telnet brute-force lockout omgået ved disconnect/reconnect | ✅ FIXED | 🟡 HØJ | BUG-327 | `auth_attempts`/`auth_lockout_time` blev nulstillet ubetinget ved hver (re)connect — lockout omgået ved at afbryde/genoprette forbindelsen. FIX: lockout-state overlever nu reconnect, nulstilles kun ved naturlig udløb eller succesfuldt login — telnet_server.cpp |
| 9 | Telnet CLI kan DoS'es permanent af én uautentificeret forbindelse | ❌ OPEN | 🟡 HØJ | — | `TELNET_READ_TIMEOUT_MS=0` (deaktiveret) + kun ét klient-slot. En åben, aldrig-autentificeret TCP-socket til port 23 blokerer al legitim admin-adgang permanent. constants.h:421, tcp_server.cpp:459 |
| 10 | Stack buffer overflow i `set coil DYNAMIC`/`set holding-reg DYNAMIC` | ❌ OPEN | 🟡 HØJ | — | `strncpy(source_copy[32], source_str, colon-source_str)` uden loft mod `sizeof(source_copy)`. CLI-token kan være op til ~254 tegn. Kræver skriverettighed via CLI. cli_config_coils.cpp:138-140, cli_config_regs.cpp:280-282 |
| 11 | ST-scripts har uindskrænket adgang til alle Modbus-slaver på bussen | ❌ OPEN | 🟡 HØJ | — | `validate_slave_addr()` tillader enhver `slave_id` 1-247 — ingen allowlist. Et uploadet ST-program kan skrive vilkårligt til enhver anden fysisk enhed på RS485-bussen. st_builtin_modbus.cpp:46-66 |
| 12 | `/api/alarms` mangler autentificering helt | ❌ OPEN | 🟠 MEDIUM | — | Kun `CHECK_API_ENABLED` + rate-limit, intet `CHECK_AUTH`-kald. Lækker source-IP og brugernavne fra fejlede forsøg til uautentificerede klienter. api_handlers.cpp:6236-6242 |
| 13 | SSE-server understøtter aldrig TLS, selv når HTTPS er aktiveret på API'et | ❌ OPEN | 🟠 MEDIUM | — | Real-time tællere/registre/GPIO-data (og et engangs-token i URL'en) går altid i klartekst. sse_events.cpp:1159 |
| 14 | Ingen adgangskontrol på Modbus function code/register-niveau | ❌ OPEN | 🟠 MEDIUM | — | Enhver der taler på bussen (og kender/gætter slave-ID, eller bruger broadcast) kan skrive til kontrol-registre (`ST_LOGIC_CONTROL_REG_BASE` m.fl.) via standard FC06/FC10. Delvist "by design" for RTU, men bør dokumenteres/afgrænses. modbus_server.cpp |
| 15 | Cleartext credential-opbevaring i NVS | ❌ OPEN | 🟠 MEDIUM | — | `config_save.cpp` beskytter kun med CRC16-integritet, ingen kryptering af den gemte `PersistConfig`-blob. WiFi/telnet/HTTP-passwords ligger i klartekst i flash. |
| 16 | Off-by-one overflow i `set persist group ... add` | ❌ OPEN | 🟠 MEDIUM | — | `strcat(range_spec, ",")` er ubounded (i modsætning til det efterfølgende `strncat`). Kræver præcis crafting af argumentlængde (255 tegn) — lav praktisk impact. cli_commands.cpp:1253 |
| 17 | ST Logic Modbus Master builtins ignorerede "Modbus Master enabled"-flag | ✅ FIXED | 🟡 HØJ | BUG-329 | Ikke fra den oprindelige analyse, men fundet samme session: `validate_slave_addr()` tjekkede kun om async-systemet var initialiseret, ikke om Master faktisk var slået til — ST-scripts kunne queue MB_*-requests og generere phantom-statistik selv når dashboard viste Master som deaktiveret. FIX: tjekker nu `g_modbus_master_config.enabled` — st_builtin_modbus.cpp:46-58 |

## Åbne punkter — prioriteret

Rækkefølge for næste runde fixes (ikke rettet i denne omgang):
1. **#2 OTA** — skift `CHECK_AUTH_OTA` til `CHECK_AUTH_WRITE` + overvej firmware-signering
2. **#9 Telnet DoS** — aktivér idle-timeout, evt. flere klient-slots eller connect-rate-limit
3. **#10 CLI DYNAMIC-parsing** — byt `strncpy(dst, src, colon-src)` til `min(colon-src, sizeof(dst)-1)`
4. **#11 ST Modbus-scope** — indfør allowlist for hvilke slave-ID'er et ST-program må adressere
5. **#7 Rate-limiting REST API** — flyt rate-limit-tjek før auth-tjek i `CHECK_AUTH`-makroen
6. **#12, #13, #14, #15, #16** — mindre/medium punkter, tag ved lejlighed når koden alligevel røres

## Relateret

- Fuld analyse (kortlægning + verificering) blev udført 2026-09-02 og gennemgået med brugeren i chat — denne fil er ekstraktet af den samtale.
- Se [`BUGS_INDEX.md`](BUGS_INDEX.md) for BUG-324 til BUG-329 (de 6 fixede punkter, med fil:linje-detaljer og build-reference).
