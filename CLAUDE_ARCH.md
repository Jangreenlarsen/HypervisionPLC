# CLAUDE Architecture - Project Structure & File Reference

**Read when:** You need to understand how components fit together, or find where code lives.

---

## 🏗️ Layered Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│ Layer 8: System (main.cpp, heartbeat, version, debug)  │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 7: CLI (parser, commands, show, history, shell)  │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 6: Persistence (config struct, load, save, apply)│
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 5: Feature Engines (Counters, Timers, ST Logic)  │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 4: Registers & Coils (storage, GPIO mapping)     │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 3: Modbus Server (RX, TX, state machine)         │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 2: FC Handlers (FC01-10, dispatch)               │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 1: Protocol Core (frame, parser, serializer)     │
└─────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────┐
│ Layer 0: Hardware Drivers (GPIO, UART, PCNT, NVS)      │
└─────────────────────────────────────────────────────────┘
```

Each layer has **ONE responsibility**. No circular dependencies.

---

## 📂 File Reference by Layer

### Layer 0: Hardware Drivers

| File | Purpose | Principle |
|------|---------|-----------|
| `gpio_driver.cpp/h` | GPIO init, read, write, interrupt | Only place that knows GPIO registers |
| `uart_driver.cpp/h` | UART0/UART1 init, RX/TX, buffering | Only place that knows UART registers |
| `pcnt_driver.cpp/h` | PCNT unit config, counting, ISR | Hardware pulse counter interface |
| `nvs_driver.cpp/h` | Non-volatile storage read/write | Only place that knows ESP32 NVS HAL |

**Key Principle:** These are the ONLY files that know ESP32 registers/HAL. Everything above uses these abstractions.

---

### Layer 1.5: HTTP/REST API (v6.0.0+, udvidet v6.3.0)

| File | Purpose |
|------|---------|
| `http_server.cpp/h` | HTTP server init, URI registrering, TLS (optional) |
| `api_handlers.cpp/h` | Alle REST API handlers (72+ endpoints) |

**Key Principle:** API handlers kalder direkte ned i Layer 4-5 (registers, engines). Kører i separat FreeRTOS task, blokerer ikke Modbus.

**v6.3.0 tilføjelser:**
- CORS headers på alle responses + OPTIONS preflight
- Bulk register read/write (hr, ir, coils, di)
- ST Logic debug API (pause/step/breakpoint/snapshot)
- Telnet, hostname, watchdog, heartbeat endpoints
- 72+ registrerede URI handlers (max_uri_handlers: 80)
- Prometheus metrics endpoint (`GET /api/metrics`)
- Persistence Group REST API (CRUD + save/restore)
- Token bucket rate limiting per klient IP

---

### Layer 1: Protocol Core (Modbus framing)

| File | Purpose |
|------|---------|
| `modbus_frame.cpp/h` | ModbusFrame struct, CRC16 calculation |
| `modbus_parser.cpp/h` | Parse raw bytes → ReadRequest/WriteRequest |
| `modbus_serializer.cpp/h` | Build response frame from data |

**Key Principle:** Pure functions, testable without full Modbus server running.

---

### Layer 2: Function Code Handlers

| File | Purpose |
|------|---------|
| `modbus_fc_read.cpp/h` | FC01, FC02, FC03, FC04 implementations |
| `modbus_fc_write.cpp/h` | FC05, FC06, FC0F, FC10 implementations |
| `modbus_fc_dispatch.cpp/h` | Route FC code → handler |

**Key Principle:** Each function code isolated. Adding new FC only requires one file change.

---

### Layer 3: Modbus Server/Master Runtime

| File | Purpose |
|------|---------|
| `modbus_rx.cpp/h` | Serial RX, frame detection, timeout, ISR (Slave UART0) |
| `modbus_tx.cpp/h` | RS-485 DIR control, serial TX (Slave UART0) |
| `modbus_server.cpp/h` | Main Modbus Slave state machine (UART0) |
| `modbus_master.cpp/h` | Modbus Master implementation (UART1) |

**Slave Flow (UART0):** idle → RX (receive frame) → process (call FC handler) → TX (send response) → idle

**Master Flow (UART1):** ST Logic request → TX (send request) → RX (wait response) → parse → return to ST Logic

---

### Layer 4: Register & Coil Storage

| File | Purpose |
|------|---------|
| `registers.cpp/h` | Holding/input register array, access functions |
| `coils.cpp/h` | Coil/discrete input bit arrays, access functions |
| `gpio_mapping.cpp/h` | GPIO ↔ coil/discrete input bindings |
| `register_allocator.cpp/h` | Global register allocation tracking (BUG-025, BUG-026, BUG-028) |
| `registers_persist.cpp/h` | Persistent register storage (NVS backup/restore) |

**Key Principle:** All register access goes through these files. Can add validation here.

---

### Layer 5: Feature Engines

#### Counter Engine
```
counter_config.cpp/h     ← Configuration struct
  ↓
