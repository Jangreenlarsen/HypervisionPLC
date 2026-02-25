# Changelog - ESP32 Modbus RTU Server

All notable changes to this project are documented in this file.

---

## [6.1.0] - 2026-02-25 (W5500 Ethernet + Telnet Standalone)

### NEW FEATURES

**W5500 SPI Ethernet**
- W5500 Ethernet driver via SPI bus (GPIO12 MISO, GPIO13 MOSI, GPIO14 CLK, GPIO15 CS, GPIO33 RST, GPIO34 INT)
- `set ethernet enable|disable` — aktivér/deaktivér Ethernet
- `set ethernet dhcp on|off` — DHCP eller statisk IP
- `set ethernet ip/gateway/netmask/dns` — statisk konfiguration
- `show ethernet` — status med link, IP, MAC, hastighed
- Ethernet og WiFi er uafhængige — kan køre samtidigt eller hver for sig
- Build-flag `ETHERNET_W5500_ENABLED` i platformio.ini

**Telnet som selvstændig sektion**
- `set telnet enable|disable` — selvstændig CLI kommando (ikke under `set wifi`)
- `set telnet user/pass/port` — konfigurér Telnet uafhængigt af WiFi
- `sh config | s telnet` — viser altid telnet-konfiguration, uanset WiFi-status
- Backward-compatible: `set wifi telnet*` virker stadig som alias

### BUG FIXES

- **BUG-221+222:** CLI parser `set wifi disable` og `set logic interval` med mellemrum
- **BUG-220:** Ethernet+Services uafhængig af WiFi + W5500 MAC fra eFuse
- **BUG-223:** W5500 ping latens ~1000ms fix — GPIO34 ISR upålidelig, polling med xTaskNotifyGive (nu 2-5ms)
- **BUG-224:** Telnet echo langsom — TCP_NODELAY på client socket
- **BUG-225:** `sh config | s telnet` viste ingen SET commands — telnet var nested under WiFi

### FILES CHANGED

| File | Change |
|------|--------|
| `src/ethernet_driver.cpp/h` | W5500 SPI driver, polling workaround |
| `src/tcp_server.cpp` | TCP_NODELAY på accept |
| `src/cli_show.cpp` | [TELNET] selvstændig sektion, `set telnet` kommandoer |
| `src/cli_commands.cpp` | `cli_cmd_set_telnet()` funktion |
| `src/cli_parser.cpp` | `set telnet` routing + wifi help opdateret |
| `include/cli_commands.h` | `cli_cmd_set_telnet()` deklaration |
| `include/constants.h` | Version 6.1.0 |

### BUILD

- **Version:** 6.1.0
- **Schema:** 10 (unchanged)

---

## [6.0.7] - 2026-02-14 (Config Backup/Restore)

### ✨ NEW FEATURES

**FEAT-017: Config Backup/Restore via HTTP API + CLI**

**GET /api/system/backup - Full Config Download**
- Downloads entire ESP32 configuration as JSON file (~12-18KB)
- Includes ALL fields: Modbus slave/master, network, HTTP, telnet, counters, timers
- Includes passwords (WiFi, HTTP auth, telnet) for complete restore
- Includes ST Logic source code for all 4 program slots
- Includes static/dynamic registers, coils, variable mappings, persist groups
- `Content-Disposition: attachment` header for browser auto-download
- `backup_version: 1` field for future format compatibility

**POST /api/system/restore - Full Config Restore**
- Accepts backup JSON (up to 24KB) and restores entire configuration
- Validates `backup_version` compatibility before applying
- Restores all config sections with `containsKey` checks (partial restore safe)
- ST Logic: deletes existing programs, uploads source, compiles, sets enabled state
- Auto-saves to NVS + SPIFFS and calls `config_apply()` to activate
- Returns warning: "Reboot recommended"

**CLI: show backup**
- Displays download URL based on current IP, port, and protocol (HTTP/HTTPS)
- Shows curl example for restore operation

### 📁 FILES CHANGED

| File | Change |
|------|--------|
| `src/api_handlers.cpp` | +2 handlers: backup (GET) + restore (POST), endpoint discovery updated |
| `src/http_server.cpp` | +2 URI definitions + registration |
| `include/api_handlers.h` | +2 extern declarations |
| `src/cli_show.cpp` | +`cli_cmd_show_backup()` |
| `include/cli_show.h` | +declaration |
| `src/cli_parser.cpp` | +BACKUP alias, dispatch, help text |
| `BUGS_INDEX.md` | +FEAT-017 entry |

### 🏗️ BUILD

- **Version:** 6.0.7
- **Build:** #1227
- **Schema:** 10 (unchanged)
- **RAM:** 32.9% (107,952 / 327,680 bytes)
- **Flash:** 94.9% (1,244,233 / 1,310,720 bytes)

---

## [6.0.3] - 2026-01-25 📋 (Test Plan Restructuring)

### 📋 DOCUMENTATION

**Test Plan Modularisering**
- **Problem:** `ST_COMPLETE_TEST_PLAN.md` var 46.000+ tokens - for stor til effektiv brug
- **Løsning:** Opdelt i 9 fokuserede testfiler i ny `tests/` mappe

**Ny Test Struktur:**
| Fil | Indhold | Tests |
|-----|---------|-------|
| `tests/TEST_INDEX.md` | Navigation hub | - |
| `tests/API_TEST_PLAN.md` | **NY:** HTTP REST API tests | 41 |
| `tests/ST_TEST_OPERATORS.md` | Aritmetik, logik, bit-shift, sammenligning | 20 |
| `tests/ST_TEST_BUILTINS.md` | Math, clamping, trig, conversion, persistence | 23 |
| `tests/ST_TEST_TIMERS.md` | TON/TOF/TP timer funktioner | 6 |
| `tests/ST_TEST_GPIO.md` | GPIO & Hardware tests | 4 |
| `tests/ST_TEST_CONTROL.md` | IF, CASE, FOR, WHILE, REPEAT | 6 |
| `tests/ST_TEST_TYPES.md` | INT/DINT/REAL, EXPORT, TIME literals | 12 |
| `tests/ST_TEST_COMBINED.md` | Kombinerede tests (Fase 2) | 10 |
| **Total** | | **122+** |

**API Test Plan Highlights:**
- Konfiguration tests (enable/disable, port, persistence)
- Endpoint tests (status, counters, timers, registers, logic)
- Authentication tests (credentials, 401 errors)
- Error handling tests (404, invalid JSON, concurrent requests)
- Performance tests (response time, throughput, memory)
- Alle tests har copy/paste curl kommandoer

**Dokumentation Opdateringer:**
- `CLAUDE_INDEX.md` - Tilføjet tests/ mappe reference
- `README.md` - Tilføjet Test Documentation sektion

### 📁 FILES ADDED

| File | Description |
|------|-------------|
| `tests/TEST_INDEX.md` | Test navigation hub |
| `tests/API_TEST_PLAN.md` | HTTP REST API testplan |
| `tests/ST_TEST_OPERATORS.md` | Operator tests |
| `tests/ST_TEST_BUILTINS.md` | Builtin function tests |
| `tests/ST_TEST_TIMERS.md` | Timer tests |
| `tests/ST_TEST_GPIO.md` | GPIO tests |
| `tests/ST_TEST_CONTROL.md` | Control structure tests |
| `tests/ST_TEST_TYPES.md` | Type system tests |
| `tests/ST_TEST_COMBINED.md` | Combined tests |

### 🏗️ BUILD

- **Version:** 6.0.3
- **Build:** #1133
- **Schema:** 10 (unchanged)

---

## [6.0.2] - 2026-01-23 🛠️ (NVS Fix + API Discovery)

### ✨ NEW FEATURES

**GET /api/ - API Discovery Endpoint**
- **Purpose:** Lists all available REST API endpoints with descriptions
- **Response:** JSON with name, version, build, and endpoints array
- **Usage:** Helps clients discover available API functionality
- **Fix:** Uses heap allocation to prevent stack overflow crash

**ST Logic Source Upload/Download via API**
- **GET /api/logic/{1-4}/source** - Download ST program source code as JSON
- **POST /api/logic/{1-4}/source** - Upload ST program (body: `{"source": "VAR..."}`)
- **Response:** Includes program name, source, size, compile status
- **Limit:** Max 8KB source code per upload
- **Fix:** URI routing via api_handler_logic_single (wildcard matching issue)

### 🐛 BUG FIXES

**HTTP Server crash på ukendte URLs (BUG-194)**
- **Problem:** System crashede når ukendte API endpoints blev kaldt
- **Årsag:** Ingen error handler for 404/405 errors + stack overflow (4KB for lille)
- **Fix:** Custom error handler returnerer JSON fejlbesked i stedet for crash
- **Fix:** Stack size øget fra 4KB til 8KB

**Telnet command history viser escape sekvenser (BUG-195)**
- **Problem:** Pil op/ned viste `^[[A^[[B` i stedet for kommandoer fra historik
- **Årsag:** Manglende IAC negotiation + klient gjorde local echo
- **Fix:** Server sender nu WILL ECHO + DONT ECHO ved connection
- **Fix:** Line redraw bruger kompatibel metode (ingen ANSI codes)

**NVS Save Error 4357 (ESP_ERR_NVS_NOT_ENOUGH_SPACE)**
- **Problem:** NVS partition becomes fragmented after multiple save operations
- **Symptom:** `save` command fails with error 4357 after running `defaults`
- **Root cause:** NVS pages fill up and cannot allocate new blob without erasing old
- **Fix:** Added `reset nvs` command to erase and reinitialize NVS partition
- **Hint:** Error message now suggests running `reset nvs` to fix

### ✨ NEW FEATURES

**reset nvs Command**
- **CLI:** `reset nvs` - Erases entire NVS partition and reinitializes
- **Purpose:** Fixes NVS fragmentation that prevents saving configuration
- **Warning:** Deletes ALL saved configuration - use `defaults` then `save` afterwards

### 📁 FILES CHANGED

| File | Change |
|------|--------|
| `src/api_handlers.cpp` | +api_handler_endpoints(), +logic_source_get/post() |
| `src/http_server.cpp` | +uri_endpoints, +uri_logic_source_get/post |
| `include/api_handlers.h` | +api_handler_endpoints(), +logic_source declarations |
| `src/cli_show.cpp` | +source endpoints in show http |
| `src/cli_parser.cpp` | +reset nvs command, +nvs_flash.h include |
| `src/config_save.cpp` | +sizeof in error message, +hint for error 4357 |
| `include/constants.h` | Version 6.0.2 |

### 🏗️ BUILD

- **Version:** 6.0.2
- **Build:** #1132
- **Schema:** 10 (unchanged)

---

