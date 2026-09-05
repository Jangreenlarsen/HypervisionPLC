# Appendiks A: CLI-kommandoreference

[← 13. Fejlfinding](13_Fejlfinding.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks B: REST API-reference →](B_REST_API_Reference.md)

---

> Denne reference er udtrukket direkte fra kildekoden (`src/cli_parser.cpp`, `src/cli_commands*.cpp`, `src/cli_show.cpp`) og er derfor autoritativ i forhold til hvad systemet faktisk understøtter — ikke kun hvad den indbyggede `help`-tekst siger (se §A.8 for kendte afvigelser mellem de to). Se [kapitel 5](05_CLI_Konsol.md) for det grundlæggende kommandomønster.

## A.1 Syntaks og generelle regler

CLI'en tokeniserer input på whitespace, understøtter citerede strenge (`"..."`), og normaliserer første token(er) case-insensitivt via en alias-tabel (`normalize_alias()` i `cli_parser.cpp`). Prompt er `> ` (eller `>>> ` i ST Logic upload-mode). Kommandoer virker identisk over Serial, Telnet og Web-CLI (`/api/cli`).

**Vigtige aliaser (verber):** `sh`/`s`→`show`, `conf`→`set`, `rst`→`reset`, `clr`→`clear`, `sv`→`save`, `ld`→`load`, `def`→`defaults`, `restart`→`reboot`, `quit`/`q`→`exit`, `con`→`connect`, `disc`/`dc`→`disconnect`, `h`/`?`→`help`, `rd`/`r`→`read`, `wr`/`w`→`write`, `cmds`→`commands`.

**Vigtige alias-navneord:** `cnt`/`cntr`→`counter`, `cnts`→`counters`, `tmr`→`timer`, `tmrs`→`timers`, `log`→`logic`, `cfg`→`config`, `regs`→`registers`, `ins`→`inputs`, `ver`/`v`→`version`, `dbg`→`debug`, `wdg`→`watchdog`, `verb`→`verbose`, `eth`→`ethernet`, `mb-master`→`modbus-master`, `mb-slave`→`modbus-slave`, `hreg`/`h-reg`/`holding-reg`/`holding_reg`→`h-reg`, `usr`→`user`, `pw`/`pass`→`password`, `priv`/`pri`→`privilege`, `bp`→`break`, `cont`→`continue`, `ln`→`line`.

`?` eller `help` efter enhver top-kommando (fx `set ?`, `show logic ?`, `set modbus-master ?`) viser indbygget hjælp.

## A.2 System- og sessionskommandoer (standalone)

| Kommando | Parametre | Beskrivelse | Eksempel |
|---|---|---|---|
| `help` / `?` / `h` | — | Fuld, detaljeret kommandohjælp | `help` |
| `commands` / `cmds` | — | Kompakt kommandoliste (quick reference) | `commands` |
| `save` / `sv` / `config save` | — | Gem hele konfigurationen til NVS (beregner CRC16 først) | `save` |
| `load` / `ld` / `config load` | — | Genindlæs konfiguration fra NVS og anvend den på det kørende system | `load` |
| `save registers all\|group <navn>` | `all` eller `group <navn>` | Gem persistente register-grupper til NVS | `save registers group sensors` |
| `load registers all\|group <navn>` | `all` eller `group <navn>` | Genindlæs persistente register-grupper fra NVS | `load registers all` |
| `defaults` / `def` | — | Nulstil hele `PersistConfig` til fabriksindstillinger (kun i RAM — kræver `save`) | `defaults` |
| `reboot` / `restart` | — | Genstart ESP32 (2 sek. forsinkelse) | `reboot` |
| `exit` / `quit` / `q` | — | Luk Telnet-session (virker ikke på seriel konsol) | `exit` |
| `ping <host> [count]` | `count`: 1–100 (default 4) | ICMP ping — kræver aktiv WiFi/Ethernet | `ping 10.1.32.1 10` |
| `connect wifi` / `con` | — | Forbind til konfigureret WiFi (validerer SSID/password først) | `connect wifi` |
| `disconnect wifi` / `dc` | — | Afbryd WiFi | `disconnect wifi` |
| `test sr` | — | Test af shift-register outputs (kun boards med `SHIFT_REGISTER_ENABLED`) | `test sr` |
| `test sr input` | — | Test af shift-register inputs | `test sr input` |
| `delete user <navn>` | — | Slet RBAC-bruger | `delete user viewer` |
| `no set gpio <pin>` | — | Fjern GPIO-mapping (auto-gemmes til NVS) | `no set gpio 23` |
| `no set counter <id>` | `id`: 1–4 | Deaktivér/slet counter-konfiguration | `no set counter 1` |
| `reset counter <id>` | `id`: 1–4 | Nulstil counter-værdi til start-value | `reset counter 1` |
| `reset logic stats [all\|cycle\|<id>]` | default `all` | Nulstil ST Logic-statistik (alt, kun cyklus-stats, eller ét program 1–4) | `reset logic stats 2` |
| `clear counters` | — | Nulstil alle 4 counters | `clear counters` |
| `config save` / `config load` | — | Alias for `save`/`load` | `config save` |