counter_engine.cpp/h     ← Orchestration (main entry point)
  ├→ counter_sw.cpp/h           (Software polling mode)
  ├→ counter_sw_isr.cpp/h       (Hardware ISR mode)
  ├→ counter_hw.cpp/h           (Hardware PCNT mode)
  └→ counter_frequency.cpp/h    (Frequency measurement)
```

**Modes:**
1. **SW** - Software polling (GPIO level read)
2. **SW-ISR** - Software interrupt (GPIO interrupt, INT0-INT5)
3. **HW** - Hardware PCNT (most robust)

**Registers per counter (v4.2.4 - 6 total, multi-word support):**
- `index_reg` - Scaled value (1-4 words depending on bit_width)
- `raw_reg` - Prescaled value (1-4 words)
- `freq_reg` - Measured frequency (Hz, 1 word)
- `overload_reg` - Overflow flag (1 word)
- `ctrl_reg` - Control bits (bit4=compare-match, 1 word)
- `compare_value_reg` - Compare threshold (1-4 words, runtime modifiable via Modbus)

**Smart Register Defaults (v4.2.4 - BUG-028, BUG-030):**
- Counter 1: HR100-114 (20 registers: index+raw+freq+overload+ctrl+compare, 5 reserved)
- Counter 2: HR120-134
- Counter 3: HR140-154
- Counter 4: HR160-174

**Example layout (Counter 1, 32-bit):**
- HR100-101: Index (scaled, 2 words)
- HR104-105: Raw (prescaled, 2 words)
- HR108: Frequency
- HR109: Overload
- HR110: Control (bit7=running, bit4=compare-match)
- HR111-112: Compare value (2 words, writable by SCADA)

#### Timer Engine
```
timer_config.cpp/h       ← Configuration struct
  ↓
timer_engine.cpp/h       ← State machines (modes 1-4)
```

**Modes:**
1. One-shot sequences (3-phase timing)
2. Monostable (retriggerable pulse)
3. Astable (blink/toggle)
4. Input-triggered (responds to discrete inputs)

#### ST Logic Engine
```
st_logic_config.cpp/h    ← Program config, persistence, chunked compilation
  ↓
st_logic_engine.cpp/h    ← Execution loop, scheduler (MAIN)
  ├→ st_compiler.cpp/h         (ST source → bytecode compiler + segment compiler)
  ├→ st_parser.cpp/h           (ST syntax parser, configurable AST pool)
  ├→ st_lexer.cpp/h            (ST tokenizer)
  ├→ st_vm.cpp/h               (Virtual machine executor)
  ├→ st_debug.cpp/h            (Debugger: breakpoints, step, inspect) ⭐ v5.3.0
  ├→ st_bytecode_persist.cpp/h (Bytecode cache i SPIFFS, CRC32 validering) ⭐ v7.6.0
  ├→ st_source_scanner.cpp/h   (Chunk boundary finder for multi-pass compile) ⭐ v7.6.0
  └→ cli_commands_logic.cpp    (CLI interface)

gpio_mapping.cpp/h       ← Variable binding system (shared with GPIO)
```

**Key Features:**
- Fixed rate scheduler: 10ms default (configurable)
- Sequential execution: Logic1 → Logic2 → Logic3 → Logic4
- 3-Phase I/O: Read inputs → Execute → Write outputs
- Type-safe variable bindings (BOOL/INT/REAL)
- Unified mapping system (ST vars ↔ Modbus registers/coils)
- **Interactive Debugger (v5.3.0):** Pause, step, breakpoints, variable inspection
- **User-Defined Functions (v6.1.0 FEAT-003):** FUNCTION/FUNCTION_BLOCK support
- **Bytecode Persistens (v7.6.0):** Cached bytecode i SPIFFS, ~2 KB boot vs 36-94 KB recompile
- **Chunked Compilation (v7.6.0):** Per-function segment compiler med jump relocation

**Registers:**
- IR 200-203: Status (enabled, compiled, error)
- IR 204-207: Execution count
- IR 208-211: Error count
- IR 212-215: Error code
- IR 216-219: Variable binding count
- IR 220-251: Variable values
- HR 200-203: Control

#### FUNCTION / FUNCTION_BLOCK Support (FEAT-003, v6.1.0)

IEC 61131-3-kompatible brugerdefinerede funktioner.

```
Compilation Pipeline:
  ST Source → Lexer → Parser → AST → Compiler → Bytecode → VM
                                        ↓
                              Two-Pass Compilation:
                              Pass 1: FUNCTION/FUNCTION_BLOCK defs
                              Pass 2: Main program body