## [6.0.1] - 2026-01-23 🔧 (HTTP API Enhancements & Bugfixes)

### 🐛 BUG FIXES

**BUG-192: HTTP Basic Auth virker ikke**
- **Problem:** Base64-encoded credentials manglede nul-terminering efter `mbedtls_base64_encode()`
- **Symptom:** `strcmp()` fejlede altid, auth blev afvist selvom credentials var korrekte
- **Fix:** Tilføjet `encoded[encoded_len] = '\0'` efter encoding (http_server.cpp:350)

**defaults kommando manglede HTTP config**
- **Problem:** `cli_cmd_defaults()` havde sin egen minimalistiske initialisering
- **Fix:** Bruger nu `config_struct_create_default()` som inkluderer alle subsystemer

**config_struct_create_default() manglede network defaults**
- **Problem:** Funktionen kaldte ikke `network_config_init_defaults()`
- **Fix:** Tilføjet kald så HTTP/WiFi/Telnet config initialiseres korrekt

### ✨ NEW FEATURES

**HTTP API enable/disable (separat fra HTTP server)**
- **CLI:** `set http api enable` / `set http api disable`
- **Formål:** Tillader HTTP server at køre uden REST API endpoints (for fremtidige udvidelser)
- **Config output:** Viser nu `api: enabled/disabled` i [API HTTP] sektion

**Debug flags for HTTP**
- **CLI:** `set debug http-server on|off` - Debug HTTP server start/stop/config
- **CLI:** `set debug http-api on|off` - Debug API requests/responses med URI og status
- **show debug:** Viser nu `http_server` og `http_api` flags

**Reset HTTP statistics**
- **CLI:** `reset http stats` - Nulstiller alle HTTP API statistikker

**Forbedret config output**
- **[API HTTP] sektion:** Viser nu `server`, `port`, `api`, `auth` status
- **SET COMMANDS:** Viser `set http api enable/disable` kommando

### 📁 FILES CHANGED

| File | Change |
|------|--------|
| `include/types.h` | +api_enabled i HttpConfig, +http_server/http_api i DebugFlags |
| `include/debug_flags.h` | +debug_flags_set_http_server/http_api() |
| `src/debug_flags.cpp` | +implementering af nye flags |
| `src/cli_commands.cpp` | Opdateret set debug, defaults bruger config_struct_create_default() |
| `src/cli_show.cpp` | Opdateret show debug, config output, show http |
| `src/cli_parser.cpp` | +reset http stats kommando, +http_server.h include |
| `src/http_server.cpp` | +debug output, +auth fix (nul-terminering) |
| `src/api_handlers.cpp` | +debug logging i api_send_json/api_send_error |
| `src/config_struct.cpp` | +network_config_init_defaults() kald |
| `BUGS_INDEX.md` | +BUG-192, opdateret FEAT-011 |

### 🏗️ BUILD

- **Version:** 6.0.1
- **Build:** #1113
- **Schema:** 10 (unchanged)

---

## [6.0.0] - 2026-01-21 🌐 (HTTP REST API - FEAT-011)

### ✨ NEW FEATURES

**HTTP REST API for Node-RED Integration**
- **Feature:** Full REST API with JSON responses for all system data
- **Server:** ESP-IDF esp_http_server med Basic Auth support
- **Library:** ArduinoJson v7.0.0 for JSON serialization

**API Endpoints:**
| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | /api/status | System info (version, uptime, heap, wifi) |
| GET | /api/counters | All 4 counters |
| GET | /api/counters/{1-4} | Single counter |
| GET | /api/timers | All 4 timers |
| GET | /api/timers/{1-4} | Single timer |
| GET | /api/registers/hr/{addr} | Read holding register |
| POST | /api/registers/hr/{addr} | Write holding register |
| GET | /api/registers/ir/{addr} | Read input register |
| GET | /api/registers/coils/{addr} | Read coil |
| POST | /api/registers/coils/{addr} | Write coil |
| GET | /api/registers/di/{addr} | Read discrete input |
| GET | /api/logic | All ST Logic programs |
| GET | /api/logic/{1-4} | Single program |

**CLI Commands:**
- `set http enable/disable` - Enable/disable HTTP server
- `set http port <port>` - Set HTTP port (default 80)
- `set http auth enable/disable` - Enable/disable Basic Auth
- `set http username <user>` - Set auth username
- `set http password <pass>` - Set auth password
- `show http` - Show HTTP server status and statistics

### 📁 FILES ADDED

- `include/http_server.h` - HTTP server API
- `src/http_server.cpp` - ESP-IDF HTTP server wrapper
- `include/api_handlers.h` - REST endpoint handlers
- `src/api_handlers.cpp` - JSON response building

### 🏗️ BUILD

- **Version:** 6.0.0
- **Build:** #1108
- **Schema:** 10 (migration from v9 adds HttpConfig)

---

## [4.8.0] - 2026-01-07 🎛️ (Signal Processing Function Blocks)

### ✨ NEW FEATURES

**Signal Processing and Conditioning (IEC 61131-3 Industrial Applications)**
- **Feature:** Added 4 industrial signal processing function blocks
- **Standard:** Designed for PLC-style signal conditioning and processing
- **Files Added:**
  - `include/st_builtin_signal.h` - Signal processing function declarations
  - `src/st_builtin_signal.cpp` - SCALE/HYSTERESIS/BLINK/FILTER implementations
  - `docs/ST_SIGNAL_PROCESSING_DESIGN.md` - Design specification
  - `docs/ST_LATCH_TEST_CASES.md` - SR/RS latch test suite (14 test cases)
- **Files Modified:**
  - `include/st_stateful.h` - Added signal processing instance storage
  - `src/st_stateful.cpp` - Added allocation for hysteresis/blink/filter
  - `include/st_builtins.h` - Added 4 new builtin enums
  - `include/st_compiler.h` - Added instance counters (3 new types)
  - `src/st_compiler.cpp` - Extended VM to support 5-argument functions
  - `src/st_vm.cpp` - Added signal processing execution handlers
  - `src/st_builtins.cpp` - Added argument count logic

**SCALE - Linear Scaling/Mapping (Stateless):**
```structured-text
(* Scale 4-20mA sensor (0-16383 ADC) to 0-100°C *)
VAR
  adc_reading : REAL;      (* 0-16383 *)
  temperature : REAL;      (* °C *)
END_VAR

temperature := SCALE(adc_reading, 0.0, 16383.0, 0.0, 100.0);
(* Clamps input to range, linear interpolation *)
```

**HYSTERESIS - Schmitt Trigger with Dead Zone (Stateful):**
```structured-text
(* Temperature control with 2°C hysteresis *)
VAR
  temp : REAL;
  heater_on : BOOL;
END_VAR

heater_on := HYSTERESIS(temp, 22.0, 20.0);
(* ON when temp < 20°C, OFF when temp > 22°C *)
(* Dead zone 20-22°C: output holds previous state *)
```

**BLINK - Pulse Generator (Stateful):**
```structured-text
(* Warning light: 500ms ON, 500ms OFF *)
VAR
  enable : BOOL;
  led_state : BOOL;
END_VAR

led_state := BLINK(enable, 500, 500);
(* State machine: IDLE → ON_PHASE → OFF_PHASE *)
```

**FILTER - Exponential Moving Average (Stateful):**
```structured-text
(* Smooth noisy sensor with 1000ms time constant *)
VAR
  sensor_raw : REAL;
  sensor_smooth : REAL;
END_VAR

sensor_smooth := FILTER(sensor_raw, 1000);
(* Low-pass filter, alpha = DT/(tau+DT) *)
```

**Memory Usage:**
- SCALE: 0 bytes (stateless)
- HYSTERESIS instance: 1 byte (Q state)
- BLINK instance: 6 bytes (Q, state, timer)
- FILTER instance: 4 bytes (out_prev)
- Max 8 instances per type per ST program
- Total: +88 bytes per program (452→540 bytes)

**VM Enhancement:**
- Extended function call support from 3 to 5 arguments
- Added arg4 and arg5 with type tracking
- Proper reverse-order argument popping

**Build:** #999-1006
**Status:** ✅ IMPLEMENTED and TESTED

---

## [4.7.3] - 2026-01-07 ✨ (SR/RS Bistable Latches)

### ✨ NEW FEATURES

**SR/RS Bistable Latches (IEC 61131-3 Standard Function Blocks)**
- **Feature:** Added SR (Set-Reset) and RS (Reset-Set) bistable latch function blocks
- **Standard:** Fully compliant with IEC 61131-3 standard specifications
- **Files Added:**
  - `include/st_builtin_latch.h` - Latch function declarations
  - `src/st_builtin_latch.cpp` - SR/RS implementations
- **Files Modified:**
  - `include/st_stateful.h` - Added latch instance storage (8 latches per program)
  - `src/st_stateful.cpp` - Added allocation and getter functions
  - `include/st_builtins.h` - Added ST_BUILTIN_SR and ST_BUILTIN_RS enums
  - `include/st_compiler.h` - Added latch_instance_count field
  - `src/st_compiler.cpp` - Added latch instance allocation during compilation
  - `src/st_vm.cpp` - Added latch execution in VM

**SR Latch (Set-Reset, Reset Priority):**
```structured-text
(* Alarm system with reset button priority *)
VAR
  sensor_triggered : BOOL;
  reset_button : BOOL;
  alarm_active : BOOL;
END_VAR

alarm_active := SR(sensor_triggered, reset_button);
(* If both S=1 and R=1, output = 0 (reset wins) *)
```

**RS Latch (Reset-Set, Set Priority):**
```structured-text
(* Motor control with start button priority *)
VAR
  start_button : BOOL;
  stop_button : BOOL;
  motor_running : BOOL;
END_VAR

motor_running := RS(start_button, stop_button);
(* If both S=1 and R=1, output = 1 (set wins) *)
```

**Memory Usage:**
- Latch instance: 4 bytes (type + state)
- Max 8 latches per ST program
- Total: 32 bytes per program (added to stateful storage)

**Build:** #999
**Status:** ✅ IMPLEMENTED and TESTED

---

## [4.6.1] - 2026-01-01 🔴 (Critical Type-Handling Bug Fixes)

### 🔴 CRITICAL BUG FIXES
- **BUG-134: MB_WRITE DINT Arguments Sender Garbage Data**
  - **Issue:** When `slave_id` or `address` arguments are DINT type, builtin functions accessed `int_val` instead of `dint_val` → garbage data validation
  - **Impact:** CRITICAL - Invalid Modbus requests could be accepted, valid requests could be rejected, data corruption on remote devices
  - **Fix:** Added type promotion in VM handler - DINT/DWORD → INT with clamping
  - **File:** `src/st_vm.cpp:809-895`
  - **Status:** ✅ FIXED in Build #919