## A.3 `show <x>` — Vis-kommandoer

### Oversigt

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `show config [section]` | `section` (valgfri, case-insensitiv delstreng): `system`, `modbus`, `counter`, `timer`, `gpio`, `network`/`wifi`, `telnet`, `ethernet`/`eth`, `http`/`api`, `sse`, `ntp`/`time`, `rate`/`ratelimit`, `analog`/`ao`, `module`, `persist`, `logic`/`st`, `rbac`/`user` | Fuld (eller filtreret) persistent konfiguration. `show config \| s wifi` og `show config \| section wifi` accepteres også (pipe ignoreres). |
| `show status` | — | Runtime-status: uptime, heap, GPIO, statistik |
| `show version` / `ver` / `v` | — | Firmware-version, build-nummer, git-hash/branch |
| `show user` | — | Aktuel sessions auth-info (RBAC-status) |

### Modbus

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `show modbus` | — | Genvej til `show config modbus` (slave + master samlet) |
| `show modbus-slave` / `mb-slave` | — | Modbus Slave-konfiguration + statistik (CRC-fejl, exceptions m.m.) |
| `show modbus-master` / `mb-master` | — | Modbus Master-konfiguration, statistik, async cache-state, adaptive backoff pr. slave, cache entries |
| `show registers [start] [count]` / `regs` | — | Holding registers (default: alle) |
| `show coils` | — | Coil-tilstande |
| `show inputs` / `ins` | — | Discrete inputs |
| `show h-reg` | — | STATIC/DYNAMIC register-mappings (samme output som `set holding-reg` ville producere) |
| `show coil` | — | Coil-tilstande (alias for `show coils`) |
| `show stats` / `show st-stats` | — | ST Logic performance-stats fra Modbus IR 252–293 |

### Features (Counters/Timers/ST Logic/Persistence)

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `show counters` / `cnts` | — | Tabel over alle 4 counters |
| `show counter <id> [verbose]` | `id`: 1–4 | Detaljer for én counter; `verbose` giver udvidet output |
| `show timers` / `tmrs` | — | Tabel over alle 4 timere |
| `show timer <id> [verbose]` | `id`: 1–4 | Detaljer for én timer |
| `show persist` | — | Persistence-grupper (inkl. auto-load-status) |

**`show logic` — undermenu** (`log` er alias for `logic`):