```

**FUNCTION (stateless):**
```
FUNCTION name : return_type
VAR_INPUT param1 : INT; END_VAR
VAR local1 : INT; END_VAR
BEGIN
  name := param1 * 2;    (* Return via funktionsnavn *)
END_FUNCTION
```
- Ingen state mellem kald
- Returnerer værdi via funktionsnavn (IEC 61131-3)
- Kompileres med scope-aware variable access (LOAD_PARAM/LOAD_LOCAL)

**FUNCTION_BLOCK (stateful):**
```
FUNCTION_BLOCK name
VAR_INPUT en : BOOL; END_VAR
VAR_OUTPUT cv : INT; END_VAR
VAR cnt : INT; END_VAR        (* Bevares mellem kald *)
BEGIN
  IF en THEN cnt := cnt + 1; END_IF;
  cv := cnt;
END_FUNCTION_BLOCK
```
- Lokale variabler bevares mellem kald (state persistence)
- Hver call-site i kildekoden får en unik instans (som TON/CTU)
- Max 16 FB instanser per program (ST_MAX_FB_INSTANCES)

**Arkitektur:**
```
st_function_registry_t (heap-allokeret, ejet af bytecode program)
  ├→ functions[64]           Registrerede funktioner (builtin + user)
  ├→ fb_instances[16]        FUNCTION_BLOCK instans-storage
  └→ user_count/builtin_count

st_call_frame_t (VM call stack, max 8 deep)
  ├→ return_pc               Retur-adresse
  ├→ param_base              Parameter offset på stack
  ├→ param_count/local_count
  └→ fb_instance_id          0xFF=stateless, ellers instans-index

Nye opcodes:
  CALL_USER    → Push call frame, jump til funktion
  RETURN       → Pop call frame, gem FB state
  LOAD_PARAM   → Læs parameter fra call frame
  LOAD_LOCAL   → Læs lokal variabel
  STORE_LOCAL  → Skriv lokal variabel