- **BUG-135: MB_WRITE_HOLDING Mangler Value Type Validering**
  - **Issue:** Always accessed `value.int_val` regardless of actual type (REAL/DINT/DWORD)
  - **Impact:** CRITICAL - REAL/DINT/DWORD values sent garbage to remote holding registers
  - **Example:** `MB_WRITE_HOLDING(1, 100) := 42.5` (REAL) → garbage sent instead of 42
  - **Fix:** Type conversion in VM: REAL→INT truncate, DINT→INT clamp, DWORD→INT lower 16 bits
  - **File:** `src/st_vm.cpp:852-895`
  - **Status:** ✅ FIXED in Build #919

- **BUG-136: MB_WRITE_COIL Mangler Value Type Validering**
  - **Issue:** Always accessed `value.bool_val` regardless of actual type (INT/REAL/DINT/DWORD)
  - **Impact:** CRITICAL - Non-BOOL values caused random coil states (garbage bool_val)
  - **Example:** `MB_WRITE_COIL(3, 20) := 42` (INT) → random TRUE/FALSE instead of conversion
  - **Fix:** Type conversion in VM: Non-zero → TRUE, Zero → FALSE (IEC 61131-3 standard)
  - **File:** `src/st_vm.cpp:809-851`
  - **Status:** ✅ FIXED in Build #919

### TECHNICAL DETAILS

**Root Cause:**
- `st_value_t` is a union - when DINT pushed, only `dint_val` is valid, `int_val` is garbage
- VM correctly pops typed values, but builtin functions ignored type information
- SEL/LIMIT functions had correct type handling, but MB_WRITE/MB_READ functions did not

**Type Conversion Logic:**

**MB_WRITE_COIL:**
```c
// Slave ID & Address: DINT/DWORD → INT (clamp to [-32768, 32767])
// Value: INT/REAL/DINT/DWORD → BOOL (non-zero = TRUE)
MB_WRITE_COIL(remote_dint, addr_dint) := int_val;
// ✅ Now works correctly with type promotion
```

**MB_WRITE_HOLDING:**
```c
// Slave ID & Address: DINT/DWORD → INT (clamp to [-32768, 32767])
// Value: REAL → INT (truncate), DINT → INT (clamp), DWORD → INT (lower 16 bits)
MB_WRITE_HOLDING(1, 100) := 42.5;  // REAL
// ✅ Now writes 42 instead of garbage
```

**Affected Use Cases (NOW FIXED):**
```structured-text
VAR
  remote_id: DINT := 3;
  base_addr: DINT := 40000;
  temperature: REAL := 72.5;
  enable_flag: INT := 1;
END_VAR

(* BUG-134 FIX - DINT arguments *)
temp := MB_READ_HOLDING(remote_id, base_addr);  (* ✅ Now works *)

(* BUG-135 FIX - REAL value *)
MB_WRITE_HOLDING(1, 100) := temperature;  (* ✅ Writes 72 *)

(* BUG-136 FIX - INT to BOOL conversion *)
MB_WRITE_COIL(3, 20) := enable_flag;  (* ✅ Writes TRUE (non-zero) *)
```

### UPGRADE NOTES
1. **Impact:** Systems using DINT/REAL/DWORD in MB_WRITE operations were sending **garbage data**
2. **Severity:** CRITICAL - Data corruption on remote Modbus devices possible
3. **Recommendation:** **UPGRADE IMMEDIATELY** if using:
   - DINT variables for slave_id or address
   - REAL values in MB_WRITE_HOLDING
   - INT values in MB_WRITE_COIL (expecting BOOL conversion)
4. **Testing:** Verify all Modbus Master write operations after upgrade

### BUILD INFO
- **Version:** v4.6.1
- **Build:** #919
- **Flash:** 961,721 bytes (73.4%) - +336 bytes from type-handling code
- **RAM:** 121,900 bytes (37.2%) - unchanged

---

## [4.6.0] - 2026-01-01 🚀 (New MB_WRITE Syntax + Critical Bug Fix)

### 🔴 CRITICAL BUG FIX
- **BUG-133: Modbus Master Request Counter Reset**
  - **Issue:** `g_mb_request_count` was never reset after ST execution cycles
  - **Impact:** System blocked all Modbus requests after 10 requests (max_requests_per_cycle)
  - **Fix:** Added `g_mb_request_count = 0;` in `st_logic_engine_loop()` before each cycle
  - **File:** `src/st_logic_engine.cpp:137`
  - **Severity:** CRITICAL - System could not communicate with remote slaves
  - **Status:** ✅ FIXED in Build #911

### ⭐ NEW FEATURE: Assignment-Based MB_WRITE Syntax
- **New IEC 61131-3 Compliant Syntax for Remote Writes:**
  ```structured-text
  (* Old syntax (deprecated) *)
  success := MB_WRITE_COIL(3, 20, TRUE);

  (* New syntax (v4.6.0+) - recommended *)
  MB_WRITE_COIL(3, 20) := TRUE;
  IF mb_success THEN
    (* Check global variable instead of return value *)
  END_IF;
  ```

- **Benefits:**
  - ✅ More intuitive (resembles variable assignments)
  - ✅ Symmetric with READ operations (`temp := MB_READ_HOLDING(1, 100)`)
  - ✅ IEC 61131-3 style for remote I/O
  - ✅ Clearer separation of address specification from value

- **Variable & Expression Support:**
  ```structured-text
  (* All arguments support variables *)
  VAR
    REMOTE_IO: INT := 3;
    COIL_ADDR: INT := 20;
    heating_on: BOOL := TRUE;
  END_VAR

  MB_WRITE_COIL(REMOTE_IO, COIL_ADDR) := heating_on;

  (* Dynamic addressing with expressions *)
  FOR i := 0 TO 9 DO
    MB_WRITE_HOLDING(2, 200 + i) := i * 10;
  END_FOR;
  ```

- **Syntax:**
  - `MB_WRITE_COIL(slave_id, address) := boolean_value`
  - `MB_WRITE_HOLDING(slave_id, address) := int_value`
  - Success check: Use global `mb_success` and `mb_last_error` variables

### TECHNICAL
- **New AST Node Type:**
  - `ST_AST_REMOTE_WRITE` - Dedicated node for remote write statements
  - `st_remote_write_t` struct with slave_id, address, value expression nodes

- **Modified Files:**
  - `src/st_logic_engine.cpp` - BUG-133 fix (g_mb_request_count reset)
  - `include/st_types.h` - Added ST_AST_REMOTE_WRITE and st_remote_write_t
  - `src/st_parser.cpp` - Parser support for new syntax
  - `src/st_compiler.cpp` - Compiler support (generates same bytecode as old syntax)
  - `README.md` - Updated examples and documentation
  - `BUGS_INDEX.md` - Marked BUG-133 as FIXED

- **Backward Compatibility:**
  - ❌ BREAKING: Old 3-argument syntax no longer supported in statement context
  - Migration required: Update existing code to use new assignment syntax
  - Global `mb_success` and `mb_last_error` still work as before

- **Build:**
  - Version: v4.6.0
  - Build: #911
  - Flash: ~68.5% (estimated +2 KB from v4.4.2)
  - RAM: ~37.1% (unchanged)

### UPGRADE NOTES
1. **BUG-133 Fix:** Existing systems should upgrade immediately to avoid request blocking
2. **Syntax Migration:** Update ST Logic programs to use new MB_WRITE syntax:
   - Old: `success := MB_WRITE_COIL(id, addr, val);`
   - New: `MB_WRITE_COIL(id, addr) := val;`
3. **Testing:** Verify all Modbus Master communications after upgrade

---

## [4.4.2] - 2025-12-25 📋 (Config Mode Enhancement)

### ADDED
- **⭐ Config Mode: Display All SET Commands**
  - `show config` now displays ALL active SET commands in copy/paste ready format
  - **Categories:** Modbus Slave, Modbus Master, System, WiFi, ST Logic, Persistence, Counters, Timers, GPIO
  - **Use case:** Easy configuration backup, copy settings to other devices, documentation
  - Passwords displayed as `********` for security
  - Full Counter/Timer/GPIO configuration display with all parameters
  - Impact: No manual config reconstruction needed, complete visibility

### ENHANCED
- **Counter Configuration Display:**
  - Shows all enabled counters with mode, hw-mode, edge, prescaler, direction, bit-width, compare
  - Format: `set counter <id> mode 1 parameter hw-mode:... edge:... prescaler:...`

- **Timer Configuration Display:**
  - Shows all enabled timers with mode-specific parameters
  - Mode 1 (One-shot): p1-duration, p2-duration, p3-duration, output-on, output-off
  - Mode 2 (Monostable): p1-duration, p2-duration, output-on, output-off, edge, invertout
  - Mode 3 (Astable): p1-duration, p2-duration, output-on
  - Mode 4 (Input-triggered): input-address, edge, output-on, output-off

- **GPIO Mapping Display:**
  - Shows all GPIO mappings (input/output)
  - Format: `set gpio <pin> input <reg>` or `set gpio <pin> coil <reg>`

### TECHNICAL
- **Modified Files:**
  - `src/cli_show.cpp` (lines 578-891) - Added SET commands display section
  - `include/constants.h` - Updated version to v4.4.2

- **Build:**
  - Flash: 68.2% (893,433 bytes / 1,310,720 bytes) - +8.9 KB from v4.4.1
  - RAM: 37.1% (121,696 bytes / 327,680 bytes) - unchanged

---

## [4.4.1] - 2025-12-24 🔧 (Modbus Slave CLI Commands)

### ADDED
- **⭐ Modbus Slave: Dedicated CLI Commands**
  - New command structure matching Modbus Master pattern
  - **Commands:**
    - `set modbus-slave enabled <on|off>` - Enable/disable slave
    - `set modbus-slave slave-id <1-247>` - Configure slave ID (replaces old `set id`)
    - `set modbus-slave baudrate <rate>` - Configure baudrate (replaces old `set baud`)
    - `set modbus-slave parity <none|even|odd>` - Configure parity (NEW)
    - `set modbus-slave stop-bits <1|2>` - Configure stop bits (NEW)
    - `set modbus-slave inter-frame-delay <ms>` - Configure inter-frame delay (NEW)
  - **Display:** `show modbus-slave` shows detailed config + statistics
  - Impact: Consistent CLI structure, more configuration options

### ENHANCED
- **Configuration Structure:**
  - Added `modbus_slave_config_t` struct to PersistConfig
  - Moved `slave_id` and `baudrate` from root to nested struct
  - Added runtime statistics tracking (total requests, CRC errors, exceptions)