| Kommando | Beskrivelse |
|---|---|
| `show logic <id>` | Program-detaljer (1–4), ST source **skjult** |
| `show logic <id> st` | Som ovenfor, men **med** ST source code |
| `show logic all` | Status for alle 4 programmer |
| `show logic program` | Oversigt m. status-ikon (⚪ EMPTY / 🔴 FAILED / 🟡 DISABLED / 🟢 ACTIVE) |
| `show logic errors` | Kun programmer med kompilerings- eller runtime-fejl |
| `show logic stats` | Performance-statistik (pool-forbrug, cyklustider, overruns) for alle programmer |
| `show logic <id> code` | Source code for ét program |
| `show logic all code` | Source code for alle programmer |
| `show logic <id> timing` | Detaljeret timing-analyse + performance-rating (EXCELLENT/GOOD/ACCEPTABLE/POOR) |
| `show logic <id> bytecode` | Dump af kompileret bytecode (opcodes, variabeltabel) |
| `show logic <id> functions` | Brugerdefinerede funktioner/FUNCTION_BLOCKs (FEAT-003) |
| `show logic <id> debug [vars\|stack]` | Debugger-state / variabelværdier / stack (FEAT-008) |

### Netværk

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `show wifi` | — | WiFi-status, IP, RSSI, MAC |
| `show telnet` | — | Genvej til `show config telnet` |
| `show ethernet` / `eth` | — | Ethernet (W5500)-status |
| `show http` | — | HTTP API-status |
| `show sse` | — | SSE-server-status og tilsluttede klienter |
| `show ntp` | — | NTP-synk.status, server, tidszone |
| `show rate-limit` | — | Rate limiting-status |
| `show users [roles]` | `roles` viser tilgængelige RBAC-roller/privilegier | RBAC-brugerliste, eller (med `roles`) forklaring af roller/privilegier |
| `show backup` | — | URL til backup/restore via HTTP API |

### Hardware / diverse

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `show gpio [pin]` | — | Alle GPIO-mappings, eller én specifik pin |
| `show metrics` | — | Prometheus metrics-reference |
| `show watchdog` / `wdg` | — | Watchdog-monitor-status (reboot-årsag, reboot-tæller) |
| `show debug` / `dbg` | — | Debug-flag-status |
| `show echo` | — | Remote echo on/off |

## A.4 `set <x>` — Konfigurationskommandoer

### System

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `set hostname <navn>` | maks. 31 tegn | Sæt hostname |
| `set echo <on\|off>` | — | Aktivér/deaktivér remote echo |
| `set debug <flag> <on\|off>` | flag: `config-save`, `config-load`, `all` | Debug-logging-flag |
| `set gpio <pin> input <idx>` | pin: 0–39 eller 100–255 (virtuel); idx: discrete input-index | Map GPIO-pin til discrete input |
| `set gpio <pin> coil <idx>` | idx: coil-index | Map coil til GPIO-output |
| `set gpio 2 enable\|disable` | — | `enable` = frigiv GPIO2 til brugerkode (deaktiverer heartbeat-LED); `disable` = reservér til heartbeat (default) |

### Modbus — mode og hardware

| Kommando | Parametre | Beskrivelse |
|---|---|---|
| `set modbus mode <slave\|master\|off>` | — | Vælg RS485-transceiver-rolle (ES32D26 har kun én RS485 — enten slave eller master) |
| `set modbus slave uart <uart0\|uart1\|uart2>` | — | Vælg UART til Modbus Slave |
| `set modbus master uart <uart0\|uart1\|uart2>` | — | Vælg UART til Modbus Master |
| `set modul rs485 <uart1\|uart2> [tx <pin> rx <pin> dir <pin>]` | — | Konfigurér RS485-pins for UART1/UART2 (UART0 er reserveret til USB-konsol) |
| `set modul ethernet <enable\|disable>` | — | Aktivér/deaktivér W5500 Ethernet (bruger GPIO 4,5,16,17,18,19 til SPI) |
| `set ao1 mode <voltage\|current>` | kun ES32D26 | Analog output 1: 0–10V eller 4–20mA |
| `set ao2 mode <voltage\|current>` | kun ES32D26 | Analog output 2 |