```

**CLI kommandoer:**
- `show logic <id> functions` - Vis registrerede user-funktioner
- `show logic <id> bytecode` - Viser CALL_USER, RETURN, LOAD_PARAM etc.

**Limits:**
| Konstant | Værdi | Beskrivelse |
|----------|-------|-------------|
| ST_MAX_USER_FUNCTIONS | 16 | Max brugerdefinerede funktioner |
| ST_MAX_FUNCTION_PARAMS | 8 | Max parametre per funktion |
| ST_MAX_FUNCTION_LOCALS | 16 | Max lokale variabler per funktion |
| ST_MAX_CALL_DEPTH | 8 | Max nestede funktionskald |
| ST_MAX_FB_INSTANCES | 16 | Max FUNCTION_BLOCK instanser |
| ST_MAX_FB_LOCALS | 16 | Max lokale variabler per FB instans |

---

### Layer 6: Persistence (Config Management)

| File | Purpose |
|------|---------|
| `config_struct.cpp/h` | PersistConfig struct definition |
| `config_load.cpp/h` | Load from NVS, schema migration, validation |
| `config_save.cpp/h` | Save to NVS, CRC calculation |
| `config_apply.cpp/h` | Apply config to engines (initialize) |

**Principle:**
- **Load:** Reading from storage (can fail gracefully)
- **Save:** Persisting to storage (atomic with CRC)
- **Apply:** Activating config in running system (idempotent)

---

### Layer 7: User Interface (CLI)

| File | Purpose |
|------|---------|
| `cli_parser.cpp/h` | Tokenize input, dispatch to handler |
| `cli_commands.cpp/h` | All `set` command implementations |
| `cli_commands_logic.cpp/h` | ST Logic specific commands |
| `cli_commands_modbus_slave.cpp/h` | Modbus Slave configuration commands (v4.4.1) |
| `cli_commands_modbus_master.cpp/h` | Modbus Master configuration commands (v4.4.0) |
| `cli_show.cpp/h` | All `show` command implementations + SET display (v4.4.2) |
| `cli_history.cpp/h` | Command history, arrow key navigation |
| `cli_shell.cpp/h` | Serial I/O, state machine, main CLI loop |
| `cli_config_regs.cpp/h` | Register configuration commands |
| `cli_config_coils.cpp/h` | Coil configuration commands |
| `cli_remote.cpp/h` | Remote CLI access handling |

**Key Principle:** One file per concern. Adding new command = one function, no cross-file changes.

---

### Layer 7.5: Network & Console (v3.0+)

| File | Purpose |
|------|---------|
| `wifi_driver.cpp/h` | ESP32 Wi-Fi HAL, connect/disconnect |
| `ethernet_driver.cpp/h` | W5500 SPI Ethernet driver (v6.1.0+) |
| `network_manager.cpp/h` | Network state machine, reconnection |
| `network_config.cpp/h` | Wi-Fi/network/ethernet configuration handling |
| `tcp_server.cpp/h` | TCP server for remote connections |
| `telnet_server.cpp/h` | Telnet protocol implementation |
| `console_serial.cpp/h` | Serial console I/O abstraction |
| `console_telnet.cpp/h` | Telnet console I/O abstraction |
| `console.h` | Unified console interface |

**Key Principle:** Network is optional. WiFi, Ethernet and Telnet are independent. CLI works on both serial and telnet transparently.

---

### Layer 8: System

| File | Purpose |
|------|---------|
| `main.cpp` | setup() and loop() only |
| `heartbeat.cpp/h` | LED blink, watchdog timer |
| `version.cpp/h` | Version string, changelog |
| `debug.cpp/h` | Debug output helpers, printf wrappers |
| `debug_flags.cpp/h` | Runtime debug flag management |
| `watchdog_monitor.cpp/h` | Watchdog timer, crash recovery |

---

### ST Logic Builtins (v4.0+)

| File | Purpose |
|------|---------|
| `st_builtins.cpp/h` | Built-in ST functions (ABS, MIN, MAX, etc.) |
| `st_builtin_persist.cpp/h` | Persistent ST variable storage |
| `st_builtin_signal.cpp/h` | Signal processing (SCALE, HYSTERESIS, BLINK, FILTER) v4.8 |
| `st_debug.cpp/h` | Debugger: pause, step, breakpoints, variable inspection (v5.3.0) |

---

### Headers (Single Source of Truth)

| File | Purpose |
|------|---------|
| `constants.h` | ALL #define, enums, MAX values |
| `types.h` | ALL struct definitions |
| `config.h` | Build-time configuration |

---

## 🎯 Finding Code by Function

### "Where is function X?"

**Search pattern:**
```bash
# Find function definition
grep -r "^[a-zA-Z_].*X\s*(" src/ include/

# Find function calls
grep -r "X(" src/ include/

# Find in specific layer
grep -r "X" src/layer*_*.cpp

# Use file reference above to narrow search
```

### Common Locations:

| What | File |
|------|------|
| Modbus register read/write | `src/registers.cpp` |
| Modbus FC handler | `src/modbus_fc_*.cpp` |
| Counter configuration | `src/counter_*.cpp` |
| Timer logic | `src/timer_engine.cpp` |
| ST Logic execution | `src/st_logic_engine.cpp` |
| CLI command | `src/cli_commands.cpp` or `src/cli_commands_logic.cpp` |
| Constants | `include/constants.h` |
| Structs | `include/types.h` |

---

## 🔀 Data Flow Example: "Read Holding Register"

```
User (via Modbus)
  ↓
Modbus Server (Layer 3)
  └→ modbus_server.cpp - Receive frame
    ↓
FC Handler (Layer 2)
  └→ modbus_fc_read.cpp - FC03 handler
    ↓
Register Storage (Layer 4)
  └→ registers.cpp - Get holding register value
    ↓
Return frame
  └→ modbus_serializer.cpp - Build response
    ↓
Modbus TX (Layer 3)
  └→ modbus_tx.cpp - Send frame
    ↓
User receives response
```

---

## 🔄 Data Flow Example: "Execute ST Logic Program"

```
Timer interrupt (10ms)
  ↓