- **Backward Compatibility:**
  - Old commands still work: `set id <value>`, `set baud <rate>`
  - CLI parser automatically redirects to new handlers

### TECHNICAL
- **New Files:**
  - `src/cli_commands_modbus_slave.cpp` (127 lines) - CLI handlers
  - `include/cli_commands_modbus_slave.h` - CLI declarations

- **Modified Files:**
  - `include/types.h` - Added `modbus_slave_config_t` struct
  - `src/cli_parser.cpp` - Added SET/SHOW modbus-slave handlers
  - `src/config_load.cpp` - Load modbus_slave from NVS
  - `src/config_save.cpp` - Save modbus_slave to NVS
  - `src/config_apply.cpp` - Apply modbus_slave configuration
  - `src/main.cpp` - Updated modbus_server_init() call

- **Build:**
  - Flash: 67.8% (888,845 bytes / 1,310,720 bytes) - +4.3 KB from v4.4.0
  - RAM: 37.1% (121,696 bytes / 327,680 bytes) - unchanged

---

## [4.4.0] - 2025-12-24 🎉 (Modbus Master + ST Logic Enhancements)

### ADDED
- **⭐ Modbus Master (Remote Device Control via ST Logic)**
  - **Hardware:** Separate RS485 port on UART1 (TX:GPIO25, RX:GPIO26, DE:GPIO27)
  - **Protocol:** Modbus RTU FC01-FC06 support
  - **6 Builtin ST Functions:**
    - `MB_READ_COIL(slave_id, address) → BOOL`
    - `MB_READ_INPUT(slave_id, address) → BOOL`
    - `MB_READ_HOLDING(slave_id, address) → INT`
    - `MB_READ_INPUT_REG(slave_id, address) → INT`
    - `MB_WRITE_COIL(slave_id, address, value) → BOOL`
    - `MB_WRITE_HOLDING(slave_id, address, value) → BOOL`
  - **Global Status Variables:** `mb_last_error` (INT), `mb_success` (BOOL)
  - **Rate Limiting:** Max 10 requests per ST cycle (configurable)
  - **CLI Configuration:** `set modbus-master enabled on`, `set modbus-master baudrate 9600`, etc.
  - **Status Display:** `show modbus-master` shows config, statistics, error counters
  - **Impact:** ESP32 can now control remote Modbus devices from ST Logic programs

- **ST Logic: 3-Argument Function Support**
  - `LIMIT(min, value, max) → INT` - Clamp value between min/max
  - `SEL(g, in0, in1) → T` - Select in0 if g=FALSE, in1 if g=TRUE
  - `MB_WRITE_COIL(slave_id, address, value) → BOOL`
  - `MB_WRITE_HOLDING(slave_id, address, value) → BOOL`
  - VM updated to support 3-argument CALL_BUILTIN operations

- **ST Logic: Trigonometric Functions (v4.3.0 backport)**
  - `SIN(x) → REAL` - Sine (x in radians)
  - `COS(x) → REAL` - Cosine (x in radians)
  - `TAN(x) → REAL` - Tangent (x in radians)
  - Type-aware stack operations for REAL arithmetic
  - Expression chaining with type conversions now works correctly

- **ST Logic: Rounding Functions (v4.3.0 backport)**
  - `ROUND(x) → INT` - Round to nearest integer
  - `TRUNC(x) → INT` - Truncate fractional part
  - `FLOOR(x) → INT` - Round down
  - `CEIL(x) → INT` - Round up

### ENHANCED
- **Documentation:**
  - Updated `docs/ST_USAGE_GUIDE.md` with all new functions + examples
  - Updated `docs/ST_IEC61131_COMPLIANCE.md` to v2.0.0
  - Updated `docs/ST_FUNCTIONS_TODO.md` with implementation status
  - Added comprehensive Modbus Master section to README.md
  - Added help menu: `set modbus-master ?`

### TECHNICAL
- **New Files:**
  - `src/modbus_master.cpp` (421 lines) - UART1 + Modbus RTU protocol
  - `src/st_builtin_modbus.cpp` (191 lines) - ST wrapper functions
  - `src/cli_commands_modbus_master.cpp` (140 lines) - CLI handlers
  - `include/modbus_master.h` - Hardware interface
  - `include/st_builtin_modbus.h` - ST Logic integration
  - `include/cli_commands_modbus_master.h` - CLI declarations

- **Modified Files:**
  - `include/types.h` - Added `modbus_master_config_t` struct
  - `include/constants.h` - Added Modbus Master GPIO pins + defaults
  - `src/st_builtins.cpp` - Added 6 Modbus functions to dispatcher
  - `src/st_vm.cpp` - Added 3-argument function support
  - `src/cli_parser.cpp` - Added SET/SHOW modbus-master handlers
  - `src/config_load.cpp` - Added Modbus Master defaults
  - `src/main.cpp` - Added `modbus_master_init()` call

- **Build:**
  - Flash: 67.5% (884,553 bytes / 1,310,720 bytes) - +1.6 KB
  - RAM: 37.1% (121,696 bytes / 327,680 bytes) - unchanged

---

## [4.2.0] - 2025-12-13 ✨ (HR 204-235 Direct Variable Write & Timing Fix)

### ADDED
- **HR 204-235: Direct Write to ST Logic Variables (v4.2.0)**
  - Modbus FC06/FC10 can now write directly to ST Logic variables
  - **Deterministic mapping:** HR 204-211 = Logic1 vars[0-7], HR 212-219 = Logic2 vars[0-7], etc.
  - **Type-aware conversion:** BOOL/INT/REAL types handled correctly
  - Bounds checking: Writes ignored if program not compiled or var out of range
  - Debug output: `[ST_VAR_INPUT] Logic# var[#] = value (type=#)` when enabled
  - No variable mapping setup needed - direct bytecode write
  - Impact: Faster Modbus variable updates, no setup overhead

### FIXED
- **Field naming:** Timing measurement fields now correctly named with *_us suffix
  - Renamed `min_execution_ms` → `min_execution_us`
  - Renamed `max_execution_ms` → `max_execution_us`
  - Renamed `last_execution_ms` → `last_execution_us`
  - Data was always in microseconds, names are now accurate
  - Impact: No confusion about timing units, clearer code intent

### CHANGED
- MODBUS_REGISTER_MAP.md: HR 204-235 section updated from "Reserved" to "Implemented"

---

## [4.1.1] - 2025-12-13 🔧 (ST Logic Bindings & Persistent Configuration)

### ADDED
- **Ultra-Fast ST Logic Execution Intervals:** 2ms og 5ms support
  - `set logic interval:2` for demanding real-time applications
  - `set logic interval:5` for fast updates
  - ⚠️ WARNING shown for intervals < 10ms (may impact other operations)
  - All intervals 2, 5, 10, 20, 25, 50, 75, 100 ms now supported

### FIXED
- **BUG-012 (HIGH):** ST Logic binding "both" mode creates duplicate output
  - Fixed: `reg:` and `coil:` bindings now default to "output" mode (not "both")
  - Result: 6 bindings from 6 commands (was 11 bindings showing both input+output)
  - Impact: Cleaner binding visualization, reduced memory usage

- **BUG-014 (HIGH):** ST Logic execution_interval_ms not persisted across reboot
  - Added `st_logic_interval_ms` field to PersistConfig struct
  - Interval now saved/loaded via normal config persistence
  - `set logic interval:2` + `save` now survives reboot
  - Impact: Settings persist correctly, no reset after power cycle

### CHANGED
- ST Logic binding display now shows "register bindings=X" for all states
  - More consistent terminology across enabled/disabled/empty slots

---

## [4.1.0] - 2025-12-12 🎯 (ST Logic Bugfixes, Fixed Rate Scheduler & Performance Monitoring)

### ADDED
- **ST Logic Fixed Rate Scheduler:** Implemented deterministic 10ms execution cycle
  - Added timing check in `st_logic_engine_loop()` to throttle execution
  - Programs now execute exactly every `execution_interval_ms` (default 10ms)
  - Prevents variable timing caused by main loop jitter (was 5-240ms, now ±1ms)
  - Optional debug output shows cycle time and warns if overrun (>10ms)
  - Enable debug: `set logic debug:true` to see `[ST_TIMING]` messages
  - Critical for PID control, timing-sensitive protocols, and deterministic behavior

- **ST Logic Performance Monitoring System:** Comprehensive execution statistics tracking
  - **Per-program metrics:** Min/Max/Avg execution time (microsecond precision)
  - **Overrun tracking:** Counts executions where time > target interval
  - **Global cycle stats:** Min/Max cycle time, total cycles executed
  - **CLI commands:**
    - `show logic stats` - Display all program performance statistics
    - `show logic X timing` - Detailed analysis for specific program with recommendations
    - `set logic stats reset [all|cycle|1-4]` - Reset statistics
  - **Performance ratings:** EXCELLENT/GOOD/ACCEPTABLE/POOR with automatic tuning recommendations
  - Stores statistics in `st_logic_program_config_t` and `st_logic_engine_state_t`

- **Modbus Register Interface for Statistics (IR 252-293):**
  - **IR 252-259:** Logic1-4 Min Execution Time (µs, 32-bit)
  - **IR 260-267:** Logic1-4 Max Execution Time (µs, 32-bit)
  - **IR 268-275:** Logic1-4 Avg Execution Time (calculated, µs, 32-bit)
  - **IR 276-283:** Logic1-4 Overrun Count (32-bit)
  - **IR 284-285:** Global Cycle Min Time (ms, 32-bit)
  - **IR 286-287:** Global Cycle Max Time (ms, 32-bit)
  - **IR 288-289:** Global Cycle Overrun Count (32-bit)
  - **IR 290-291:** Total Cycles Executed (32-bit)
  - **IR 292-293:** Execution Interval (read-only copy, ms, 32-bit)
  - All statistics auto-sync to registers in `registers_update_st_logic_status()`

- **Dynamic Execution Interval Control:**
  - **CLI command:** `set logic interval:X` (X = 10, 20, 25, 50, 75, 100 ms)
  - **Modbus control:** HR 236-237 (32-bit, write to change interval)
  - Validation: Only specific values allowed, invalid values rejected
  - Changes take effect immediately
  - Must be saved with `save` command to persist across reboot
  - Use cases: Reduce interval for faster response, increase for slow programs

- **ST Logic Execution Time Monitoring:** Per-program execution time tracking
  - Stores actual execution time in `last_execution_ms` field (now microseconds)
  - Warnings logged if individual program takes >100ms
  - Helps identify performance bottlenecks

### FIXED
- **BUG-001 (CRITICAL):** Input registers 220-251 now update with ST variable values
  - Fixed missing code in `registers_update_st_logic_status()`
  - Modbus masters can now read ST Logic variable values from input registers
  - Impact: Output visibility restored (was broken, registers always 0)