### Modbus Slave (`set modbus-slave <param> <værdi>`)

| Parameter | Gyldige værdier | Default |
|---|---|---|
| `enabled` | `on`/`off` | on |
| `slave-id` / `id` | 1–247 | 1 |
| `baudrate` / `baud` | 2400, 4800, 9600, 19200, 38400, 57600, 115200 | 115200 |
| `parity` | `none`, `even`, `odd` (også `n`/`e`/`o`) | none |
| `stop-bits` | 1, 2 | 1 |
| `inter-frame-delay` / `delay` | 0 (=auto t3.5) – 1000 ms | 0 (auto) |

### Modbus Master (`set modbus-master <param> <værdi>`)

| Parameter | Gyldige værdier | Default |
|---|---|---|
| `enabled` | `on`/`off` | off |
| `baudrate` / `baud` | 2400–115200 (standardværdier) | 9600 |
| `parity` | `none`, `even`, `odd` | none |
| `stop-bits` | 1, 2 | 1 |
| `timeout` | 100–5000 ms | 500 |
| `inter-frame-delay` / `delay` | 0 (auto) – 1000 ms | 10 |
| `max-requests` | 1–100 | 10 |
| `cache-ttl` | 0 (aldrig udløb) – 65535 ms | 0 |
| `cache-size` | 1–32 | 32 |
| `queue-size` | 4–32 | 16 |

Hardware (standard ESP32-varianter): UART1, TX=GPIO25, RX=GPIO26, DE/RE=GPIO27. Globale ST-variabler: `mb_last_error` (0=OK,1=TIMEOUT,2=CRC,3=EXCEPTION,4=MAX_REQ,5=DISABLED), `mb_success`.

### Statiske/dynamiske registre og coils

| Kommando | Beskrivelse |
|---|---|
| `set holding-reg STATIC <addr> Value [uint\|int\|dint\|dword\|real] <værdi>` | Skriv statisk register. Type default `uint`. `uint`: 0–65535. `int`: -32768–32767 (16-bit, 1 reg). `dint`/`dword`/`real`: 32-bit (2 reg). Adresser 200–237 er reserveret til ST Logic og afvises. |
| `set holding-reg DYNAMIC <addr> counter<id>:<func>` | `func`: `index`, `raw`, `freq`, `overflow`, `ctrl` |
| `set holding-reg DYNAMIC <addr> timer<id>:<func>` | `func`: `output` |
| `set coil STATIC <addr> Value <ON\|OFF>` | Skriv statisk coil (skrives med det samme) |
| `set coil DYNAMIC <addr> counter<id>:overflow` / `timer<id>:output` | Dynamisk coil-binding |

### Counter (`set counter <id> mode 1 <key:value> ...`, `id`: 1–4)

| Parameter | Værdier |
|---|---|
| `hw-mode` | `sw`, `sw-isr`, `hw` (PCNT) |
| `edge` | `rising`, `falling`, `both` |
| `prescaler` | 1–65535 |
| `scale` | float (default 1.0) |
| `start-value` | 64-bit unsigned |
| `bit-width` | 8, 16, 32, 64 |
| `direction` | `up`, `down` |
| `debounce` | `on`/`off` |
| `debounce-ms` | ms (default 10) |
| `input-dis` | discrete input-index (SW mode) |
| `interrupt-pin` | GPIO (SW-ISR mode) |
| `hw-gpio` | GPIO 1–39 (HW/PCNT mode; advarsel ved strapping-pins 2/15) |
| `compare` / `compare-enabled` | `on`/`off` |
| `compare-value` | 64-bit unsigned |
| `compare-mode` | 0=≥, 1=>, 2=exact |
| `compare-source` | 0=raw, 1=prescaled, 2=scaled |
| `enable` / `disable` | `on`/`off` |

**Registre auto-tildeles** (manuel konfiguration er deaktiveret): Counter 1→HR100-114, Counter 2→HR120-134, Counter 3→HR140-154, Counter 4→HR160-174 (index/raw/freq/overload/ctrl/compare — antal ord afhænger af bit-width).

