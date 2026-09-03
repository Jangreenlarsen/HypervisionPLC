# Appendiks B: REST API-reference

[← Appendiks A: CLI-reference](A_CLI_Kommando_Reference.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks C: Ordliste →](C_Ordliste.md)

---

> Denne reference er udtrukket direkte fra kildekoden (`src/http_server.cpp`, `src/api_handlers.cpp`, `src/ota_handler.cpp`) — alle **92** registrerede `httpd_uri_t`-handlers er talt og dokumenteret nedenfor (verificeret via `grep -c "httpd_register_uri_handler" src/http_server.cpp`), plus SSE-serveren som kører uden for hoved-httpd'en. Se [kapitel 7](07_REST_API.md) for grundlæggende brug (auth, rate limiting, eksempler).

## B.1 Generelt

**Base-URL:** `http://<ip>:<port>/api/...` (default port 80, eller 443 hvis `set http tls on`). SSE-strømmen (`/api/events`) kører på en **separat** raw-socket-server på en dedikeret port (default: HTTP-port + 1, konfigurerbar via `set sse port`) — den er ikke registreret via ESP-IDF's `httpd` og har derfor ingen `httpd_uri_t`.

**Auth-niveauer brugt i tabellerne** (defineret som makroer i `api_handlers.cpp`):
- **Ingen** — kun `CHECK_API_ENABLED` (kræver `set http api on`), ingen brugerauth
- **CHECK_AUTH** — kræver gyldig auth (Basic Auth-header eller RBAC-bruger), enhver rolle/privilegie er nok (read-only ok)
- **CHECK_AUTH_WRITE** — kræver gyldig auth **og** write-privilegie (RBAC `privilege=write` eller `read/write`, eller legacy virtual-admin)
- **CHECK_AUTH_ROLE(rolle)** — kræver gyldig auth **og** en specifik RBAC-rolle (bruges kun af `/api/cli`, som derudover selv tjekker write-privilegie)

Alle svar er `application/json`. Fejl som `{"error":"...","status":N}` med matchende HTTP-statuskode (400/401/403/404/409/429/500). Alle responses sætter `Access-Control-Allow-Origin: *` og `Cache-Control: no-store`. 401 sætter `WWW-Authenticate: Basic`.