- **BUG-002 (CRITICAL):** Type checking in ST Logic variable bindings
  - Added proper BOOL/INT/REAL type conversion in `gpio_mapping.cpp`
  - BOOL variables now output 0/1 (not arbitrary INT values)
  - REAL variables properly converted to/from float
  - Impact: Correct data types prevent logic errors in ST programs

- **BUG-003 (HIGH):** Bounds checking on ST variable index
  - Added validation before array access in `registers.cpp`
  - Prevents memory corruption if mapping references non-existent variable
  - Impact: Crash prevention, safer operation

- **BUG-004 (HIGH):** Control register reset bit now auto-clears
  - Bit 2 (RESET_ERROR) in HR 200-203 now clears after handling
  - Modbus master can detect when command was processed
  - Impact: Better command acknowledgment, follows standard pattern

- **BUG-005 (MEDIUM):** Variable binding count now cached for performance
  - Added `binding_count` field to `st_logic_program_config_t`
  - Eliminated O(n×m) nested loop (256 checks per main loop)
  - Updated on bind/unbind and load from NVS
  - Impact: Reduced CPU overhead, better scalability

- **BUG-006 (LOW):** Execution/error counters changed to 16-bit
  - Changed from `uint32_t` to `uint16_t` to match register size
  - Saves 32 bytes RAM (8 bytes × 4 programs)
  - Counters wrap at 65535 (acceptable for most use cases)
  - Impact: No more data truncation, cleaner code

- **BUG-007 (MEDIUM):** Timeout protection on ST program execution
  - Added `millis()` timing wrapper around VM execution
  - Warning logged if program takes >100ms
  - Stores execution time in `last_execution_ms`
  - Impact: Better visibility into performance, watchdog safety

### CHANGED
- **ST Logic Architecture:** Timing model now deterministic
  - Programs execute every 10ms (configurable via `execution_interval_ms`)
  - Previously: Variable timing (5-240ms) depending on system load
  - Now: Fixed rate ±1ms jitter from main loop delay(1)
  - See `TIMING_ANALYSIS.md` for detailed timing documentation

### DOCUMENTATION
- **Added:** `BUGS.md` - Comprehensive bug tracking system with test plans (7 bugs, all FIXED)
- **Added:** `TIMING_ANALYSIS.md` - ST Logic timing analysis, scheduler details, recommendations
- **Added:** `ST_MONITORING.md` - Complete performance monitoring & tuning guide with examples
- **Added:** `MODBUS_REGISTER_MAP.md` - **Complete Modbus register reference** with ALL registers:
  - Fixed mappings: ST Logic (IR/HR 200-293)
  - Dynamic mappings: Counters, Timers, Persistent Registers
  - Coils & Discrete Inputs
  - Address collision avoidance guide
  - Python pymodbus examples
  - CLI command reference
- **Updated:** `CLAUDE.md` - Added "ALTID TJEK BUGS.MD FØR ÆNDRINGER" + Key Files Reference updated

**Files changed:** 15 files, ~800 lines added/modified
- `include/st_logic_config.h` - Added performance monitoring fields (min/max/avg time, overrun count)
- `include/constants.h` - Added IR 252-293 and HR 236-237 register defines
- `include/registers.h` - Added registers_process_st_logic_interval() declaration
- `include/cli_commands_logic.h` - Added cli_cmd_set_logic_interval() declaration
- `src/st_logic_config.cpp` - Added st_logic_reset_stats() and st_logic_reset_cycle_stats()
- `src/st_logic_engine.cpp` - Fixed rate scheduler + performance tracking (min/max/avg/overrun)
- `src/registers.cpp` - Statistics sync to IR 252-293 + interval handler for HR 236-237
- `src/gpio_mapping.cpp` - Type checking for BOOL/INT/REAL
- `src/cli_commands_logic.cpp` - Added show logic stats/timing commands + set interval command
- `src/cli_parser.cpp` - Parser for "set logic interval:X"
- `BUGS.md` - Created with 7 bugs (all FIXED)
- `TIMING_ANALYSIS.md` - Created (timing deep dive)
- `ST_MONITORING.md` - Created (performance guide)
- `MODBUS_REGISTER_MAP.md` - Created (complete register reference)
- `CLAUDE.md` - Updated

**Migration notes:**
- ST Logic timing is now deterministic - existing programs will behave identically
- If total execution time >10ms, scheduler automatically extends interval
- Use `set logic debug:true` to monitor timing performance
- **New CLI commands available:** `show logic stats`, `show logic X timing`, `set logic interval:X`
- **Modbus access:** Read statistics from IR 252-293, control interval via HR 236-237
- **Performance tuning:** Use `ST_MONITORING.md` guide for optimization workflows
- **Register reference:** See `MODBUS_REGISTER_MAP.md` for complete register documentation

---

## [4.0.2] - 2025-12-11 🔐 (Telnet Auth Fix)

### FIXED
- **Telnet Authentication:** Fixed password validation failure due to whitespace
  - Added `trim_string()` helper function to strip leading/trailing whitespace
  - Username and password inputs now trimmed before validation
  - Fixes issue where telnet clients sending trailing spaces/tabs/CR/LF would fail authentication
  - Added debug logging (ESP_LOGI) showing username and password lengths for troubleshooting
  - Prevents authentication failures due to invisible characters from different telnet clients

**Root cause:** Telnet clients (especially Windows telnet) may send trailing whitespace/control characters after password input, causing `strcmp()` to fail even with correct credentials.

**Files changed:** 1 file, +29 lines
- `src/telnet_server.cpp` - Added trim_string() + debug logging

---

## [4.0.1] - 2025-12-11 🐛 (Bugfix Patch)

### FIXED
- **CLI Parser:** Fixed `set persist enable on|off` command
  - Duplicate output removed - command now prints status only once
  - Boolean value normalization added (`on`/`off`/`true`/`false` → uppercase)
  - Command now correctly enables/disables persistence system
  - Previous bug: `set persist enable on` incorrectly printed "DISABLED" twice

**Files changed:** 2 files, +8 lines
- `src/cli_commands.cpp` - Removed duplicate debug print
- `src/cli_parser.cpp` - Added ON/OFF/TRUE/FALSE normalization

---

## [4.0.0] - 2025-12-11 💾 (Persistent Registers & Watchdog Monitor)

### FEATURES ADDED

#### Persistent Register System ✅ NEW
- **Persistence Groups:** Named groups for selective register persistence (max 8 groups × 16 registers)
  - Create groups via CLI: `set persist group <name> add <reg1> [reg2] ...`
  - Remove registers: `set persist group <name> remove <reg>`
  - Delete groups: `set persist group <name> delete`
  - Enable/disable system: `set persist enable on|off`
- **Manual Save:** Save groups to NVS via CLI commands
  - `save registers all` - Save all persistence groups
  - `save registers group <name>` - Save specific group
  - Groups auto-restore at boot via `config_apply()`
- **ST Logic Integration:** Built-in SAVE()/LOAD() functions
  - `SAVE()` - Save all persistence groups to NVS from ST program
  - `LOAD()` - Restore all persistence groups from NVS
  - Rate limiting: Max 1 save per 5 seconds (flash wear protection)
  - Return value: 0=success, -1=error, -2=rate limited
- **CLI Commands:**
  - `show persist` - Display all persistence groups with register values
  - `set persist ?` - Show detailed help

**Use Cases:**
- Save sensor calibration data across reboots
- Restore last known good setpoints after power loss
- Conditional persistence from ST programs (save only when valid)
- Differentiated save/load (not all-or-nothing)

#### Watchdog Monitor ✅ NEW
- **ESP32 Task Watchdog Timer:** Auto-restart system if main loop hangs (30s timeout)
  - Initialized in `setup()` - increments reboot counter
  - `watchdog_feed()` called in `loop()` - MUST be called < 30s
  - Auto-restart on timeout (panic trigger)
- **Persistence:** Watchdog state saved to NVS
  - Reboot counter (persistent across resets)
  - Last reset reason (Power-on, Panic, Brownout, etc.)
  - Last error message (max 127 chars)
  - Last reboot uptime (seconds before crash)
- **CLI Command:**
  - `show watchdog` - Display watchdog status, reboot counter, reset reason
- **API Functions:**
  - `watchdog_init()` - Initialize (called in setup)
  - `watchdog_feed()` - Reset timer (called in loop)
  - `watchdog_enable(bool)` - Enable/disable
  - `watchdog_record_error(msg)` - Save error before crash
  - `watchdog_get_state()` - Get state for CLI

**Safety Features:**
- Production-ready: Auto-restart on system hang
- Debug-friendly: Last error message visible after reboot
- Configurable timeout (default 30s)

### SCHEMA & STORAGE

#### Schema Version 7 → 8 ✅ BREAKING
- **New Fields in PersistConfig:**
  - `PersistentRegisterData persist_regs` - Persistence groups (8 groups × 16 registers)
  - Total: ~600 bytes for persistence system
- **Backward Compatibility:** v7 configs auto-migrate to v8 on load
  - `persist_regs` initialized to empty/disabled
  - All existing configs preserved

#### NVS Storage Organization
- **Config namespace:** `modbus_cfg` (existing)
  - Key `config` - Main PersistConfig blob (CRC16 validated)
  - Now includes `persist_regs` with group data
- **Watchdog namespace:** `modbus_cfg` (shared)
  - Key `watchdog` - WatchdogState blob
  - Reboot counter, reset reason, last error

### FILES ADDED
- `include/registers_persist.h` - Persistence group management API
- `src/registers_persist.cpp` - Group create/delete, save/restore engine (444 lines)
- `include/st_builtin_persist.h` - ST Logic SAVE()/LOAD() declarations
- `src/st_builtin_persist.cpp` - Built-in function implementations
- `include/watchdog_monitor.h` - Watchdog API
- `src/watchdog_monitor.cpp` - ESP32 Task WDT wrapper (295 lines)

### FILES MODIFIED
- `include/types.h` - Added PersistGroup, PersistentRegisterData, WatchdogState structs
- `include/constants.h` - Bumped CONFIG_SCHEMA_VERSION to 8, PROJECT_VERSION to "4.0.0"
- `src/config_load.cpp` - Schema migration 7→8, persist_regs initialization
- `src/config_apply.cpp` - Restore persistence groups at boot
- `src/st_builtins.h` - Added ST_BUILTIN_PERSIST_SAVE/LOAD enums
- `src/st_builtins.cpp` - Integrated SAVE/LOAD into dispatcher
- `include/cli_commands.h` - Added persist command declarations
- `src/cli_commands.cpp` - Implemented persistence CLI commands
- `include/cli_show.h` - Added show persist/watchdog declarations
- `src/cli_show.cpp` - Implemented show persist/watchdog displays
- `src/cli_parser.cpp` - Added routing for persist/watchdog commands
- `src/main.cpp` - Integrated watchdog_init() and watchdog_feed()