Main loop() - Layer 8
  ↓
ST Logic Engine (Layer 5)
  └→ st_logic_engine.cpp - Main scheduler
    ↓
3-Phase Execution:
  ├─ Phase 1: Read inputs
  │   └→ gpio_mapping.cpp - Read Modbus input registers/coils
  │
  ├─ Phase 2: Execute each program
  │   ├→ st_lexer.cpp - Tokenize bytecode
  │   ├→ st_parser.cpp - Parse tokens
  │   └→ st_vm.cpp - Execute bytecode
  │
  └─ Phase 3: Write outputs
      └→ gpio_mapping.cpp - Write Modbus output registers/coils

Timing check (Layer 5)
  └→ If cycle > interval, extend interval automatically
```

---

## 🚀 Adding New Feature: Step-by-Step

### Example: Add new counter mode (Mode 4: PWM)

1. **Define struct** in `include/types.h`:
   ```c
   // Add to CounterConfig
   uint8_t mode_4_frequency;
   uint8_t mode_4_duty;
   ```

2. **Create implementation** in `src/counter_mode4.cpp`:
   ```cpp
   void counter_mode4_init(uint8_t id) { ... }
   void counter_mode4_update() { ... }
   ```

3. **Update orchestration** in `src/counter_engine.cpp`:
   ```cpp
   if (mode == 4) counter_mode4_update();
   ```

4. **Update CLI** in `src/cli_commands.cpp`:
   ```cpp
   // set counter 1 mode 4 frequency:50 duty:75
   ```

5. **Update validation** in `src/counter_config.cpp`:
   ```cpp
   if (mode == 4) { validate_mode4(); }
   ```

**Result:** 5 files changed, isolated, no cross-file side effects.

---

## 📊 Build System

**Tool:** PlatformIO (pio)

**Board:** ESP32 DOIT DevKit V1

**Commands:**
```bash
pio run              # Build
pio run -t upload    # Upload to device
pio device monitor   # Serial monitor
pio clean && pio run # Clean rebuild
```

**Configuration:** `platformio.ini`

**Build artifacts:** `.pio/build/esp32/firmware.bin`

---

## 📈 Code Statistics

- **Total files:** 50+ .cpp/.h files
- **Total lines:** ~15,000 lines of code
- **File size:** 100-300 lines per file (modular)
- **Dependency depth:** 8 layers (clear hierarchy)
- **Circular deps:** 0 (by design)

**Memory Usage (ESP32):**
- RAM: 520 KB available → 38% used (~124 KB)
- Flash: 4 MB available → 68% used (~2.8 MB, Build #759)

---

## 🔗 Related Documentation

- **BUGS_INDEX.md** - Known bugs by priority
- **BUGS.md** - Detailed bug analysis
- **TIMING_ANALYSIS.md** - ST Logic timing deep dive
- **MODBUS_REGISTER_MAP.md** - Complete register reference
- **ST_DEBUG_GUIDE.md** - ST Logic Debugger usage guide (v5.3.0)
- **FEAT-003_PLAN.md** - FUNCTION/FUNCTION_BLOCK implementation plan (FEAT-003)
- **tests/ST_TEST_FUNCTIONS.md** - FUNCTION/FUNCTION_BLOCK test cases
- **CHANGELOG.md** - Version history

---

## ✅ Architecture Principles

1. **Layered Design:** Each layer has single responsibility
2. **No Circular Dependencies:** Prevents spaghetti code
3. **Testable:** Layers can be mocked for unit testing
4. **Modular:** Adding features = adding files, not modifying core
5. **Hardware Abstraction:** Layer 0 isolated from application logic
6. **Single Responsibility:** One file = one concern
7. **Configuration Separated:** Load ≠ Save ≠ Apply

---

## 🎯 Quick Navigation by Task

| Task | Start Here | Then Read |
|------|-----------|-----------|
| Fix counter bug | `BUGS_INDEX.md` | `src/counter_*.cpp` |
| Add timer mode | This file | `src/timer_engine.cpp` |
| Fix Modbus issue | This file | `src/modbus_*.cpp` |
| Understand flow | This file (Data Flow section) | Specific layer files |
| Find function X | File reference table | Use grep |

---

**Last Updated:** 2026-03-18
**Version:** v7.1.0
**Build:** #1432
**Status:** ✅ Active & Complete