`set counter <id> control <flag>:<on|off> ...`: `counter-reg-reset-on-read`, `compare-reg-reset-on-read`, `auto-start`, `running` (auto-enabler counteren hvis den var slukket).

### Timer (`set timer <id> mode <1-4> <key:value> ...`, `id`: 1–4)

| Mode | Navn | Parametre |
|---|---|---|
| 1 | One-shot (3-fase) | `p1-duration`, `p1-output`, `p2-duration`, `p2-output`, `p3-duration`, `p3-output` (ms/0-1) |
| 2 | Monostable (retriggerbar puls) | `pulse-ms`, `trigger-level` (0/1) |
| 3 | Astable (blink) | `on-ms`, `off-ms` |
| 4 | Input-triggered | `input-dis` (coil-index, evt. virtuel GPIO 100-255), `delay-ms`, `trigger-edge` (0=falling,1=rising) |

Fælles: `output-coil` (0–65535), `ctrl-reg` (Modbus-register — bit0=START, bit1=STOP, bit2=RESET, autoclearer), `enabled` (`on`/`off`/`1`).

### ST Logic

| Kommando | Beskrivelse |
|---|---|
| `set logic <id> upload "<kode>"` | Inline upload + auto-kompilering |
| `set logic <id> upload` (uden kode) | Starter multi-line upload-mode — se §A.7 |
| `set logic <id> enabled:true\|false` / `set logic <id> enabled\|disabled` | Aktivér/deaktivér program (kræver kompileret program) |
| `set logic <id> reinit` / `coldstart` | Cold restart: nulstil variabler til VAR-initialværdier, TON/TOF/CTU/CTD, FB-instanser |
| `set logic <id> delete` | Slet program |
| `set logic <id> bind <var_navn> reg:<addr>\|coil:<addr>\|input-dis:<addr>\|input:<addr> [input\|output\|both]` | Ny syntaks: bind ST-variabel til Modbus (navngivet). Default retning: `output` for reg:/coil:, `input` for input-dis:/input: |
| `set logic <id> bind <var_idx> <register> [input\|output\|both]` | Gammel syntaks (numerisk var-indeks) — stadig understøttet |
| `set logic debug:true\|false` | Globalt ST Logic debug-flag (bytecode-print, exec-trace) |
| `set logic interval:<ms>` / `set logic interval <ms>` | Global exekveringsinterval: **2, 5, 10, 20, 25, 50, 75, 100** ms (kun disse værdier) |

**Debugger (FEAT-008), `set logic <id> debug <subkommando>`:**

| Subkommando | Beskrivelse |
|---|---|
| `pause` | Pause ved næste instruktion (kræver enabled program) |
| `continue` / `cont` | Fortsæt til breakpoint/halt |
| `step` | Exekvér én instruktion |
| `break <pc>` | Sæt breakpoint på bytecode-adresse |
| `break line <linje>` | Sæt breakpoint på kildekode-linje (kræver gyldigt line-map fra seneste kompilering) |
| `clear [pc]` | Fjern ét (eller alle) breakpoints |
| `stop` | Stop debugging, genoptag normal kørsel |

Max 8 breakpoints pr. program.

### Persistence-grupper (`set persist ...`)

| Kommando | Beskrivelse |
|---|---|
| `set persist group <navn> add <reg-spec>` | Tilføj registre. `reg-spec` understøtter ranges/lister: `100-105`, `110,112`, `100-105,110,120-122`, eller gammel space-separeret syntaks |
| `set persist group <navn> remove <reg>` | Fjern ét register fra gruppe |
| `set persist group <navn> delete` | Slet gruppe |
| `set persist enable <on\|off>` | Aktivér/deaktivér persistence-systemet |
| `set persist reset` / `clear` | Slet **alle** grupper (bruges ved korruption) |
| `set persist auto-load enable\|disable` | Auto-load ved boot til/fra |
| `set persist auto-load add\|remove <group_id>` | Tilføj/fjern gruppe fra auto-load-listen |