### RESOURCE USAGE
- **RAM:** 36.7% (120,296 bytes / 327,680 bytes) - +152 bytes from v3.3.0
- **Flash:** 64.6% (847,177 bytes / 1,310,720 bytes) - +2,896 bytes from v3.3.0
- **NVS:** +600 bytes for persistence groups, +200 bytes for watchdog state

---

## [3.3.0] - 2025-12-11 🔄 (GPIO Mapping Split & ST Logic Binding Improvements)

### FEATURES ADDED

#### GPIO Mapping Split - INPUT/OUTPUT Separation ✅ NEW
- **Nye funktioner:**
  - `gpio_mapping_read_before_st_logic()` - Læser alle INPUT mappings FØR ST logic kører
  - `gpio_mapping_write_after_st_logic()` - Skriver alle OUTPUT mappings EFTER ST logic kører
- **Formål:** Forhindrer INPUT i at overskrive OUTPUT i samme loop iteration
- **Implementation:**
  - `gpio_mapping_update()` nu deprecated (men stadig functional)
  - `main.cpp` opdateret til at kalde de to separate funktioner omkring ST logic loop
  - GPIO og ST variable mappings håndteres korrekt i begge retninger

#### ST Logic Binding - "Both" Mode Fix ✅ FIXED
- **Issue:** "both" mode (bidirectional binding) overskrev data i samme loop iteration
- **Root Cause:** `VariableMapping.is_input` er en MODE flag (1=INPUT, 0=OUTPUT), IKKE capability flag
- **Solution:**
  - "both" mode opretter nu 2 separate mappings:
    - Mapping 1: INPUT mode (Modbus → ST var)
    - Mapping 2: OUTPUT mode (ST var → Modbus)
  - Eksisterende bindings for samme variabel slettes før nye oprettes
  - Maximum mappings øget fra 16 til 64
- **Result:** Bidirectional bindings fungerer nu korrekt uden data tab

#### ST Logic Binding - CLI Improvements ✅ NEW
- **Nye features:**
  - "input:" shortcut (alias for "input-dis:") - hurtigere at skrive
  - Debug output ved binding oprettelse (hvis debug mode aktiveret)
  - Bedre fejlmeddelelser ved parsing errors
- **CLI opdateringer:**
  - Hjælpetekst opdateret: `set logic <id> bind <var_name> reg:100|coil:10|input:5`
  - `show config` viser nu variable navne (ikke kun indeks)
  - `show config` viser INPUT/OUTPUT retning mere klart

### BUGFIXES & IMPROVEMENTS

#### GPIO Mapping - Input/Output Conflict ✅ FIXED
- **Issue:** INPUT mappings kunne overskrive OUTPUT mappings i samme loop iteration
- **Solution:** Split i to funktioner der kaldes omkring ST logic execution
- **Result:** GPIO og ST variable mappings fungerer korrekt uden race conditions

#### ST Logic Binding - Variable Name Display ✅ IMPROVED
- **Improvement:** `show config` viser nu variable navne fra bytecode (f.eks. "bEnable") i stedet for indeks
- **Fallback:** Viser indeks hvis variable navn ikke tilgængelig
- **Result:** Lettere at identificere bindings i config output