**Rate limiting:** Token-bucket pr. klient-IP (max 8 IP'er trackes), 30 requests burst, genopfyldning 10/sek — giver `429 Too Many Requests`. Se [§7.3](07_REST_API.md#73-rate-limiting).

**Wildcard suffix-routing (vigtigt arkitekturvalg):** ESP-IDF's `httpd_uri_match_wildcard` matcher kun `*` i **slutningen** af en URI — et mønster som `/api/logic/*/source` findes ikke og ville aldrig matche. Løsningen i denne kodebase: der registreres én bred wildcard-URI pr. ressource-type (fx `/api/logic/*`, `/api/counters/*`, `/api/gpio/*`, `/api/modbus/*`, `/api/wifi/*`, `/api/persist/groups/*`), og selve C-handler-funktionen undersøger derefter `req->uri`-strengens **suffix** manuelt og delegerer internt til den rette under-handler. Fx håndterer `api_handler_counter_single()` alene: almindelig `GET /api/counters/{id}`, samt (via suffix-check) `POST .../reset`, `.../start`, `.../stop`, `.../control`, og almindelig `POST .../{id}` (config) — alt sammen bag ét `httpd_uri_t`. Rækkefølgen af registreringer har betydning: mere specifikke/eksakte URI'er (fx `/api/gpio/2/heartbeat`) registreres **før** de bredere wildcards, så de ikke skygges — se [kapitel 13](13_Fejlfinding.md) for et konkret eksempel på hvad der sker når dette princip brydes.

**API-versionering (`/api/v1/*`):** spejler **næsten** hele det uversionerede API via en intern rewrite+routingtabel — undtagelsen er `/api/alarms` og `/api/alarms/ack`, som mangler i versioneringstabellen og derfor giver 404 under `/api/v1/`. Brug det uversionerede `/api/alarms` indtil videre.

Nedenfor markeres suffix-routede under-endpoints med *(via wildcard-suffix)*.

## B.2 System / Status

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api` , `/api/` | CHECK_AUTH | Endpoint-discovery (statisk, **forældet** — mangler flere nyere endpoints, brug denne reference i stedet) |
| GET | `/api/status` | CHECK_AUTH | version, build, uptime_ms, heap_free, wifi_connected, ip, modbus_slave_id, https |
| GET | `/api/version` | CHECK_AUTH | api_version, firmware_version, build, min_supported_api, versioned_prefix |
| GET | `/api/system/watchdog` | CHECK_AUTH | enabled, timeout_ms, reboot_count, last_reset_reason, last_error, heap-info |
| POST | `/api/system/reboot` | CHECK_AUTH_WRITE | Genstarter ESP32 (1 sek. efter svar er sendt) |
| POST | `/api/system/save` | CHECK_AUTH_WRITE | Gemmer hele config til NVS (inkl. CRC16) |
| POST | `/api/system/load` | CHECK_AUTH_WRITE | Genindlæser + anvender config fra NVS |
| POST | `/api/system/defaults` | CHECK_AUTH_WRITE | Nulstiller til fabriksdefaults (kun i RAM, ikke gemt) |
| GET | `/api/user/me` | *Ingen* (returnerer `authenticated:false` hvis ikke logget ind) | Aktuel brugers auth-status: username, roles, privilege, mode (`legacy`/`rbac`) |
| POST | `/api/cli` | CHECK_AUTH_ROLE(`cli`) + write-check | Body: `{"command":"<cli-kommando>"}`. Kører kommandoen via samme dispatcher som Serial/Telnet, returnerer `{"output":"..."}`. Blokerer `reboot`/`defaults`. Maks 256 tegn kommando. |
| GET | `/api/hostname` | CHECK_AUTH | `{"hostname":"..."}` |
| POST | `/api/hostname` | CHECK_AUTH_WRITE | Body: `{"hostname":"..."}` (1-31 tegn) |

## B.3 Konfiguration

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/config` | CHECK_AUTH | Stort read-only snapshot: system, modbus_mode, modbus_slave, modbus_master, analog_outputs, network, telnet, http, counters[], timers[], gpio[], st_logic (m. programs[]), modules, persistence |
| POST | `/api/http` | CHECK_AUTH_WRITE | Body-felter: `enabled`, `port`, `auth_enabled`, `api_enabled`, `tls_enabled`, `username`, `password`, `priority`(`LOW`/`NORMAL`/`HIGH`). Port/TLS kræver reboot. |
| GET | `/api/modules` | CHECK_AUTH | `{"counters":bool,"timers":bool,"st_logic":bool}` (modul-flag) |
| POST | `/api/modules` | CHECK_AUTH_WRITE | Samme felter — slå moduler til/fra |
| GET | `/api/dashboard/layout` | *Ingen* | `card_order`, `card_tabs`, `card_hidden` (dashboard UI-præference) |
| POST | `/api/dashboard/layout` | *Ingen* | Samme felter — bevidst uden auth (ikke-følsom UI-indstilling) |
| GET | `/api/debug` | CHECK_AUTH | Debug-flags: `all`, `config_save`, `config_load`, `wifi_connect`, `network_validate`, `http_server`, `http_api` |
| POST | `/api/debug` | CHECK_AUTH_WRITE | Samme felter (bool) |

## B.4 Modbus Master

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/modbus/master` *(via `/api/modbus/*` wildcard)* | CHECK_AUTH | config (baudrate, parity, stop_bits, timeout_ms, inter_frame_delay_ms, max_requests_per_cycle, cache_ttl_ms) + stats (requests/success/timeouts/crc/exceptions) |
| POST | `/api/modbus/master` | CHECK_AUTH_WRITE | Body: `enabled`, `baudrate`, `parity`(`even`/`odd`), `stop_bits`, `timeout_ms`, `inter_frame_delay_ms`, `max_requests_per_cycle`, `cache_ttl_ms` |
| POST | `/api/modbus/master/reset-stats` *(suffix)* | CHECK_AUTH_WRITE | Nulstiller al master-statistik |
| POST | `/api/modbus/master/rw` *(suffix)* | CHECK_AUTH_WRITE | Async read/write via cache+kø. Body: `{"op":"read\|write","type":"holding\|coil\|input\|input-reg","slave":1-247,"addr":N,"value":N}`. Read: returnerer cache-hit (`status:"ok"`) eller `status:"pending"` (kø'et). Write: altid `status:"queued"`. Markeres i aktivitetsloggen som kilde `dashboard`. |

## B.5 Modbus Slave

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/modbus/slave` *(suffix)* | CHECK_AUTH | config (slave_id, baudrate, parity, stop_bits, inter_frame_delay_ms) + stats |
| POST | `/api/modbus/slave` | CHECK_AUTH_WRITE | Body: `slave_id`(1-247), `baudrate`, `parity`, `stop_bits`, `inter_frame_delay_ms` |

## B.6 Modbus Aktivitetslog (FEAT-149, RAM-only)

*Bemærk: disse to routes er ikke selvstændige `httpd_uri_t` — de matches af `/api/modbus/*`-wildcarden, men delegeres helt i toppen af `api_handler_modbus_get`/`_post` (før disse handlers egen auth-logik), fordi wildcarden ellers ville "skygge" dem. Se [kapitel 13](13_Fejlfinding.md) for baggrundshistorien.*

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/modbus/activity` | *Ingen* (kun `CHECK_API_ENABLED` + rate limit) | Array af de seneste (op til 40) wire-level Master+Slave-transaktioner: `timestamp_ms`, `role`(`master`/`slave`), `source`(`st_logic`/`cli`/`dashboard`/`external`), `slave_id`, `fc`, `address`, `count`, `value`, `error`, `success` |
| POST | `/api/modbus/activity/clear` | CHECK_AUTH_WRITE | Tømmer aktivitetsloggen |

## B.7 Registre / Coils / Discrete Inputs

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/registers/hr/{addr}` *(wildcard)* | CHECK_AUTH | `{"address":N,"value":N}` |
| POST | `/api/registers/hr/{addr}` | CHECK_AUTH_WRITE | Body: `{"value":N,"type":"uint\|int\|dint\|dword\|real"}` (type default `uint`). dint/dword/real skriver 2 registre (high word først). |
| GET | `/api/registers/ir/{addr}` | CHECK_AUTH | `{"address":N,"value":N}` — kun læsning (FC04) |
| GET | `/api/registers/coils/{addr}` | CHECK_AUTH | `{"address":N,"value":bool}` |
| POST | `/api/registers/coils/{addr}` | CHECK_AUTH_WRITE | Body: `{"value":bool\|int}` |
| GET | `/api/registers/di/{addr}` | CHECK_AUTH | `{"address":N,"value":bool}` |
| GET | `/api/registers/hr?start=&count=` | CHECK_AUTH | Bulk-læsning, count 1–200. `{"start":N,"count":N,"registers":[{"addr":N,"value":N},...]}` |
| POST | `/api/registers/hr/bulk` | CHECK_AUTH_WRITE | Body: `{"writes":[{"addr":N,"value":N},...]}`. Svar: `{"status":200,"written":N}` |
| GET | `/api/registers/ir?start=&count=` | CHECK_AUTH | Bulk-læsning IR, count 1–200 |
| GET | `/api/registers/coils?start=&count=` | CHECK_AUTH | Bulk-læsning coils, count 1–256 |
| POST | `/api/registers/coils/bulk` | CHECK_AUTH_WRITE | Body: `{"writes":[{"addr":N,"value":bool},...]}` |
| GET | `/api/registers/di?start=&count=` | CHECK_AUTH | Bulk-læsning discrete inputs, count 1–256 |

Adresseområder: HR/IR 0–255, coils/DI 0–255.

## B.8 GPIO

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/gpio` | CHECK_AUTH | Array af alle konfigurerede GPIO-mappings: pin, direction, value, coil/register |
| GET | `/api/gpio/{pin}` *(wildcard)* | CHECK_AUTH | Pin-værdi + evt. mapping-info. Gyldig pin: 0–39, 101–108, 201–208 |
| POST | `/api/gpio/{pin}` | CHECK_AUTH_WRITE | Body: `{"value":bool\|int}`. Skriver kun hvis pin er mappet som output. |
| POST | `/api/gpio/{pin}/config` *(suffix)* | CHECK_AUTH_WRITE | Opret/opdater mapping. Body: `{"direction":"input\|output","register":N}` (input) eller `{"direction":"output","coil":N}` |
| DELETE | `/api/gpio/{pin}` | CHECK_AUTH_WRITE | Fjern GPIO-mapping |
| GET / POST | `/api/gpio/2/heartbeat` *(egen eksakt registrering, registreret FØR `/api/gpio/*`)* | GET: CHECK_AUTH · POST: CHECK_AUTH (+ write-check) | GET: `{"enabled":bool,"gpio2_user_mode":bool}`. POST body: `{"enabled":bool}` — styr heartbeat-LED vs. brugerkode på GPIO2 |

## B.9 Counters

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/counters` | CHECK_AUTH | Array: id, enabled, mode(`SW`/`SW_ISR`/`HW_PCNT`), value |
| GET | `/api/counters/{id}` *(wildcard, 1–4)* | CHECK_AUTH | id, enabled, mode, value, raw, frequency, running, overflow, compare_triggered |
| POST | `/api/counters/{id}` *(suffix-routing, ingen suffix)* | CHECK_AUTH_WRITE | Fuld config-body: `enabled`, `hw_mode`, `edge`, `direction`, `prescaler`, `bit_width`, `scale_factor`, `value_reg`, `raw_reg`, `freq_reg`, `ctrl_reg`, `start_value`, `hw_gpio`, `interrupt_pin`, `input_dis`, `debounce_ms`, `compare_enabled`, `compare_value`, `compare_mode` |
| POST | `/api/counters/{id}/reset` *(suffix)* | CHECK_AUTH_WRITE | Nulstil til start-value |
| POST | `/api/counters/{id}/start` *(suffix)* | CHECK_AUTH_WRITE | Sæt start-bit (bit1) i ctrl-reg |
| POST | `/api/counters/{id}/stop` *(suffix)* | CHECK_AUTH_WRITE | Sæt stop-bit (bit2) i ctrl-reg |
| POST | `/api/counters/{id}/control` *(suffix)* | CHECK_AUTH_WRITE | Body: `{"running":bool,"reset":bool}` |
| DELETE | `/api/counters/{id}` | CHECK_AUTH_WRITE | Nulstiller til defaults (disabled) |

## B.10 Timers

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/timers` | CHECK_AUTH | Array: id, enabled, mode, output (coil-state) |
| GET | `/api/timers/{id}` *(wildcard, 1–4)* | CHECK_AUTH | Fuld config + mode-specifikke felter (phase1-3_ms, pulse_ms, on/off_ms, input_dis, delay_ms) |
| POST | `/api/timers/{id}` *(kun eksakt match — andre suffixer afvises med 404)* | CHECK_AUTH_WRITE | Body: `enabled`, `mode`(`ONESHOT`/`MONOSTABLE`/`ASTABLE`/`INPUT_TRIGGERED`), `output_coil`, `ctrl_reg`, samt mode-specifikke felter (`on_ms`/`off_ms` accepteres som alias for `on_duration_ms`/`off_duration_ms`) |
| DELETE | `/api/timers/{id}` | CHECK_AUTH_WRITE | Nulstil til disabled |

## B.11 ST Logic

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/logic` | CHECK_AUTH | Global status (enabled, interval, total_cycles, heap/pool-resources) + programs[] (id,name,enabled,compiled,source_size,execution_count,error_count) |
| GET | `/api/logic/{id}` *(wildcard, 1–4)* | CHECK_AUTH | Fuld program-info + variables[] (index,name,type,value) hvis kompileret |
| GET | `/api/logic/{id}/source` *(suffix)* | CHECK_AUTH | `{"id","name","source","size"}` |
| POST | `/api/logic/{id}/source` *(suffix)* | CHECK_AUTH_WRITE | Body: `{"source":"<ST-kode>"}` (maks 8KB). Uploader + kompilerer. Svar inkl. `compiled`, `instr_count`, evt. `compile_error` |
| POST | `/api/logic/{id}/enable` *(suffix)* | CHECK_AUTH_WRITE | Aktivér program |
| POST | `/api/logic/{id}/disable` *(suffix)* | CHECK_AUTH_WRITE | Deaktivér program |
| POST | `/api/logic/{id}/reinit` *(suffix)* | CHECK_AUTH_WRITE | Cold restart (nulstil variabler) |
| DELETE | `/api/logic/{id}` | CHECK_AUTH_WRITE | Slet program |
| GET | `/api/logic/{id}/stats` *(suffix)* | CHECK_AUTH | execution_count, error_count, min/max/avg/last_execution_us, overrun_count |
| POST | `/api/logic/{id}/bind` *(suffix)* | CHECK_AUTH_WRITE | Body: `{"variable":"navn","binding":"reg:N\|coil:N\|input:N","direction":"input\|output\|both"}` (direction valgfri) |
| GET | `/api/logic/{id}/debug/state` *(suffix)* | **CHECK_AUTH** (ikke write!) | mode(`off`/`paused`/`step`/`run`), breakpoints[], snapshot (pc,sp,halted,error,variables[]) |
| POST | `/api/logic/{id}/debug/pause\|continue\|step\|stop` *(suffix)* | **CHECK_AUTH** (ikke write!) | Styr debugger — bemærk: kun almindelig auth kræves, ikke write-privilegie |
| POST | `/api/logic/{id}/debug/breakpoint` *(suffix)* | CHECK_AUTH | Body: `{"pc":N}`. Maks 8 breakpoints. |
| DELETE | `/api/logic/{id}/debug/breakpoint` *(suffix)* | CHECK_AUTH | Body: `{"pc":N}` (fjern én) eller uden body (ryd alle) |
| POST | `/api/logic/settings` | CHECK_AUTH_WRITE | Body: `{"interval_ms":1-60000}` — globalt exec-interval |
| GET | `/api/bindings` | CHECK_AUTH | Alle variabel↔register-bindinger: index, program, var_index, name, type, direction, register_type(`HR`/`DI`/`Coil`), register_addr |
| DELETE | `/api/bindings/{index}` *(wildcard)* | CHECK_AUTH_WRITE | Fjern én binding (global `var_maps`-indeks fra GET-listen) |

## B.12 Netværk (WiFi / Ethernet / Telnet / NTP)

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/wifi` | CHECK_AUTH | config (ssid,dhcp,power_save,static_*) + runtime (connected,ip,gateway,netmask,dns,rssi,uptime_ms,ssid,state) |
| POST | `/api/wifi` *(eksakt)* | CHECK_AUTH_WRITE | Body: `ssid`,`password`,`dhcp`,`enabled`,`power_save`,`static_ip`,`static_gateway`,`static_netmask`,`static_dns` |
| POST | `/api/wifi/connect` *(suffix)* | CHECK_AUTH_WRITE | Starter WiFi-forbindelse (async) |
| POST | `/api/wifi/disconnect` *(suffix)* | CHECK_AUTH_WRITE | Afbryder WiFi |
| GET | `/api/ethernet` | CHECK_AUTH | config (enabled,dhcp,static_*,hostname) + runtime (connected,ip,gateway,netmask,dns,speed_mbps,full_duplex,mac,state) |
| POST | `/api/ethernet` | CHECK_AUTH_WRITE | Body: `enabled`,`dhcp`,`static_ip`,`static_gateway`,`static_netmask`,`static_dns`,`hostname` |
| GET | `/api/telnet` | CHECK_AUTH | `enabled`,`port`,`username`,`auth_required` |
| POST | `/api/telnet` | CHECK_AUTH_WRITE | Body: `enabled`,`port`,`username`,`password` |
| GET | `/api/ntp` | CHECK_AUTH | `enabled`,`server`,`timezone`,`sync_interval_min`,`synced`,`sync_count`,`error_count`,(hvis synced) `local_time`,`iso_time`,`epoch`,`last_sync_age_ms` |
| POST | `/api/ntp` | CHECK_AUTH_WRITE | Body: `enabled`,`server`,`timezone`,`sync_interval_min`(1-1440). Anvendes øjeblikkeligt. |

## B.13 Sikkerhed / RBAC / Brugere

Der findes **intet** dedikeret `/api/users`-CRUD-endpoint. Brugerhåndtering sker udelukkende via CLI (`set user`/`delete user`, se [Appendiks A](A_CLI_Kommando_Reference.md#a4-set-x--konfigurationskommandoer)) eller indirekte via `/api/system/backup`+`/api/system/restore` (som inkluderer RBAC-brugere i klartekst — se §B.14).

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/user/me` | *Ingen* | Se §B.2 — viser roller/privilegie for den kaldende bruger |

Auth-model: HTTP Basic Auth-header, matchet mod enten RBAC-brugerdatabasen (op til 8 brugere, roller `api`/`cli`/`editor`/`monitor`, privilegie `read`/`write`/`read/write`) eller — hvis RBAC er deaktiveret — det gamle single-user `network.http.username`/`password`-par (svarer til en "virtual admin", uid=99, fuld adgang).

## B.14 Backup / Restore

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/system/backup` | **CHECK_AUTH_WRITE** (bevidst skærpet fra CHECK_AUTH — indeholder WiFi/Telnet/HTTP/RBAC-adgangskoder i klartekst) | Komplet JSON-dump af hele konfigurationen: metadata, modbus, network/telnet/http/sse/ntp (inkl. passwords!), counters[], timers[], static/dynamic regs+coils, var_maps[], persist_regs (m. gruppeindhold), logic_programs[] (m. source code), rbac.users[] (m. passwords). Sender `Content-Disposition: attachment; filename="backup.json"`. |
| POST | `/api/system/restore` | CHECK_AUTH_WRITE | Body: samme JSON-struktur som backup-outputtet. Erstatter hele konfigurationen, gemmer til NVS og anvender den. Kræver reboot for fuld effekt (advarsel i svaret). |

Se [kapitel 11](11_Backup_Restore_og_Firmware.md) for brugsanvisning og opbevaringsanbefalinger.

## B.15 OTA Firmware Update (FEAT-031)

*Bruger sin egen `CHECK_AUTH_OTA`-makro, som **svarer til `CHECK_AUTH`** (kræver blot en autentificeret bruger — ikke specifikt write-privilegie, på trods af at operationen er destruktiv). Se [`../../SECURITY_INDEX.md`](../../SECURITY_INDEX.md) og [kapitel 11](11_Backup_Restore_og_Firmware.md#113-ota-firmwareopdatering) for status.*

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| POST | `/api/system/ota` | CHECK_AUTH_OTA | Chunked upload af `.bin`-firmware (streames direkte til flash, 4KB chunks). Validerer ESP32 magic byte (0xE9). Maks størrelse ~1,8125 MB. Ved succes: reboot efter 2000ms. Body: rå binærdata (ikke JSON). |
| GET | `/api/system/ota/status` | CHECK_AUTH_OTA | `state`(`idle`/`receiving`/`verifying`/`done`/`error`), `received`,`total`,`percent`,`error`,`new_version`,`current_version`,`running_partition`,`boot_partition`,`rollback_possible` |
| POST | `/api/system/ota/rollback` | CHECK_AUTH_OTA | Skifter boot-partition til den anden OTA-slot og genstarter (2000ms delay) |

## B.16 Alarmer (FEAT-085)

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/alarms` | *Ingen* (kun `CHECK_API_ENABLED` + rate limit) | Ringbuffer med op til 32 alarmer: `timestamp_ms`,`message`,`severity`(0=info,1=warning,2=critical),`acknowledged`,`uptime`,(hvis NTP synced) `epoch`,`time`, evt. `source_ip`/`username` (for auth-fejl). Genereres automatisk hvert 3. sek. ved: lav heap, stigende CRC-fejl (slave), stigende timeouts (master), stigende auth-failures, write-privilegie nægtet, SSE-kø fuld, ST Logic overrun-rate >5%. |
| POST | `/api/alarms/ack` | CHECK_AUTH_WRITE | Kvitterer (acknowledged=true) alle alarmer |

## B.17 Persistence Groups (FEAT-022)

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/persist/groups` | CHECK_AUTH | `enabled`,`group_count`,`max_groups`(8),`auto_load_enabled`,`groups`[{id,name,reg_count,max_regs(16),last_save_ms}] |
| GET | `/api/persist/groups/{navn}` *(wildcard)* | CHECK_AUTH | Gruppedetaljer inkl. `registers`[{addr,saved_value,current_value}] |
| POST | `/api/persist/groups/{navn}` | CHECK_AUTH_WRITE | Opretter gruppen hvis den ikke findes. Body: `{"registers":[N,...]}` (tilføj) og/eller `{"remove":[N,...]}` (fjern). Aktiverer persistence hvis slukket. |
| DELETE | `/api/persist/groups/{navn}` | CHECK_AUTH_WRITE | Sletter gruppen |
| POST | `/api/persist/save` | CHECK_AUTH_WRITE | Uden body: gem alle grupper. Body: `{"group":"navn"}` eller `{"group_id":N}` for én gruppe. |
| POST | `/api/persist/restore` | CHECK_AUTH_WRITE | Uden body: gendan alle grupper. Body: `{"group":"navn"}` eller `{"group_id":N}` |

## B.18 Metrics / SSE

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| GET | `/api/metrics` | *Ingen* (kun `CHECK_API_ENABLED` + rate limit) | Prometheus text-exposition-format. Dækker: system, HTTP-stats, Modbus slave/master (config+stats+cache+backoff pr. slave), SSE, WiFi/Ethernet/Telnet, counters, timers, ST Logic (globalt + pr. program), GPIO, alle non-zero HR/IR-registre, persistence-grupper, watchdog, FreeRTOS task stack, firmware-info, NTP, alarm-log-tælling |
| GET | `/api/events/status` | Svarer til CHECK_AUTH | `sse_enabled`,`sse_port`,`max_clients`,`active_clients`,`check_interval_ms`,`heartbeat_ms`,`topics`,`endpoint`, samt et **kortlivet `sse_token`** (til cross-port-auth af EventSource) |
| GET | `/api/events/clients` | Svarer til CHECK_AUTH | `active_clients`, `clients`[{slot,ip,username,topics,uptime_s}] |
| POST | `/api/events/disconnect` | Svarer til CHECK_AUTH (**ikke** write-gated) | Body: `{"slot":N}` (enkelt) eller `{"slot":-1}` (alle) |
| GET | `/api/events?subscribe=<topics>&token=<token>` | **Kører på dedikeret SSE-port**, ikke hoved-httpd'en. Auth: query-param `token` ELLER Basic Auth-header. Kræver RBAC-rolle `monitor` **eller** `api` | Server-Sent-Events-stream. Topics: `counters`,`timers`,`registers`,`system` (kommasepareret, eller alle via ingen/`all`). |

## B.19 CORS

| Metode | URI | Auth | Beskrivelse |
|---|---|---|---|
| OPTIONS | `/api` | Ingen | Preflight — `204 No Content` |
| OPTIONS | `/api/*` (alle øvrige API-stier) | Ingen | Preflight — `Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS`, `Access-Control-Allow-Headers: Authorization, Content-Type`, `Max-Age: 86400` |

## B.20 Web-UI-sider (statisk HTML, ikke JSON-API)

Disse serverer statisk HTML/JS **uden nogen server-side auth-kontrol** — siderne selv laver klient-side, autentificerede kald til `/api/*` (browseren viser evt. Basic-Auth-prompt for de kald).

| Metode | URI | Beskrivelse |
|---|---|---|
| GET | `/` | Web-dashboard (forside) |
| GET | `/editor` | ST Logic-editor (browser-baseret) |
| GET | `/system` | Web-baseret systemadministration |
| GET | `/ota` | OTA firmware-upload-side |
| GET | `/cli` | Standalone Web-CLI-side (bruger `/api/cli`) |

## B.21 Opsummering / verifikation

- `grep -c "httpd_register_uri_handler" src/http_server.cpp` → **92** — alle 92 er dokumenteret ovenfor (enten som selvstændig række, eller som *suffix*-delegeret under-endpoint med reference til deres fælles wildcard-registrering).
- Dertil kommer **1** endpoint der bevidst ikke er en del af hoved-`httpd`'en: `GET /api/events` (dedikeret SSE-portserver).
- `ota_handler.cpp` bidrager 3 af de 92 (registreres fra `http_server.cpp`, men implementeres i egen fil).

---

[← Appendiks A: CLI-reference](A_CLI_Kommando_Reference.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks C: Ordliste →](C_Ordliste.md)