Max 8 grupper × 16 registre. ST Logic: `SAVE(0)`/`LOAD(0)` = alle grupper, `SAVE(id)`/`LOAD(id)` = specifik gruppe (rate-limited).

### Netværk

**`set wifi <option> [værdi]`:** `ssid <navn>` (maks 32), `password <pw>` (8–63 tegn), `dhcp on|off`, `ip/gateway/netmask/dns <adr>`, `power-save on|off`, `enable`/`disable`, samt (legacy, nu ækvivalent med `set telnet`) `telnet enable|disable|on|off`, `telnet-user`, `telnet-pass`, `telnet-port`.

**`set telnet <option> [værdi]`:** `enable`/`on`, `disable`/`off`, `user <navn>`, `pass <pw>`, `port <1-65535>` (default 23).

**`set ethernet <option> [værdi]`:** `enable`, `disable`, `dhcp on|off`, `ip/gateway/netmask/dns <adr>`.

**`set http <option> <værdi>`:** `enabled on|off`, `port <1-65535>` (default 80, bruges KUN til almindelig HTTP), `https-port <1-65535>` (default 443, dedikeret HTTPS-port — se BUG-350, kræver reboot), `auth on|off`, `username <navn>`, `password <pw>`, `api on|off` (aktiverer/deaktiverer REST API-endpoints), `tls on|off` (kræver reboot; lytter på `https-port`, ikke `port`).

**`set sse <option> [værdi]`:** `enable`/`disable` (kræver reboot), `port <0-65535>` (0=auto=HTTP-port+1), `max-clients <1-5>`, `interval <50-5000>` (check-interval ms), `heartbeat <1000-60000>` (ms), `disconnect all` / `disconnect <slot>`.

**`set rate-limit enable|disable`:** Slår token-bucket rate limiting til/fra (default: aktiveret, 30 req burst / 10 req/s pr. IP).

**`set ntp <option> [værdi]`:** `enable`, `disable`, `server <hostname>`, `timezone <POSIX-TZ>` / `tz` (fx `CET-1CEST,M3.5.0,M10.5.0/3` for Danmark, `UTC0`), `interval <1-1440>` (minutter).

### Sikkerhed / RBAC

| Kommando | Beskrivelse |
|---|---|
| `set rbac enable\|disable` | Aktivér/deaktivér Role-Based Access Control (advarer hvis 0 brugere findes) |
| `set user <navn> password <pw> roles <roller> privilege <priv>` | Opret/opdater bruger (maks 8). `roles`: kommasepareret `api,cli,editor,monitor` eller `all`. `privilege`: `read`, `write`, `read/write` (alias `rw`) |
| `delete user <navn>` | Slet bruger |

Roller: `api`=0x01 (REST API + SSE), `cli`=0x02 (CLI), `editor`=0x04 (`/editor`), `monitor`=0x08 (dashboard `/` + SSE-streams), `all`=0x0F. SSE kræver `api` **eller** `monitor`. Read-only brugere (uden write-privilegie) må kun bruge `show `, `sh `, `help`, `ping `, `?`.

### Deprecated (bagudkompatible, virker stadig)

| Kommando | Erstattes af |
|---|---|
| `set baud <rate>` | `set modbus-slave baudrate <rate>` |
| `set id <slave_id>` | `set modbus-slave slave-id <id>` |

## A.5 `read` / `write` — Lokal Modbus-dataadgang