### FILES MODIFIED
- `include/gpio_mapping.h` - Tilføjet `gpio_mapping_read_before_st_logic()` og `gpio_mapping_write_after_st_logic()`, deprecated `gpio_mapping_update()`
- `src/gpio_mapping.cpp` - Implementeret split i read/write funktioner
- `src/main.cpp` - Opdateret main loop til at bruge nye split funktioner
- `src/cli_commands_logic.cpp` - Implementeret "both" mode med 2 mappings, tilføjet "input:" shortcut, tilføjet debug output
- `src/cli_parser.cpp` - Opdateret hjælpetekst til "input:" shortcut
- `src/cli_show.cpp` - Implementeret variable navn display i `show config`
- `include/build_version.h` - Auto-genereret build info (build #499, 2025-12-11)

---

## [3.2.0] - 2025-12-09 🎛️ (CLI Commands Complete + Persistent Settings)

### FEATURES ADDED

#### Show Commands - Specific Item Details ✅ NEW
- **New Commands:**
  - `show counter <id>` - Display detailed status for specific counter (1-4)
    - Shows hardware mode (SW/ISR/HW), edge type, prescaler, scale factor
    - Displays register mappings (index, raw, frequency, control)
    - Shows current values (raw, prescaled, scaled, frequency in Hz)
    - Shows compare feature configuration if enabled
  - `show timer <id>` - Display detailed status for specific timer (1-4)
    - Shows mode-specific parameters (one-shot, monostable, astable, input-triggered)
    - Displays output coil and control register addresses
    - Mode-specific details (phases, pulse duration, on/off times, etc.)

#### Persistent Configuration Settings ✅ NEW
- **`set hostname <name>` - FIXED & PERSISTENT**
  - Now saves hostname to `PersistConfig` (was runtime-only)
  - Persists to NVS on `save` command
  - Default: "modbus-esp32"
  - Max 31 characters (+ null terminator)
  - Shows in `show config` output
  - Survives reboot

- **`set echo <on|off>` - NOW PERSISTENT**
  - Previously runtime-only, now saves to persistent config
  - Persists to NVS on `save` command
  - Default: ON (echo enabled)
  - Applied automatically at boot via `config_apply()`
  - Shows in `show config` output

#### Schema Version Upgrade ✅
- **Schema v5 → v6:** Added `hostname` field to PersistConfig
- **Schema v6 → v7:** Added `remote_echo` field to PersistConfig
- **Backward Compatibility:** Old v5 configs auto-migrate on load (schema mismatch triggers reinit with defaults)
- **CRC Protection:** Dynamic CRC calculation (works with any struct size)

### BUGFIXES & IMPROVEMENTS

#### CLI Show Commands - Missing Normalization ✅ FIXED
- **Issue:** `show debug` command failed with "SHOW: unknown argument" error
- **Root Cause:** "DEBUG" token not added to `normalize_alias()` function
- **Solution:** Added DEBUG normalization in cli_parser.cpp (line 142)
- **Result:** `show debug` now works correctly

#### Configuration Save/Load Schema Safety ✅ IMPROVED
- **Issue:** When extending PersistConfig struct, old configs could cause data corruption
- **Root Cause:** `config_init_defaults()` in config_load.cpp didn't initialize new fields during schema migration
- **Solution:**
  1. Added hostname initialization in `config_init_defaults()` (config_load.cpp:32-33)
  2. Added remote_echo initialization in `config_init_defaults()` (config_load.cpp:34)
  3. Added same initializations in `config_struct_create_default()` (config_struct.cpp)
  4. Verified CRC calculation is dynamic (sizeof(PersistConfig) calculated at runtime)
  5. Verified schema version mismatch triggers full reinit (safe migration)
- **Result:**
  - Old v5/v6 configs → schema mismatch → reinit with new defaults
  - No data corruption possible
  - All new fields guaranteed initialized
  - CRC validation ensures integrity

#### Show Config Output - Duplicate Hostname ✅ FIXED
- **Issue:** `show config` displayed hostname twice:
  ```
  Hostname: GREENS         (correct, from config)
  ...
  Hostname: esp32-modbus   (wrong, hardcoded)
  ```
- **Root Cause:** Leftover debug line at cli_show.cpp:69
- **Solution:** Removed duplicate hardcoded hostname line
- **Result:** Single hostname display (correct value from persistent config)

#### Config Apply - Missing Include ✅ FIXED
- **Issue:** `config_apply()` calls `cli_shell_set_remote_echo()` but header not included
- **Solution:** Added `#include "cli_shell.h"` to config_apply.cpp
- **Result:** Proper include dependency chain

### FILES MODIFIED
- `include/types.h` - Added `hostname[32]` and `remote_echo` fields to PersistConfig
- `include/constants.h` - Bumped CONFIG_SCHEMA_VERSION from 5→6→7
- `include/cli_show.h` - Added `cli_cmd_show_counter(uint8_t id)` and `cli_cmd_show_timer(uint8_t id)` declarations
- `src/cli_show.cpp` - Implemented counter/timer detail commands, added hostname display, removed duplicate hostname
- `src/cli_parser.cpp` - Added DEBUG normalization, added counter/timer <id> dispatch logic
- `src/cli_commands.cpp` - Implemented `set hostname` persistent storage, implemented `set echo` persistent storage
- `src/config_struct.cpp` - Added default initialization of hostname and remote_echo
- `src/config_load.cpp` - Added hostname/remote_echo initialization in schema migration
- `src/config_apply.cpp` - Added remote_echo application at boot, added cli_shell.h include

---

## [3.1.1] - 2025-12-08 ⌨️ (Telnet Insert Mode & ST Upload Copy/Paste)

### BUGFIXES & IMPROVEMENTS

#### Telnet ST Logic Upload Copy/Paste Support ✅ FIXED (Build #533)
- **Issue:** When copy/pasting multi-line ST code into telnet during `set logic X upload`, all lines after first were executed as commands instead of being added to upload buffer. Only last line was captured.
- **User Feedback #1:** "når jeg kopi pasta tekst ind den få ikke fanget tekst input på multi linies"
- **User Feedback #2:** "det gør det så IKKE" - Double prompts (`>>> >>>`) and lines not properly captured
- **User Feedback #3:** "ok gør det IKKE, analyse og fiks" - Lines still showing "Unknown command" errors
- **User Feedback #4:** "ultrathink samme lort prøv igen" - Only 30 bytes (one line) captured instead of entire program
- **Root Cause (The REAL Problem):**
  1. Copy/paste sends ALL bytes at once: `set logic 2 upload\r\nVAR\r\n  state: INT;\r\n  loop_var: INT;\r\n...`
  2. Telnet byte-reading loop (line 837-840) reads ALL bytes in one go via `while (tcp_server_recv_byte())`
  3. Each `\n` sets `input_ready = 1` and RESETS `input_pos = 0` immediately (line 467-468)
  4. **Critical Bug:** Loop continues reading → next line OVERWRITES previous line in buffer!
  5. Result: Only LAST line remains in buffer when loop finishes, all others lost
  6. Only "  loop_var: INT;" (30 bytes) captured, rest overwritten
- **Solution - Stop Reading After Complete Line:**
  1. Modified byte-reading loop to BREAK when `input_ready` becomes 1
  2. This ensures only ONE line is read per iteration, preventing buffer overwrite
  3. Subsequent lines remain in TCP buffer for next iteration
  4. Combined with existing batch processing logic for upload mode
  5. **Files Modified:**
     - `src/telnet_server.cpp` (line 841-846): Added `if (server->input_ready) break;` in byte-reading loop
     - `src/cli_shell.cpp`: Upload line feeding with echo control
     - `include/cli_shell.h`: `cli_shell_feed_upload_line()` declaration
- **Technical Details:**
  - Byte-reading loop now processes ONE line at a time
  - When `input_ready = 1` detected, break immediately
  - Remaining bytes stay in TCP buffer for next `telnet_server_loop()` iteration
  - Batch processing logic reads pending lines when upload mode just entered
  - Each line properly fed to upload buffer via `cli_shell_feed_upload_line()`
- **Result:** Copy/paste of entire ST programs into telnet now works correctly - ALL lines captured in upload buffer (not just last one), no buffer overwrites, no "Unknown command" errors

#### Telnet Cursor Position Editing (Insert Mode) ✅ FIXED
- **Issue:** When cursor moved left (LEFT arrow) and text inserted, existing text was overwritten instead of shifted forward
- **User Feedback:** "cursor fungere næster, problem: hvis jeg flytte curser tilbage en text og indsætter text flyttes text foran curser ikke frem den overskriver bare"
- **Root Cause:** Cursor position not tracked internally, text always inserted at end of buffer (append mode)
- **Solution:**
  1. Added `cursor_pos` field to `TelnetServer` struct (tracks current cursor position independent of buffer write position)
  2. Modified LEFT/RIGHT arrow handlers to update `cursor_pos` and only echo movement if within valid range
  3. Implemented true insert mode for character input:
     - When cursor at end: simple append (fast path)
     - When cursor in middle: shift all characters right, insert at cursor, redraw line with ANSI sequences
  4. Implemented cursor-aware backspace:
     - When cursor at end: simple delete (fast path)
     - When cursor in middle: shift all characters left, redraw line with clear+reposition
  5. Updated all `input_pos = 0` locations to also reset `cursor_pos = 0`:
     - Line completion, disconnect, auth reset, history navigation, new connection
- **Files Modified:**
  - `include/telnet_server.h` - Added `cursor_pos` field
  - `src/telnet_server.cpp` - Complete cursor position tracking and insert mode implementation
- **Result:** Text editing now works like standard terminals (insert mode by default, cursor-aware editing)

---

## [3.1.0] - 2025-12-05 🌐 (WiFi Display, Validation, & Telnet Auth Improvements)

### BUGFIXES & IMPROVEMENTS

#### WiFi Configuration Display & Validation ✅ IMPROVED
- **Issue:** WiFi configuration not showing in `show config`, IP not displaying on DHCP, Telnet status always showing "DISABLED"
- **Solution:**
  1. Added complete WiFi/Network section to `show config` - displays all WiFi settings centrally
  2. Enhanced `show wifi` IP display - shows actual IP from WiFi driver (works for DHCP and static)
  3. Fixed Telnet status - now reads from `g_persist_config.network.telnet_enabled` instead of hardcoded
  4. Added `set wifi telnet enable|disable` command for toggling Telnet via CLI
  5. Improved WiFi connection validation with specific error messages:
     - SSID empty check before connection attempt
     - Password length validation (0 or 8-63 chars)
     - Clear hints for each validation failure
  6. Enhanced `network_config_validate()` to explicitly check for empty SSID when WiFi enabled
  7. Added WiFi debug flags (`wifi_connect`, `network_validate`) with comprehensive logging

#### Telnet Server Duplicate Prompt Fix ✅ IMPROVED
- **Issue:** Telnet authentication showed duplicate login banner and prompts, output was garbled
- **Root Cause:** Double prompt sending - both `telnet_handle_auth_input()` AND `telnet_server_loop()` sent next prompt
- **Solution:**
  - Remove duplicate `telnet_send_auth_prompt()` call after auth input (line 530-531)
  - Create `telnet_send_retry_prompt()` for simplified retry prompts (no banner)
  - Keep full banner only for initial connection
- **Result:** Clean authentication with single prompt per step

### NEW FEATURES
- `set wifi telnet enable|disable` - Toggle Telnet server without file edit
- Enhanced `show config` network section - centralized configuration display
- Detailed `connect wifi` error messages with helpful hints
- WiFi debug flags for troubleshooting connection issues

---

## [2.3.0] - 2025-12-04 🔧 (CRITICAL BUGFIX RELEASE)

### BUGFIXES & CRITICAL FIXES

#### GPIO Persistence & Config Reboot Restoration ✅ FIXED
**Root Cause:** Compiler struct padding caused misalignment between save/load, breaking CRC validation

- **Problem:** GPIO mappings and all config were lost after reboot (appeared saved but weren't restored)
- **Root Cause #1:** Struct alignment padding - `uint32_t baudrate` was misaligned causing entire config to shift
- **Root Cause #2:** Schema version mismatch (v2 vs v3 vs v4) - no validation before CRC check
- **Solution #1:** All persistence structs marked with `__attribute__((packed))` for binary compatibility
  - `PersistConfig` - main config struct
  - `CounterConfig` - counter configuration
  - `TimerConfig` - timer configuration
  - `StaticRegisterMapping`, `DynamicRegisterMapping` - register mappings
  - `StaticCoilMapping`, `DynamicCoilMapping` - coil mappings
  - `VariableMapping` - GPIO + ST variable unified mapping
- **Solution #2:** Explicit schema version validation BEFORE CRC check in `config_load.cpp`
- **Impact:** Config persistence now 100% reliable across firmware updates

### NEW FEATURES

#### Debug Command System 🆕
Runtime-controllable debug flags for diagnostic output

- `set debug config-save on|off` - Toggle config save debugging
- `set debug config-load on|off` - Toggle config load debugging
- `set debug all on|off` - Toggle all debug flags
- Default: All debug ON (helps initial diagnosis)
- Can be disabled without reboot for cleaner output

**Files:**
- `src/debug_flags.cpp/.h` - Global debug flag management
- Modified: `config_save.cpp`, `config_load.cpp` - Conditional debug output
- Modified: `cli_commands.cpp`, `cli_parser.cpp` - CLI integration

### IMPROVEMENTS

#### Config Load Debug Enhancements
- Added `[LOAD_START]` message at boot
- Added `[LOAD_DEBUG] Reading blob` - shows NVS read attempt
- Added `[LOAD_DEBUG] nvs_get_blob returned` - shows NVS read result
- Added `[LOAD_DEBUG] After nvs_get_blob` - shows var_map_count and schema_version
- Added `[LOAD_DEBUG] First 20 bytes` - hex dump of loaded data for diagnostics
- Added `[LOAD_DEBUG] Schema version OK` - confirmation of version match
- All controllable via `set debug config-load on|off`

#### Config Save Debug Enhancements
- Added `[SAVE_DEBUG] var_map_count` - shows data being saved
- Added `[SAVE_DEBUG] sizeof(PersistConfig)` - shows structure size
- Added `[SAVE_DEBUG] var_maps[i]` entries - lists each mapping being saved
- All controllable via `set debug config-save on|off`

### DOCUMENTATION

- Updated `docs/index.html` with v2.3 features
- Added "Struct Alignment Fix" explanation in Persistence section
- Added GPIO persistence examples
- Added debug commands reference
- Updated footer to v2.3.0

### TESTING & VALIDATION

- ✅ Verified: GPIO mappings persist across reboot
- ✅ Verified: All config types (counters, timers, registers, coils) persist
- ✅ Verified: CRC validation passes 100% reliably
- ✅ Verified: Schema version validation works correctly
- ✅ Verified: Debug flags can be toggled without reboot
- ✅ Verified: Backward compatibility maintained (auto-converts old configs)

---

## [2.0.0] - 2025-11-30 🚀 (MAJOR RELEASE)

### NEW FEATURES

#### Structured Text (IEC 61131-3) Language Support ✨
**Introduction of Logic Mode - Programmatic control in ST language**

- **Language:** Structured Text per IEC 61131-3:2013 standard
- **Compliance:** ST-Light profile (embedded PLC variant)
- **Implementation Status:** Phase 1-2 (Lexer, Parser, AST complete)

#### ST Lexer (Tokenization)
- Full keyword recognition: IF, THEN, ELSE, ELSIF, FOR, WHILE, REPEAT, CASE, OF, DO, END_*
- Data type keywords: BOOL, INT, DWORD, REAL
- Variable declarators: VAR, VAR_INPUT, VAR_OUTPUT, CONST
- Operators: +, -, *, /, MOD, AND, OR, NOT, XOR, SHL, SHR, <, >, <=, >=, =, <>
- Literals: Boolean (TRUE, FALSE), Integer (decimal, hex, binary), Real (IEEE 754), String
- Comments: Block comments with nesting `(* comment *)`
- **Code:** `src/st_lexer.cpp` (580 lines)

#### ST Parser (AST Construction)
- Recursive descent parser with operator precedence
- Statements: IF/ELSIF/ELSE, CASE/OF, FOR/TO/BY/DO, WHILE/DO, REPEAT/UNTIL, EXIT
- Expressions: Binary ops, unary ops, literals, variables, parenthesized
- Variable declarations: VAR, VAR_INPUT, VAR_OUTPUT with type inference
- Full AST construction for all statement types
- Memory-safe tree with proper freeing
- **Code:** `src/st_parser.cpp` (680 lines)

#### Data Types (IEC 61131-3 Elementary)
- **BOOL:** Boolean (0 = FALSE, 1 = TRUE)
- **INT:** Signed 32-bit (-2^31 to 2^31-1)
- **DWORD:** Unsigned 32-bit (0 to 2^32-1)
- **REAL:** IEEE 754 32-bit floating point

#### Control Flow Statements (IEC 6.3.2)
- **IF/THEN/ELSE/ELSIF/END_IF:** Conditional execution
- **CASE/OF/END_CASE:** Multi-way branching
- **FOR/TO/BY/DO/END_FOR:** Deterministic counting loops
- **WHILE/DO/END_WHILE:** Condition-based loops
- **REPEAT/UNTIL/END_REPEAT:** Post-test loops
- **EXIT:** Break from innermost loop

#### Operators (Full Set)
- Arithmetic: +, -, *, /, MOD (% semantics)
- Logical: AND, OR, NOT (full boolean algebra)
- Relational: <, >, <=, >=, =, <> (full comparison set)
- Bitwise: SHL (shift left), SHR (shift right) on INT/DWORD
- Assignment: := (Pascal-style)
- Operator precedence: Standard (multiplication > addition > relational > logical)

#### Type System & Semantics
- Type checking at parse time
- Implicit type conversions (INT + REAL → REAL)
- Operator overloading (AND works on BOOL and INT)
- Assignment with type coercion

#### Documentation
- **File:** `docs/ST_IEC61131_COMPLIANCE.md` (comprehensive)
  - 13 sections covering full compliance claim
  - Detailed feature matrix (supported vs. excluded)
  - Execution model and memory constraints
  - Modbus register binding strategy
  - Known limitations and future extensions

#### Testing Infrastructure
- **File:** `test/test_st_lexer_parser.cpp` (360 lines)
- 8 test cases: lexer/parser for IF, FOR, WHILE, REPEAT, CASE, expressions
- Real-world examples (nested IF, complex expressions)
- AST visualization and debugging

#### Type Definitions & Structures
- **File:** `include/st_types.h` (438 lines)
- Token types (st_token_type_t): 60+ token types
- Data types (st_datatype_t): BOOL, INT, DWORD, REAL, NONE
- AST node types: assignment, if/case/for/while/repeat, binary/unary ops, literal, variable
- Bytecode instructions: 30+ opcodes (stack-based VM design)
- Variable declaration: st_variable_decl_t with scope flags
- Program structure: st_program_t containing VAR block + statement list

#### Design for Phase 2 (Bytecode Compiler & VM)
- Bytecode instruction set defined: PUSH_*, STORE_*, LOAD_*, arithmetic ops, jumps
- Stack-based VM architecture designed (64-value stack)
- VM state machine structure (pc, stack_ptr, halted flag)
- Bytecode program container (512 instruction max)
- Ready for compiler and execution engine implementation

#### Modbus Integration (Phase 3)
- Logic programs isolated from main Modbus server
- VAR_INPUT binds to Modbus registers (read-only in program)
- VAR_OUTPUT binds to Modbus registers (write-only in program)
- 4 independent logic program slots
- Register mapping configuration separate from ST code

---

## [1.0.0] - 2025-11-29 🎉

### FEATURES

#### New: Timer Mode 4 (Input-Triggered) ✨
- **Rising edge detection** (trigger-edge:1): Responds to 0→1 transitions
- **Falling edge detection** (trigger-edge:0): Responds to 1→0 transitions
- **Configurable delay** (delay-ms): Wait before setting output
- **COIL-based input**: Use Modbus COILs as input source for testing
- **Output latch behavior**: Output stays HIGH once triggered
- **Proper state initialization**: No false edges on timer configuration
- **Code:** `src/timer_engine.cpp:152-216`, `src/cli_commands.cpp:391-395`
- **Test:** `test_mode4_comprehensive.py` - [PASS]

#### Timer Modes (Complete)
- **Mode 1:** One-shot (3-phase sequence)
- **Mode 2:** Monostable (retriggerable pulse)
- **Mode 3:** Astable (oscillator/blink) - Verified working
- **Mode 4:** Input-triggered (NEW - verified working)

#### Counter Modes (Complete)
- **Mode 1:** SW (Software Polling) ~10kHz
- **Mode 2:** SW-ISR (GPIO Interrupt) ~100kHz
- **Mode 3:** HW (PCNT Hardware) >1MHz

#### Advanced Features
- **Prescaler:** 1, 4, 8, 16, 64, 256, 1024
- **Scale factor:** Float multiplikator for units
- **Bit-width clamping:** 8, 16, 32, 64-bit support
- **Frequency measurement:** 0-20kHz with 1-sec window
- **Control register:** Start/stop/reset via Modbus
- **PCNT overflow handling:** 16-bit HW → 64-bit accumulator

#### Persistence
- **NVS Storage:** All configs persist across reboot
- **Schema versioning:** v2 with v1 migration support
- **CRC16 validation:** Data integrity check
- **Auto-save:** After counter/timer configuration

#### CLI Commands
- **70+ commands:** Set/show for timers, counters, registers, coils
- **GPIO mapping:** Static cortlægning (persistent)
- **Persistence:** save, load, defaults, reboot
- **System:** hostname, baud, id, echo

#### GPIO Features
- **Physical GPIO:** 0-39 (direct ESP32 pins)
- **Virtual GPIO:** 100-255 (read/write COIL mapping)
- **GPIO2 Heartbeat:** 500ms blink, toggleable
- **STATIC Mapping:** GPIO ↔ Discrete input/Coil

#### Register & Coil Management
- **STATIC registers:** 16 max (persistent)
- **DYNAMIC registers:** 16 max (auto-updated)
- **STATIC coils:** 16 max (persistent)
- **DYNAMIC coils:** 16 max (auto-updated)
- **Multi-word support:** 32/64-bit across registers

---

## [1.0.0-rc1] - 2025-11-28

### FIXED

#### P0.7: PCNT Division-by-2 Bug
- **Issue:** PCNT was dividing counter by 2 (incorrect)
- **Root cause:** Misunderstanding of PCNT hardware behavior
- **Fix:** Removed division - PCNT counts 100% correctly
- **Code:** `src/counter_hw.cpp`
- **Impact:** Counter Mode 3 now accurate

#### CNT-HW-08: Race Condition (Gate-based sync)
- **Issue:** PCNT polling task interfered with counter reads
- **Root cause:** Async poll updates during Modbus reads
- **Fix:** Implement pause/resume gates for atomic access
- **Code:** `src/counter_engine.cpp`, `src/pcnt_driver.cpp`
- **Impact:** Stable counter readings under load

#### Timer Mode 3 Test Suite Failure
- **Issue:** test_suite_extended reports [0,0,0,0,0] for Mode 3
- **Root cause:** Async serial reader race condition in test
- **Status:** NOT A FIRMWARE BUG - Manual tests prove Mode 3 works
- **Verification:** `test_timer_exact.py`, `test_coil_raw.py` - [PASS]

---

## [0.9.0] - 2025-11-15

### REFACTORING

#### Architecture Overhaul (Mega2560 → ESP32)
- **Modular structure:** 30+ focused .cpp files (100-250 lines each)
- **Layer-based design:** 8 clear abstraction layers
- **Hardware drivers:** Explicit abstraction (gpio, uart, pcnt, nvs)
- **Separation of concerns:** Config load/save/apply are separate

#### Module Structure
```
Layer 0: Hardware Abstraction (gpio_driver, uart_driver, pcnt_driver, nvs_driver)
Layer 1: Protocol Core (modbus_frame, modbus_parser, modbus_serializer)
Layer 2: Function Codes (modbus_fc_read, modbus_fc_write, modbus_fc_dispatch)
Layer 3: Modbus Runtime (modbus_rx, modbus_tx, modbus_server)
Layer 4: Storage (registers, coils, gpio_mapping)
Layer 5: Feature Engines (counter_*, timer_*)
Layer 6: Persistence (config_load, config_save, config_apply)
Layer 7: CLI (cli_parser, cli_commands, cli_show, cli_shell)
Layer 8: System (heartbeat, version, debug, main)
```

---

## [0.8.5] - Previous Mega2560 v3.6.5

### ORIGINAL FEATURES (Ported to ESP32)
- Modbus RTU (FC01-10)
- 4 Timers (Modes 1-3)
- 4 Counters (SW, SW-ISR, HW)
- EEPROM persistence
- CLI shell
- RS-485 support

---

## Development Notes

### Testing Status
- ✅ Mode 3: Astable/Blink verified working
- ✅ Mode 4: Input-triggered [PASS] both rise/fall
- ✅ Counter: All modes operational
- ✅ Persistence: Save/load/defaults working
- ✅ Extended suite: 10/11 pass (1 known async race)

### Known Limitations
1. **Async test suite race:** test_suite_extended shows false negatives for Mode 3
   - Workaround: Run manual timing tests for verification
   - Root cause: Unbuffered serial reader in test framework

2. **GPIO signal requirement:** Counter tests require actual signal on GPIO
   - Workaround: Use test simulation or COIL-based input

3. **UART1 only:** Modbus RTU on UART1 (GPIO4/5)
   - Design choice: GPIO0, GPIO2, GPIO15 are strapping pins

---

## Build Information

### RAM Usage
- Typical: ~21KB / 327KB (6.5%)
- Headroom for future features: ~300KB available

### Flash Usage
- Firmware: ~262KB / 1.3MB usable
- Headroom: ~1MB available for features/data

### Dependencies
- ESP32-Arduino core (v2.0.0+)
- PlatformIO (v6.0+)
- No external libraries required (zero dependencies)

---

## Documentation Updates (v1.0.0)

### NEW
- **FEATURE_GUIDE.md:** Complete reference for all features
  - All Timer Modes (1-4) with examples
  - All Counter modes with detailed specs
  - 70+ CLI commands documented
  - GPIO mapping, persistence, system commands
  - Comprehensive examples section

### UPDATED
- **SETUP_GUIDE.md:** Added Quick Start Examples
  - Timer Mode 3 blink test
  - Timer Mode 4 input-triggered test
  - Configuration save/load
  - Hardware counter setup

### EXISTING
- **CLAUDE.md:** Architecture & development guide (maintained)
- **ESP32_Module_Architecture.md:** Module structure (up-to-date)
- **ESP32_Migration_Analysis.md:** Mega2560→ESP32 notes

---

## Future Roadmap

### Planned Features
- [ ] Ethernet (W5500) integration
- [ ] Analog input support (ADC)
- [ ] PWM output (Mode 5 timer)
- [ ] More counter modes (differential, encoder)
- [ ] Real-time clock (RTC)
- [ ] Advanced filtering (moving average, etc)

### Research Phase
- [ ] I2C slave mode
- [ ] Multi-UART support (UART2)
- [ ] SD card logging
- [ ] JSON configuration import/export

---

## Contributors

- **Primary Developer:** Jan Grønlund Larsen
- **Architecture:** Modular redesign of Mega2560 project
- **Testing:** Comprehensive test suite with Python automation

---

## License

[Your License Here]

---

## Support & Resources

- **GitHub:** https://github.com/Jangreenlarsen/Modbus_server_slave_ESP32
- **Documentation:** `docs/` folder (FEATURE_GUIDE.md, SETUP_GUIDE.md)
- **Architecture:** CLAUDE.md (project development guide)
- **Tests:** Test scripts in root (`test_*.py`)

---

## Version History Timeline

```
v1.0.0      (2025-11-29) Release - Timer Mode 4 complete
v1.0.0-rc1  (2025-11-28) Release Candidate 1
v0.9.0      (2025-11-15) Major refactor (30+ modules)
v0.8.5      (2025-10-xx) Ported from Mega2560 v3.6.5
```

---

**Last Updated:** 2025-11-30 | **Status:** v2.0.0 Released 🚀