| Kommando | Beskrivelse |
|---|---|
| `read h-reg <addr> [count] [uint\|int\|dint\|dword\|real]` | Læs holding register(e). Type kan stå som 2. **eller** 3. argument. `dint`/`dword`/`real` er 32-bit (2 reg pr. værdi, `count` gentager). Trigger reset-on-read for compare-flag. |
| `read i-reg <addr> [count] [uint\|int\|dint\|dword\|real]` | Læs input register(e) (0–255). Samme typer/logik som h-reg. IR 200-251/252-293 = ST Logic status/timing. |
| `read coil <addr> [count]` | Læs coil(s), 0/1 pr. linje |
| `read input <addr> [count]` | Læs discrete input(s) |
| `write h-reg <addr> value uint\|int\|dint\|dword\|real <værdi>` | Skriv holding register. `uint`:0-65535, `int`:-32768..32767, `dint`/`dword`/`real`: 2 registre (little-endian LSW først) |
| `write coil <addr> [value] <on\|off\|1\|0>` | Skriv coil (`value`-keyword er valgfrit) |

Range: Holding/Input registers 0–255 (`HOLDING_REGS_SIZE`/`INPUT_REGS_SIZE`=256), Coils/Discrete Inputs 0–255 (32 byte × 8 bit).

## A.6 `mb <x>` — Modbus Master remote-kommandoer

Alle `mb`-kommandoer kræver `set modbus-master enabled on`. Baudrate kan overskrives midlertidigt ved at angive den som sidste argument (gyldige: 2400/4800/9600/19200/38400/57600/115200) — gendannes automatisk efter kommandoen.

| Kommando | Beskrivelse |
|---|---|
| `mb read coil <slave> <addr> [baud]` | FC01 |
| `mb read input <slave> <addr> [baud]` | FC02 |
| `mb read holding <slave> <addr> [count] [baud]` | FC03, count 1–16, default 1. Alias: `h-reg`/`hreg` |
| `mb read input-reg <slave> <addr> [baud]` | FC04. Alias: `i-reg`/`ireg` |
| `mb write coil <slave> <addr> <0\|1\|on\|off> [baud]` | FC05 |
| `mb write holding <slave> <addr> <value> [baud]` | FC06. Alias: `h-reg`/`hreg` |
| `mb scan [start_id] [end_id] [baud]` | Scanner FC03 addr 0 for slave-id'er 1–247 (default), 10ms pause pr. forsøg |
| `mb reset backoff [slave_id]` | Nulstil adaptiv backoff (én slave eller alle) |
| `mb reset stats` | Nulstil master-statistik |
| `mb reset cache` | Ryd async cache-entries |
| `mb ?` / `mb help` | Detaljeret hjælp |

Aliaser: `mb rd`=`mb read`, `mb wr`=`mb write`, `mb rst`=`mb reset`. `slave_id`: 1–247.

Fejlkoder vist ved fejl: `OK`, `TIMEOUT`, `CRC ERROR`, `EXCEPTION`, `NOT ENABLED`, `INVALID SLAVE ID`, `INVALID ADDRESS`.

## A.7 Multi-line ST Logic upload-mode

`set logic <id> upload` (uden inline kode) skifter prompten til `>>> ` og optager linjer i en 5000-byte buffer indtil brugeren skriver `END_UPLOAD` (case-insensitivt), hvorefter koden kompileres automatisk. Echo-indstilling (`set echo`) bevares under upload.

## A.8 Kendte unøjagtigheder i den indbyggede hjælp

Fundet under kildekode-gennemgangen — dokumenteret her så I ikke arver dem ved at følge `help`-outputtet blindt:

- Den indbyggede `help`-kommando viser `upload logic <id> <source>` som selvstændig kommando — **findes ikke** i dispatcheren. Brug `set logic <id> upload "<source>"` (§A.4).
- RBAC-hjælpeteksten nævner `who` som tilladt read-only-kommando, men den er ikke koblet til noget i kommando-dispatcheren — vil give "Unknown command".

---

[← 13. Fejlfinding](13_Fejlfinding.md) · [Indeks](00_INDEKS.md) · Næste: [Appendiks B: REST API-reference →](B_REST_API_Reference.md)
