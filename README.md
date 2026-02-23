# Modbus RTU Server (ESP32)

**Version:** v6.0.7 | **Build:** #1227 | **Status:** Production-Ready | **Platform:** ESP32-WROOM-32

En komplet, modulær **Modbus RTU Server** implementation til ESP32-WROOM-32 mikrocontroller med **dual Modbus interfaces** (Slave + Master), ST Structured Text Logic programmering med IEC 61131-3 type system, Wi-Fi netværk, **HTTP REST API** for Node-RED integration, telnet CLI interface, og komplet Modbus register dokumentation.

---

## 🏗️ Architecture Overview

ESP32'en fungerer som en **dual-mode Modbus gateway** med to separate RS-485 interfaces:

### 📥 Modbus Slave Interface (UART0)
**Formål:** Styring fra SCADA/HMI systemer
- **Hardware:** UART0 (GPIO16 TX, GPIO17 RX, GPIO15 DE/RE)
- **Funktion:** ESP32'en agerer som Modbus Slave
- **Use cases:**
  - SCADA læser/skriver counters, timers, ST Logic variabler
  - HMI visualisering af process data
  - PLC integration for fabriksautomatisering
  - Remote monitoring og control

### 📤 Modbus Master Interface (UART1)
**Formål:** Styring af eksterne I/O boards og remote devices
- **Hardware:** UART1 (GPIO25 TX, GPIO26 RX, GPIO27 DE/RE)
- **Funktion:** ESP32'en agerer som Modbus Master
- **Use cases:**
  - Læs eksterne temperature sensorer
  - Styr remote I/O modules (relæer, analog inputs)
  - Poll multiple Modbus slaves
  - Distributed I/O expansion
  - **Integration:** Via ST Logic builtin funktioner (MB_READ_*, MB_WRITE_*)

### 🔄 Workflow Eksempel
```
SCADA (Modbus Master)
    ↕ UART0 (Slave)
ESP32 Core
    - Counters (4x)
    - Timers (4x)
    - ST Logic Programs (4x) ← Kan læse/skrive via UART1
    ↕ UART1 (Master)
Remote I/O Boards (Modbus Slaves)
    - Temperature sensors
    - Relay modules
    - Analog inputs
```

---

## 📑 Complete Documentation Index

> **Navigation Tip:** Use `Ctrl+F` to search this index. All documents are categorized by purpose.

---

### 📋 Quick Navigation

| I Need To... | Read This |
|--------------|-----------|
| **Get started** | [Quick Start](#quick-start-first-boot) or [docs/SETUP_GUIDE.md](docs/SETUP_GUIDE.md) |
| **Understand features** | [README.md](#) (this file) or [docs/FEATURE_GUIDE.md](docs/FEATURE_GUIDE.md) |
| **Configure Modbus** | [MODBUS_REGISTER_MAP.md](MODBUS_REGISTER_MAP.md) |
| **Learn ST Logic** | [docs/ST_USAGE_GUIDE.md](docs/ST_USAGE_GUIDE.md) or [docs/README_ST_LOGIC.md](docs/README_ST_LOGIC.md) |
| **Debug ST programs** | [ST_DEBUG_GUIDE.md](ST_DEBUG_GUIDE.md) |
| **Use REST API** | [docs/REST_API.md](docs/REST_API.md) |
| **Backup/restore config** | [README.md → Backup/Restore](#config-backuprestore-v607) |
| **Configure counters** | [docs/COUNTER_COMPARE_QUICK_START.md](docs/COUNTER_COMPARE_QUICK_START.md) |
| **Set up GPIO** | [docs/GPIO_MAPPING_GUIDE.md](docs/GPIO_MAPPING_GUIDE.md) |
| **Contribute code** | [CLAUDE_INDEX.md](CLAUDE_INDEX.md) → [CLAUDE_WORKFLOW.md](CLAUDE_WORKFLOW.md) |
| **Check known bugs** | [BUGS_INDEX.md](BUGS_INDEX.md) |
| **Run tests** | [tests/TEST_INDEX.md](tests/TEST_INDEX.md) |
| **See version history** | [CHANGELOG.md](CHANGELOG.md) |

---

### 📚 Documentation Categories

#### 1. User Documentation (End Users & Integrators)

| Document | Description |
|----------|-------------|
| **[README.md](#)** | Complete feature guide, CLI reference, examples |
| **[docs/SETUP_GUIDE.md](docs/SETUP_GUIDE.md)** | Hardware setup, first boot guide |
| **[docs/FEATURE_GUIDE.md](docs/FEATURE_GUIDE.md)** | Feature overview and capabilities |
| **[CHANGELOG.md](CHANGELOG.md)** | Version history with all changes |
| **[TODO.md](TODO.md)** | Planned features and roadmap |

#### 2. Modbus Documentation

| Document | Description |
|----------|-------------|
| **[MODBUS_REGISTER_MAP.md](MODBUS_REGISTER_MAP.md)** | Complete register/coil mapping reference |
| **[MODBUS_MASTER_ANALYSIS.md](MODBUS_MASTER_ANALYSIS.md)** | Modbus Master feature analysis |
| **[MODBUS_MASTER_ENHANCEMENTS.md](MODBUS_MASTER_ENHANCEMENTS.md)** | Modbus Master improvements |

#### 3. ST Logic Documentation

**Getting Started:**
| Document | Description |
|----------|-------------|
| **[docs/README_ST_LOGIC.md](docs/README_ST_LOGIC.md)** | ST Logic introduction |
| **[docs/ST_USAGE_GUIDE.md](docs/ST_USAGE_GUIDE.md)** | Complete ST usage guide |
| **[docs/GPIO2_ST_QUICK_START.md](docs/GPIO2_ST_QUICK_START.md)** | GPIO control with ST Logic |

**Reference & Specification:**
| Document | Description |
|----------|-------------|
| **[ST_SYNTAX_ANALYSIS.md](ST_SYNTAX_ANALYSIS.md)** | ST syntax reference |
| **[docs/ST_IEC61131_COMPLIANCE.md](docs/ST_IEC61131_COMPLIANCE.md)** | IEC 61131-3 compliance details |
| **[ST_FUNCTIONS_COMPLETE_ANALYSIS.md](ST_FUNCTIONS_COMPLETE_ANALYSIS.md)** | All builtin functions documented |
| **[ST_FUNCTIONS_V47.md](ST_FUNCTIONS_V47.md)** | v4.7 function additions |
| **[docs/ST_SIGNAL_PROCESSING_DESIGN.md](docs/ST_SIGNAL_PROCESSING_DESIGN.md)** | Signal processing functions |

**Debugging & Performance:**
| Document | Description |
|----------|-------------|
| **[ST_DEBUG_GUIDE.md](ST_DEBUG_GUIDE.md)** | Interactive debugger guide (v5.3.0) |
| **[ST_MONITORING.md](ST_MONITORING.md)** | Performance monitoring |
| **[TIMING_ANALYSIS.md](TIMING_ANALYSIS.md)** | Timing specifications |
| **[ST_MONITORING_IMPLEMENTATION_STATUS.md](ST_MONITORING_IMPLEMENTATION_STATUS.md)** | Monitoring implementation status |

**Testing (Modular Test Plans):**
| Document | Description |
|----------|-------------|
| **[tests/TEST_INDEX.md](tests/TEST_INDEX.md)** | Test navigation hub - start here |
| **[tests/API_TEST_PLAN.md](tests/API_TEST_PLAN.md)** | HTTP REST API tests (41 tests) |
| **[tests/ST_TEST_OPERATORS.md](tests/ST_TEST_OPERATORS.md)** | Operator tests: +, -, *, /, AND, OR, etc. (20 tests) |
| **[tests/ST_TEST_BUILTINS.md](tests/ST_TEST_BUILTINS.md)** | Builtin functions: ABS, SQRT, MIN, MAX, etc. (23 tests) |
| **[tests/ST_TEST_TIMERS.md](tests/ST_TEST_TIMERS.md)** | Timer tests: TON, TOF, TP (6 tests) |
| **[tests/ST_TEST_GPIO.md](tests/ST_TEST_GPIO.md)** | GPIO & Hardware tests (4 tests) |
| **[tests/ST_TEST_CONTROL.md](tests/ST_TEST_CONTROL.md)** | Control structures: IF, CASE, FOR, WHILE (6 tests) |
| **[tests/ST_TEST_TYPES.md](tests/ST_TEST_TYPES.md)** | Type system: INT, DINT, REAL, EXPORT (12 tests) |
| **[tests/ST_TEST_COMBINED.md](tests/ST_TEST_COMBINED.md)** | Combined/integration tests (10 tests) |
| **[tests/ST_TEST_FUNCTIONS.md](tests/ST_TEST_FUNCTIONS.md)** | FUNCTION/FUNCTION_BLOCK tests (14 tests) ⭐ NEW |

**Legacy Test Documentation:**
| Document | Description |
|----------|-------------|
| **[ST_COMPLETE_TEST_PLAN.md](ST_COMPLETE_TEST_PLAN.md)** | Original monolithic test plan (archived) |
| **[ST_COMPLEX_TEST_CASES.md](ST_COMPLEX_TEST_CASES.md)** | Advanced test scenarios |
| **[docs/ST_FUNCTION_TESTS.md](docs/ST_FUNCTION_TESTS.md)** | Function-specific tests |
| **[docs/ST_LATCH_TEST_CASES.md](docs/ST_LATCH_TEST_CASES.md)** | SR/RS latch test cases |
| **[docs/TEST_GPIO2_ST_LOGIC.md](docs/TEST_GPIO2_ST_LOGIC.md)** | GPIO+ST integration tests |

**Architecture & Design:**
| Document | Description |
|----------|-------------|
| **[ST_MODULE_ANALYSIS.md](ST_MODULE_ANALYSIS.md)** | Module architecture |
| **[ST_DEEP_ANALYSIS.md](ST_DEEP_ANALYSIS.md)** | Deep dive analysis |
| **[ST_TYPE_REFACTOR_PLAN.md](ST_TYPE_REFACTOR_PLAN.md)** | IEC 61131-3 type system plan |
| **[ST_REMOTE_WRITE_SYNTAX_DESIGN.md](ST_REMOTE_WRITE_SYNTAX_DESIGN.md)** | MB_WRITE syntax design |
| **[CODE_REVIEW_ST_FUNCTIONS.md](CODE_REVIEW_ST_FUNCTIONS.md)** | Code review notes |
| **[docs/ST_FUNCTIONS_TODO.md](docs/ST_FUNCTIONS_TODO.md)** | Planned function additions |
| **[FEAT-003_PLAN.md](FEAT-003_PLAN.md)** | FUNCTION/FUNCTION_BLOCK implementation plan ⭐ NEW |

#### 4. Counter & Timer Documentation

| Document | Description |
|----------|-------------|
| **[docs/COUNTER_COMPARE_QUICK_START.md](docs/COUNTER_COMPARE_QUICK_START.md)** | Counter compare feature quick start |
| **[docs/COUNTER_COMPARE_REFERENCE.md](docs/COUNTER_COMPARE_REFERENCE.md)** | Complete compare reference |
| **[docs/COUNTER_CONFIG_TEMPLATES.md](docs/COUNTER_CONFIG_TEMPLATES.md)** | Ready-to-use counter configs |

#### 5. GPIO & Hardware Documentation

| Document | Description |
|----------|-------------|
| **[docs/GPIO_MAPPING_GUIDE.md](docs/GPIO_MAPPING_GUIDE.md)** | GPIO to Modbus mapping guide |
| **[docs/GPIO2_ST_QUICK_START.md](docs/GPIO2_ST_QUICK_START.md)** | GPIO2 with ST Logic |
| **[docs/ESP32_Migration_Analysis.md](docs/ESP32_Migration_Analysis.md)** | ESP32 migration from Mega2560 |
| **[docs/ESP32_Module_Architecture.md](docs/ESP32_Module_Architecture.md)** | ESP32 module architecture |

#### 6. Developer Documentation (Claude Code / Contributors)

**Start Here:**
| Document | Description |
|----------|-------------|
| **[CLAUDE.md](CLAUDE.md)** | Development guide hub (Danish) |
| **[CLAUDE_INDEX.md](CLAUDE_INDEX.md)** | Quick start & navigation ⭐ |
| **[CLAUDE_SETUP.md](CLAUDE_SETUP.md)** | Security rules & environment |
| **[CLAUDE_WORKFLOW.md](CLAUDE_WORKFLOW.md)** | Code modification & commit guidelines |
| **[CLAUDE_ARCH.md](CLAUDE_ARCH.md)** | Layered architecture & file reference |

**Bug Tracking:**
| Document | Description |
|----------|-------------|
| **[BUGS_INDEX.md](BUGS_INDEX.md)** | Quick bug lookup ⚡ CHECK BEFORE CODING |
| **[BUGS.md](BUGS.md)** | Detailed bug analysis & history |

#### 7. Design & Architecture Documents

| Document | Description |
|----------|-------------|
| **[docs/LOGIC_BLOCK_HLD.md](docs/LOGIC_BLOCK_HLD.md)** | Logic block high-level design |
| **[SYSTEM_FEATURE_ANALYSIS.md](SYSTEM_FEATURE_ANALYSIS.md)** | System feature analysis |

#### 8. Release & Version Documentation

| Document | Description |
|----------|-------------|
| **[CHANGELOG.md](CHANGELOG.md)** | Complete version history |
| **[RELEASE_NOTES_V47.md](RELEASE_NOTES_V47.md)** | v4.7 release notes |
| **[IMPLEMENTATION_STATUS_v4.6.0.md](IMPLEMENTATION_STATUS_v4.6.0.md)** | v4.6.0 implementation status |
| **[TEST_PLAN_v4.6.0.md](TEST_PLAN_v4.6.0.md)** | v4.6.0 test plan |

#### 9. Future Plans

| Document | Description |
|----------|-------------|
| **[docs/W5500_Ethernet_Integration_Plan.md](docs/W5500_Ethernet_Integration_Plan.md)** | Ethernet expansion plan |
| **[TODO.md](TODO.md)** | Planned features and improvements |

#### 10. Archived Documentation

Historical test reports and analysis documents are preserved in `docs/ARCHIVED/` for reference.

---

### 📁 File Organization

```
📦 ESP32 Modbus RTU Server
├── 📄 README.md                    ← You are here (main documentation)
├── 📄 CHANGELOG.md                 ← Version history
├── 📄 TODO.md                      ← Planned features
│
├── 📂 Development (CLAUDE_*.md)
│   ├── CLAUDE.md                   ← Developer hub
│   ├── CLAUDE_INDEX.md             ← Quick navigation
│   ├── CLAUDE_SETUP.md             ← Security & rules
│   ├── CLAUDE_WORKFLOW.md          ← Commit workflow
│   └── CLAUDE_ARCH.md              ← Architecture reference
│
├── 📂 Bug Tracking (BUGS*.md)
│   ├── BUGS_INDEX.md               ← Quick lookup table
│   └── BUGS.md                     ← Detailed analysis
│
├── 📂 ST Logic (*ST*.md)
│   ├── ST_DEBUG_GUIDE.md           ← Debugger guide
│   ├── ST_SYNTAX_ANALYSIS.md       ← Syntax reference
│   ├── ST_MONITORING.md            ← Performance monitoring
│   └── ... (12+ more ST docs)
│
├── 📂 Modbus (MODBUS*.md)
│   ├── MODBUS_REGISTER_MAP.md      ← Register reference
│   └── MODBUS_MASTER_*.md          ← Master feature docs
│
├── 📂 docs/                        ← Additional documentation
│   ├── SETUP_GUIDE.md              ← Hardware setup
│   ├── FEATURE_GUIDE.md            ← Feature overview
│   ├── ST_USAGE_GUIDE.md           ← ST programming guide
│   ├── GPIO_MAPPING_GUIDE.md       ← GPIO configuration
│   ├── COUNTER_*.md                ← Counter documentation
│   └── ARCHIVED/                   ← Historical docs
│
├── 📂 src/                         ← C++ source files (30+)
├── 📂 include/                     ← Header files
└── 📄 platformio.ini               ← Build configuration
```

---

### 🏗️ Core Features (This Document)

**Architecture & Interfaces:**
- [**Architecture Overview**](#-architecture-overview) ⭐ - Dual Modbus Interface (Slave + Master)
- [Modbus Slave Interface (UART0)](#modbus-slave-interface-uart0---scadahmi-control) - SCADA/HMI control
- [Modbus Master Interface (UART1)](#modbus-master-interface-uart1---remote-device-control) - Remote device control

**Core Engines:**
- [Counter Engine](#counter-engine-4-uafhængige-counters) - 4 independent counters (HW/Modbus/GPIO)
- [Timer Engine](#timer-engine-4-uafhængige-timers) - 4 timers (TON/TOF/TP modes)
- [ST Logic Programming](#st-logic-structured-text-programming-v20) ⭐ - IEC 61131-3 Structured Text

**Connectivity:**
- [Network & Wi-Fi](#network--wi-fi-v30) - WiFi AP/STA, Telnet CLI
- [GPIO Mapping](#gpio-mapping) - Digital I/O til Modbus registers

---

### ⭐ Advanced Features

**Data Persistence (v4.0+):**
- [**Persistent Registers**](#persistent-registers-v40) - Save/restore register groups
  - [Group Management](#1-group-management-cli)
  - [Save & Restore Operations](#2-save--restore-operations)
  - [**Auto-Load on Boot**](#3-auto-load-on-boot-v430) ⭐ v4.3.0
  - [ST Logic Integration](#4-st-logic-integration)
  - [Best Practices](#6-best-practices)
  - [Troubleshooting](#8-troubleshooting)

**Config Management:**
- [**Config Backup/Restore**](#config-backuprestore-v607) - Full JSON backup/restore via HTTP API (v6.0.7)

**ST Logic Capabilities:**
- [IEC 61131-3 Type System](#st-logic-structured-text-programming-v20) - INT/DINT/REAL/BOOL (v5.0.0)
- [User-Defined Functions (FEAT-003)](#st-logic-structured-text-programming-v20) - FUNCTION/FUNCTION_BLOCK (v6.1.0) ⭐ NEW
- [Modbus Master Integration](#modbus-master-interface-uart1---remote-device-control) - MB_READ_*/MB_WRITE_* functions
- [Performance Monitoring](#st-logic-structured-text-programming-v20) - Min/Max/Avg execution times
- [Bytecode Debugging](#-cli-commands-reference) - `show logic <id> bytecode` (v4.4.0)

---

### 🛠️ Development & Usage

**Getting Started:**
- [Quick Start](#quick-start-first-boot) - First boot walkthrough
- [Installation & Build](#-installation--build) - PlatformIO setup
- [Hardware Requirements](#-hardware-requirements) - ESP32-WROOM-32 specs

**CLI Interface (v4.4.0):**
- [**CLI Commands Reference**](#-cli-commands-reference) ⭐ NEW - Complete guide (650+ lines)
  - [Quick Reference: `show config` → `set` mapping](#quick-reference-show-config--set-command-mapping)
  - [8 Organized Sections](#show-config-output-organization-v440) - SYSTEM, MODBUS, COUNTERS, TIMERS, GPIO, ST LOGIC, NETWORK, PERSISTENCE
  - [Common Workflows](#common-cli-workflows) - Counter setup, ST upload, persistence
  - [Error Handling](#error-handling--validation)

**Examples & Testing:**
- [Usage Examples](#-usage-examples) - Counter, timer, ST Logic examples
- [Testing Guide](#-testing) - Unit tests, integration tests

**Project Reference:**
- [Project Structure](#-project-structure) - File organization
- [Architecture](#-architecture) - Layered design
- [Specifications](#-specifications) - Technical specs

---

### 📖 Version History & Support

- [**Version History**](#-version-history) - Complete changelog (v4.4.0+)
- [Contributing](#-contributing) - Code standards
- [Support & Issues](#-support--issues) - Bug reporting
- [License](#-license) - MIT License
- [Additional Documentation](#-additional-documentation) - External docs

---

## 🚀 Features

### Modbus Slave Interface (UART0) - SCADA/HMI Control

**ESP32'en som Modbus RTU Slave device** - styres af external SCADA/HMI/PLC systemer.

#### Hardware (UART0)
- **TX:** GPIO16
- **RX:** GPIO17
- **DE/RE:** GPIO15 (RS-485 Direction Enable)

#### Protocol Support
- **Modbus RTU Function Codes:** FC01, FC02, FC03, FC04, FC05, FC06, FC0F (15), FC10 (16)
  - FC01: Read Coils
  - FC02: Read Discrete Inputs
  - FC03: Read Holding Registers
  - FC04: Read Input Registers
  - FC05: Write Single Coil
  - FC06: Write Single Register
  - FC0F: Write Multiple Coils
  - FC10: Write Multiple Registers
- **Configurable Slave ID:** 1-247 (default: 1)
- **Configurable Baudrate:** 300-115200 bps (default: 9600)
- **Parity:** None/Even/Odd (default: None)
- **Stop Bits:** 1 eller 2 (default: 1)

#### Register Map
- **256 Holding Registers** (HR 0-255) - Read/Write
- **256 Input Registers** (IR 0-255) - Read-only
- **256 Coils** (0-255) - Read/Write bits
- **256 Discrete Inputs** (0-255) - Read-only bits

#### Features
- **CRC16-CCITT Validation** på alle frames
- **Timeout Handling:** 3.5 character times (baudrate-adaptive)
- **Error Detection:** CRC mismatch, illegal function, illegal address
- **Auto-mapped Registers:** Counters, timers, ST Logic variabler

#### CLI Configuration
```bash
set modbus-slave id:1 baudrate:9600 parity:none stopbits:1
show modbus-slave  # Vis konfiguration
```

#### Use Cases
- **SCADA Integration:** WinCC, Factory IO, Ignition læser/skriver registers
- **HMI Panels:** Touch screens til operator interface
- **PLC Communication:** Siemens S7, Allen-Bradley kan kommunikere
- **Data Logging:** Historian systemer kan logge counter/timer data
- **Remote Control:** Process automation via Modbus TCP gateway

---

### Modbus Master Interface (UART1) - Remote Device Control

**ESP32'en som Modbus RTU Master** - kan styre eksterne I/O boards, sensorer, og Modbus slave devices.

**⭐ NYT I v4.4.0:** Separat RS-485 interface til distributed I/O expansion!

#### Hardware (UART1)
- **TX:** GPIO25
- **RX:** GPIO26
- **DE/RE:** GPIO27 (RS-485 Direction Enable)
- **Note:** Komplet separat fra UART0 (Modbus Slave) - ingen deling!

#### Protocol Support
- **Modbus RTU Function Codes:** FC01-FC06
  - FC01: Read Coils (fra remote slave)
  - FC02: Read Discrete Inputs (fra remote slave)
  - FC03: Read Holding Registers (fra remote slave)
  - FC04: Read Input Registers (fra remote slave)
  - FC05: Write Single Coil (til remote slave)
  - FC06: Write Single Register (til remote slave)
- **Baudrate:** Konfigurerbar (300-115200, default: 9600)
- **Parity:** None/Even/Odd (default: None)
- **Stop Bits:** 1 eller 2 (default: 1)
- **Timeout:** Konfigurerbar (default: 500ms)
- **Max Requests/Scan:** Konfigurerbar (default: 10)

#### ST Logic Integration (6 Builtin Functions)

**Read Functions (2 argumenter):**
```structured-text
MB_READ_COIL(slave_id, address) → BOOL         (* Read remote coil *)
MB_READ_INPUT(slave_id, address) → BOOL        (* Read remote discrete input *)
MB_READ_HOLDING(slave_id, address) → INT       (* Read remote holding register *)
MB_READ_INPUT_REG(slave_id, address) → INT     (* Read remote input register *)
```

**Write Functions (v4.6.0+ ny syntax - assignment-baseret):**
```structured-text
MB_WRITE_COIL(slave_id, address) := boolean_value      (* Write remote coil *)
MB_WRITE_HOLDING(slave_id, address) := int_value       (* Write remote holding register *)
```

**Type Support (v4.6.1+):**
- **slave_id:** INT, DINT, DWORD (auto-converted til INT med clamping)
- **address:** INT, DINT, DWORD (auto-converted til INT med clamping)
- **MB_WRITE_COIL value:** BOOL, INT, REAL, DINT, DWORD (non-zero → TRUE)
- **MB_WRITE_HOLDING value:** INT, REAL, DINT, DWORD (auto-converted til INT)

**Type Conversion (v4.6.1+):**
- DINT → INT: Clamp til [-32768, 32767]
- DWORD → INT: Clamp til [0, 32767]
- REAL → INT: Truncate (42.9 → 42)
- REAL → BOOL: |x| > 0.001 → TRUE
- INT → BOOL: Non-zero → TRUE, Zero → FALSE

**Note:** Gammel 3-argument syntax (`MB_WRITE_COIL(id, addr, value)`) er deprecated. Brug ny assignment syntax i stedet.

**Global Status Variables:**
```structured-text
mb_last_error (INT)   (* 0=OK, 1=TIMEOUT, 2=CRC, 3=EXCEPTION, 4=MAX_REQ, 5=DISABLED *)
mb_success (BOOL)     (* TRUE if last operation succeeded *)
```

#### Eksempel: Remote Temperature Control (v4.6.0+ ny syntax)
```structured-text
VAR
  remote_temp: INT;
  error: BOOL;
END_VAR

(* Read temperature from slave ID 5, HR#100 *)
remote_temp := MB_READ_HOLDING(5, 100);

(* Check if read succeeded *)
IF mb_success THEN
  (* Control heating based on temperature *)
  IF remote_temp < 18 THEN
    MB_WRITE_COIL(3, 20) := TRUE;   (* Slave 3, Coil 20 ON *)
  ELSE
    MB_WRITE_COIL(3, 20) := FALSE;  (* Slave 3, Coil 20 OFF *)
  END_IF;
  error := FALSE;
ELSE
  (* Communication failed *)
  error := TRUE;
END_IF;
```

**Variable Argument Support (v4.6.0+):**
```structured-text
VAR
  REMOTE_IO: INT := 3;
  COIL_ADDR: INT := 20;
  heating_on: BOOL := TRUE;
END_VAR

(* All arguments can be variables or expressions *)
MB_WRITE_COIL(REMOTE_IO, COIL_ADDR) := heating_on;

(* Dynamic addressing with expressions *)
FOR i := 0 TO 9 DO
  MB_WRITE_HOLDING(2, 200 + i) := i * 10;
END_FOR;
```

**Type Conversion Examples (v4.6.1+):**
```structured-text
VAR
  remote_id: DINT := 3;           (* DINT type *)
  base_addr: DINT := 40000;       (* Large address *)
  temperature: REAL := 72.5;      (* REAL value *)
  enable_flag: INT := 1;          (* INT as boolean *)
  flags: DWORD := 16#ABCD1234;    (* DWORD value *)
END_VAR

(* DINT arguments auto-converted to INT *)
temp := MB_READ_HOLDING(remote_id, base_addr);
(* remote_id: 3 → INT(3) ✓ *)
(* base_addr: 40000 → INT(32767) clamped ✓ *)

(* REAL value auto-converted to INT *)
MB_WRITE_HOLDING(1, 100) := temperature;
(* 72.5 → INT(72) truncated ✓ *)

(* INT value auto-converted to BOOL *)
MB_WRITE_COIL(3, 20) := enable_flag;
(* 1 → BOOL(TRUE) non-zero ✓ *)

(* DWORD value auto-converted to INT *)
MB_WRITE_HOLDING(2, 200) := flags;
(* 0xABCD1234 → INT(0x1234) lower 16 bits ✓ *)
```

#### CLI Configuration
```bash
# Enable Modbus Master
set modbus-master enabled on

# Configure serial port
set modbus-master baudrate 9600
set modbus-master parity none
set modbus-master stop-bits 1
set modbus-master timeout 500

# Configure rate limiting
set modbus-master max-requests 10           # Max 10 requests per ST cycle
set modbus-master inter-frame-delay 10      # 10ms delay between requests

# Show status
show modbus-master

# Help
set modbus-master ?
```

#### Rate Limiting
- **Default:** Max 10 requests per ST execution cycle
- **Purpose:** Prevent bus overload
- **Error:** MB_MAX_REQUESTS_EXCEEDED (code 4) hvis limit overskredet
- **Best Practice:** Cache remote values i lokale variabler

#### Function Codes Supported
- **FC01:** Read Coils (MB_READ_COIL)
- **FC02:** Read Discrete Inputs (MB_READ_INPUT)
- **FC03:** Read Holding Registers (MB_READ_HOLDING)
- **FC04:** Read Input Registers (MB_READ_INPUT_REG)
- **FC05:** Write Single Coil (MB_WRITE_COIL)
- **FC06:** Write Single Register (MB_WRITE_HOLDING)

#### Use Cases
- **Temperature Monitoring:** Læs remote temperature sensorer (slave ID 1-10)
- **I/O Expansion:** Styr eksterne relay boards (16-kanals relay modules)
- **Sensor Integration:** Poll multiple analog input modules
- **Distributed Control:** Control remote actuators baseret på local logic
- **Multi-device Polling:** Scan op til 247 slave devices på samme bus
- **Process Automation:** Koordinér actions mellem multiple Modbus devices

#### Real-World Application Example
```
ESP32 (Modbus Master via UART1)
  ├─ Slave ID 1: Temperature sensor module (HR#0 = temperature)
  ├─ Slave ID 2: Humidity sensor module (HR#0 = humidity)
  ├─ Slave ID 3: 8-channel relay board (Coil#0-7 = relay outputs)
  ├─ Slave ID 4: 16-channel analog input (HR#0-15 = ADC channels)
  └─ Slave ID 5: External counter module (HR#0 = pulse count)

ST Logic Program:
  - Read sensors via MB_READ_HOLDING()
  - Make control decisions
  - Write relay outputs via MB_WRITE_COIL()
  - Export aggregated data via Modbus Slave (UART0) til SCADA
```

---

### Counter Engine (4 Uafhængige Counters)
Hver counter har **3 hardware modes:**

#### **Mode 1: Software Polling (SW)**
- GPIO pin læses i main loop (~1ms rate)
- Edge detection via level change tracking
- CPU overhead: lav (polling)
- Max frequency: ~500Hz (limited by loop rate)
- Debounce: software (konfigurerbar ms delay)

#### **Mode 2: Software ISR (SW-ISR)**
- GPIO interrupt-driven counting
- ESP32 GPIO interrupt på INT0-5
- Edge detection: rising, falling, or both
- Max frequency: ~10kHz
- Debounce: hardware + software

#### **Mode 3: Hardware PCNT (HW)**
- ESP32 Pulse Counter Unit (PCNT)
- Hardware counting uden CPU overhead
- 32-bit counter range
- Max frequency: 40MHz (hardware limit)
- Prescaler: hardware support (1-65535)
- No debounce needed (hardware filters)

**Counter Features:**
- **Prescaler:** 1-65535 (deler counter value før output)
- **Scale Factor:** float multiplier (0.0001-10000.0)
- **Bit Width:** 8, 16, 32, eller 64-bit output (multi-word support)
- **Direction:** up eller down counting
- **Frequency Measurement:** 0-20kHz med 1-sekund update rate
- **Compare Feature (v4.2.4):**
  - Threshold detection med edge detection (≥, >, ==)
  - Rising edge trigger (kun sæt ved threshold crossing)
  - Runtime modificerbar threshold via Modbus
  - Auto-reset on read (ctrl-reg eller value-reg)
  - Output til control register bit 4
- **Register Outputs (v4.4.3 - Optimized Layout):**
  - Counter 1: HR100-110 (11 registers total, 64-bit max)
  - Counter 2: HR120-130
  - Counter 3: HR140-150
  - Counter 4: HR160-170
  - **Per counter (32-bit example):**
    - HR100-101: Value (scaled value, 2 words, LSW first)
    - HR104-105: Raw (prescaled value, 2 words, LSW first)
    - HR108: Frequency (Hz, 1 word)
    - HR110: Control register (bit3=overflow, bit4=compare-match, bit2=running)
    - HR111-114: Compare value (4 words for 64-bit, runtime writable)

### Timer Engine (4 Uafhængige Timers)
Hver timer har **4 modes:**

#### **Mode 1: One-shot (3-phase sequence)**
- Sekventiel 3-phase timing
- Phase 1: duration_ms, output_state
- Phase 2: duration_ms, output_state
- Phase 3: duration_ms, output_state
- Auto-stop efter phase 3
- Trigger via control register bit 1

#### **Mode 2: Monostable (Retriggerable Pulse)**
- Single pulse output
- Retriggerable (pulse extends on new trigger)
- Rest state og pulse state konfigurerbar
- Pulse duration: 1ms - 4294967295ms (49 days max)
- Trigger via control register bit 1

#### **Mode 3: Astable (Oscillator/Blink)**
- Kontinuerlig toggle mellem ON og OFF states
- ON duration og OFF duration konfigurerbar uafhængigt
- Start/stop via control register
- Perfekt til LED blink, PWM simulation, watchdog

#### **Mode 4: Input-Triggered (Edge Detection)**
- Monitorer discrete input for edge
- Trigger edge: rising (0→1) eller falling (1→0)
- Delay efter edge detection (0ms - 4294967295ms)
- Output level efter delay
- Auto-reset eller hold state (konfigurerbar)

**Timer Features:**
- **Output:** Coil register (0-255)
- **Control Register:** Start (bit 1), Stop (bit 2), Reset (bit 0)
- **Timing Precision:** ±1ms (millis() baseret)
- **Status Readback:** current phase, running state, output state
- **Persistent Configuration:** gemmes til NVS

### ST Logic Programming (Structured Text) - v4.1.0
4 uafhængige logic programmer med IEC 61131-3 ST syntax og **advanced performance monitoring**:

**Language Features:**
- **Variable Types:** INT, BOOL, REAL (16-bit, 1-bit, float)
- **Variable Visibility:** EXPORT keyword (v5.1.0) - mark variables for Modbus IR 220-251 exposure
- **Operators:** +, -, *, /, MOD, AND, OR, NOT, XOR
- **Comparisons:** =, <>, <, >, <=, >=
- **Control Flow:** IF/THEN/ELSIF/ELSE, WHILE, FOR, REPEAT/UNTIL
- **Variable Sections:** VAR_INPUT, VAR_OUTPUT, VAR (persistent)
- **User Functions (v6.1.0):** FUNCTION (stateless), FUNCTION_BLOCK (stateful) - IEC 61131-3
- **Comments:** (* multi-line *) og // single-line
- **Built-in Functions:**
  - Math: `ABS()`, `SQRT()`, `MIN()`, `MAX()`, `SUM()`, `LIMIT()`, `SEL()`, `MUX()` ⭐ NEW v4.8.4
  - Exponential (v4.7.0): `EXP()`, `LN()`, `LOG()`, `POW()`
  - Type Conversion: `INT_TO_REAL()`, `REAL_TO_INT()`, `BOOL_TO_INT()`, `DWORD_TO_INT()`
  - Trigonometry: `SIN()`, `COS()`, `TAN()`, `ROUND()`, `TRUNC()`, `FLOOR()`, `CEIL()`
  - Bit Rotation (v4.8.4): `ROL()`, `ROR()` ⭐ NEW
  - Edge Detection (v4.7.0): `R_TRIG()`, `F_TRIG()`
  - Timers (v4.7.0): `TON()`, `TOF()`, `TP()`
  - Counters (v4.7.0): `CTU()`, `CTD()`, `CTUD()`
  - Latches (v4.7.3): `SR()`, `RS()` ⭐ NEW
  - Signal Processing (v4.8): `SCALE()`, `HYSTERESIS()`, `BLINK()`, `FILTER()`
  - Persistence: `SAVE(group_id)` → INT, `LOAD(group_id)` → INT (0=OK, -1=error, -2=rate limited)
  - Modbus Master: `MB_READ_COIL()`, `MB_READ_HOLDING()`, `MB_WRITE_COIL()`, `MB_WRITE_HOLDING()`

**Compiler & Runtime:**
- **Bytecode Compilation:** Real-time compilation ved upload
- **Zero Interpreter Overhead:** Direct bytecode execution
- **Fixed Rate Scheduler:** Deterministic 10ms execution cycle (±1ms jitter)
- **Dynamic Interval Control:** Adjust execution interval (10/20/25/50/75/100 ms)
- **Variable Binding:** ST variables ↔ Modbus registers/coils (with type checking)
- **Program Size:** Max 2KB source code per program
- **Bytecode Size:** Max 1KB compiled bytecode per program
- **Error Handling:** Compile errors with line numbers, runtime timeout protection

**Performance Monitoring (v4.1.0):**
- **Execution Statistics:** Min/Max/Avg execution time (microsecond precision)
- **Overrun Tracking:** Counts executions where time > target interval
- **Global Cycle Stats:** Min/Max cycle time, total cycles executed
- **Performance Ratings:** EXCELLENT/GOOD/ACCEPTABLE/POOR with auto recommendations
- **CLI Commands:**
  - `show logic stats` - Display all program performance statistics
  - `show logic X timing` - Detailed analysis for specific program
  - `set logic stats reset [all|cycle|1-4]` - Reset statistics
  - `set logic interval:X` - Change execution interval dynamically
- **Modbus Access:** Read statistics from IR 252-293, control interval via HR 236-237

**Debugger (v5.3.0 - FEAT-008):** ⭐ NEW
- **Pause/Continue:** Stop and resume program execution at any time
- **Single-Step:** Execute one bytecode instruction at a time
- **Breakpoints:** Set up to 8 breakpoints per program at PC addresses
- **Variable Inspection:** View all variable values when paused
- **Instruction View:** See current instruction at PC
- **CLI Commands:**
  - `set logic <id> debug pause` - Pause at next instruction
  - `set logic <id> debug continue` - Continue until breakpoint/halt
  - `set logic <id> debug step` - Execute one instruction
  - `set logic <id> debug stop` - Stop debugging
  - `set logic <id> debug break <pc>` - Add breakpoint
  - `set logic <id> debug clear [<pc>]` - Clear breakpoint(s)
  - `show logic <id> debug` - Show debug state
  - `show logic <id> debug vars` - Show variable values
- **Documentation:** See [ST_DEBUG_GUIDE.md](ST_DEBUG_GUIDE.md) for detailed usage

**User-Defined Functions (v6.1.0 - FEAT-003):** ⭐ NEW
IEC 61131-3 kompatible brugerdefinerede FUNCTION og FUNCTION_BLOCK:

- **FUNCTION (stateless):** Rene funktioner der returnerer en værdi
  ```
  FUNCTION DOUBLE : INT
  VAR_INPUT val : INT; END_VAR
  BEGIN
    DOUBLE := val * 2;
  END_FUNCTION
  ```
- **FUNCTION_BLOCK (stateful):** Funktionsblokke med persistent state mellem kald
  ```
  FUNCTION_BLOCK MY_COUNTER
  VAR_INPUT en : BOOL; END_VAR
  VAR_OUTPUT cv : INT; END_VAR
  VAR cnt : INT; END_VAR        (* Bevares mellem kald *)
  BEGIN
    IF en THEN cnt := cnt + 1; END_IF;
    cv := cnt;
  END_FUNCTION_BLOCK
  ```
- **Features:**
  - IEC 61131-3 return-semantik (returner via funktionsnavn)
  - Multiple parametre (VAR_INPUT, op til 8 per funktion)
  - Lokale variabler (VAR, op til 16 per funktion)
  - Nested function calls (op til 8 niveauer dyb)
  - FUNCTION_BLOCK instanser med uafhængig state (som TON/CTU)
  - Two-pass compilation (funktioner defineres før de kaldes)
  - Op til 16 user-defined functions og 16 FB instanser per program
- **CLI Commands:**
  - `show logic <id> functions` - Vis registrerede user-funktioner
  - `show logic <id> bytecode` - Viser CALL_USER, RETURN, LOAD_PARAM opcodes
- **Limits:**
  | Konstant | Værdi |
  |----------|-------|
  | Max funktioner per program | 16 |
  | Max parametre per funktion | 8 |
  | Max lokale variabler per funktion | 16 |
  | Max nestede kald | 8 |
  | Max FUNCTION_BLOCK instanser | 16 |
- **Documentation:** See [FEAT-003_PLAN.md](FEAT-003_PLAN.md) for implementation details
- **Tests:** See [tests/ST_TEST_FUNCTIONS.md](tests/ST_TEST_FUNCTIONS.md) for 14 test cases

**Variable I/O Binding:**
```
ST Variable ↔ Holding Register (read/write)
ST Variable ↔ Input Register (read-only)
ST Variable ↔ Coil (read/write bit)
ST Variable ↔ Discrete Input (read-only bit)
```

**CLI Commands:**
```bash
# Program Management
set logic <id> upload            # Upload ST source code
set logic <id> enabled:true      # Enable program execution
set logic <id> enabled:false     # Disable program
set logic <id> delete            # Delete program
set logic <id> bind <var> reg:X  # Bind variable to register

# Performance Monitoring (v4.1.0)
show logic stats                 # All programs performance stats
show logic <id> timing           # Detailed timing analysis
show logic <id>                  # Specific program details
show logic <id> functions        # Show user-defined functions (FEAT-003)
set logic stats reset all        # Reset all statistics
set logic stats reset cycle      # Reset global cycle stats
set logic stats reset <1-4>      # Reset specific program stats

# Execution Control (v4.1.0)
set logic interval:10            # Set execution interval (10/20/25/50/75/100 ms)
set logic debug:true             # Enable debug output
set logic debug:false            # Disable debug output
```

### Networking Features (v3.0+)

#### Wi-Fi Client Mode
- **DHCP Support:** Automatic IP assignment
- **Static IP Support:** Manual IP, gateway, netmask, DNS configuration
- **WPA/WPA2 Authentication:** Password-protected networks
- **SSID:** Max 32 characters
- **Password:** 8-63 characters (WPA2 requirement)
- **Connection Status:** Real-time monitoring via `show wifi`
- **Auto-Reconnect:** Automatic reconnection on disconnect
- **IP Display:** Shows assigned IP (DHCP or static)

#### Telnet Server
- **Port:** 23 (default, konfigurerbar)
- **Authentication:** Username/password (optional)
  - Default: empty username, empty password (disabled)
  - Configurable via CLI: `set wifi telnet-user`, `set wifi telnet-pass`
- **Concurrent Connections:** 1 simultaneous client
- **Line Editing:** Arrow keys (←→) for cursor movement
- **Insert Mode:** Character insertion without overwriting
- **Command History:** Arrow keys (↑↓) for history navigation
- **Echo Control:** Configurable remote echo (on/off)
- **Graceful Disconnect:** `exit` command
- **Session Timeout:** Configurable inactivity timeout

#### HTTP REST API (v6.0.0+)
- **Port:** 80 (default, konfigurerbar)
- **Protocol:** HTTP/1.1 med JSON responses
- **Authentication:** Optional Basic Auth
- **Use Cases:**
  - Node-RED integration via HTTP Request nodes
  - Web dashboards og visualisering
  - Third-party system integration
  - Remote monitoring og control
- **Endpoints:** Se [docs/REST_API.md](docs/REST_API.md) for komplet dokumentation
- **Quick Test:**
```bash
# System status
curl http://192.168.1.100/api/status

# Læs alle counters
curl http://192.168.1.100/api/counters

# Læs holding register 100
curl http://192.168.1.100/api/registers/hr/100

# Skriv til holding register
curl -X POST -H "Content-Type: application/json" \
     -d '{"value": 12345}' \
     http://192.168.1.100/api/registers/hr/100
```
- **CLI Configuration:**
```bash
set http enabled on|off        # Enable/disable HTTP server
set http port <port>           # Set HTTP port (default: 80)
set http auth on|off           # Enable/disable Basic Auth
set http username <user>       # Set auth username
set http password <pass>       # Set auth password
show http                      # Show HTTP status & statistics
show backup                   # Show backup/restore URLs & instructions
```

#### Config Backup/Restore (v6.0.7)

Full configuration backup and restore via HTTP API. Backup includes ALL settings: Modbus slave/master, network/WiFi (incl. passwords), HTTP, telnet, counters, timers, static/dynamic registers, coils, variable mappings, persistent register groups, and ST Logic source code.

**Download backup:**
```bash
# Browser: open URL directly (downloads as backup.json)
# curl:
curl -u api_user:password http://192.168.1.100/api/system/backup -o backup.json
```

**Restore from backup:**
```bash
curl -u api_user:password -X POST \
     -H "Content-Type: application/json" \
     -d @backup.json \
     http://192.168.1.100/api/system/restore
```

**CLI:**
```bash
show backup    # Shows download URL with current IP + curl examples
```

**Details:**
- **Backup format:** JSON (~12-20KB depending on config)
- **Auth required:** Yes (Basic Auth if enabled)
- **ST Logic:** Source code included as plain text, auto-compiled on restore
- **Passwords:** WiFi, HTTP, and telnet passwords included (needed for full restore)
- **Restore behavior:** Full replace (not merge), saves to NVS + SPIFFS, calls `config_apply()`
- **Versioning:** `backup_version: 1` field for future compatibility

#### Network Configuration Persistence
- **NVS Storage:** All network settings saved to non-volatile storage
- **CRC Validation:** Data integrity check on load
- **Schema Migration:** Automatic backward compatibility
- **Configuration Commands:**
```bash
set wifi ssid <name>           # Set Wi-Fi SSID
set wifi password <pass>       # Set Wi-Fi password
set wifi dhcp on|off           # Enable/disable DHCP
set wifi ip <addr>             # Static IP (e.g., 192.168.1.100)
set wifi gateway <addr>        # Gateway IP
set wifi netmask <addr>        # Netmask (e.g., 255.255.255.0)
set wifi dns <addr>            # DNS server
set wifi telnet enable         # Enable telnet server
set wifi telnet-port <port>    # Set telnet port
set wifi telnet-user <user>    # Set telnet username
set wifi telnet-pass <pass>    # Set telnet password
save                           # Persist to NVS
```

### CLI Interface (Command Line Interface)

#### Serial Console (USB)
- **Port:** UART0 (USB CDC)
- **Baudrate:** 115200 bps (fixed)
- **Line Ending:** CR+LF (\\r\\n)
- **Buffer:** 256 bytes input buffer
- **Echo:** Always ON (local echo)

#### Telnet Console (Network)
- **Port:** 23 (default)
- **Authentication:** Username/password
- **Line Editing:** Full cursor control
- **Remote Echo:** Configurable (on/off)
- **Buffer:** 256 bytes input buffer

#### CLI Commands (~53 funktioner)

**Configuration Commands:**
```bash
set id <1-247>                 # Set Modbus slave ID
set baud <300-115200>          # Set Modbus baudrate
set hostname <name>            # Set system hostname (max 31 chars)
set echo on|off                # Enable/disable remote echo
save                           # Save config to NVS
load                           # Load config from NVS (auto on boot)
reset                          # Reset to factory defaults
```

**Show Commands (Status & Monitoring):**
```bash
show config                    # Full system configuration
show version                   # Firmware version & build info
show counters                  # All counters status (table)
show counter <1-4>             # Specific counter detailed status
show timers                    # All timers status (table)
show timer <1-4>               # Specific timer detailed status
show logic                     # All ST Logic programs status
show logic <id>                # Specific program details
show logic <id> code           # Show compiled bytecode
show registers [start] [count] # Holding registers (0-255)
show inputs [start] [count]    # Input registers (0-255)
show coils [start] [count]     # Coils (0-255)
show gpio                      # GPIO mappings
show wifi                      # Wi-Fi connection status
show modbus-master             # Modbus Master status & stats (v4.4+)
show debug                     # Debug flags status
show echo                      # Echo status
```

**Counter Commands (v4.2.4):**
```bash
# Configure counter (register addresses AUTO-ASSIGNED)
# NOTE: Register addresses are NOW READ-ONLY smart defaults (v4.2.4+)
set counter <id> mode <1-3> hw-mode:<sw|sw-isr|hw> \
  edge:<rising|falling|both> prescaler:<1-65535> \
  scale:<float> bit-width:<8|16|32|64> \
  direction:<up|down>

# Mode-specific configuration
set counter <id> mode 1 input-dis:<idx>        # SW mode: discrete input
set counter <id> mode 2 interrupt-pin:<gpio>   # SW-ISR mode: GPIO interrupt
set counter <id> mode 3 hw-gpio:<gpio>         # HW mode: PCNT unit

# Compare feature (v4.2.4 - BUG-029, BUG-030)
set counter <id> compare:on compare-value:<val> compare-mode:<0|1|2>
  # Mode 0: ≥ (greater-or-equal, rising edge trigger)
  # Mode 1: > (strictly greater, rising edge trigger)
  # Mode 2: === (exact match, rising edge trigger)

# Control commands (v4.2.9 - BUG-041)
set counter <id> control counter-reg-reset-on-read:<on|off>   # Reset counter når value regs læses
set counter <id> control compare-reg-reset-on-read:<on|off>   # Auto-clear compare bit 4
set counter <id> control auto-start:<on|off>                  # Start counter ved boot
set counter <id> control running:<on|off>                     # Start/stop counter

# Control via Modbus FC06 write to ctrl-reg
write h-reg <ctrl-reg> value 0x0001      # Reset counter (bit 0, auto-clears)
write h-reg <ctrl-reg> value 0x0002      # Start counter (bit 1, auto-clears)

# Read status flags from ctrl-reg
read h-reg <ctrl-reg>                     # Read control register
# Bit 0: Reset command (write-only)
# Bit 1: Start command (write-only)
# Bit 2: Running status (read-only: 1=running, 0=stopped)
# Bit 3: Overflow flag (read-only: 1=overflow, 0=normal)
# Bit 4: Compare match (read-only: 1=threshold reached, 0=below)
# Bit 7: Direction (read-only: 0=up, 1=down)

# Enable/disable (Cisco-style)
set counter <id> enable:<on|off>       # Enable/disable counter
no set counter <id>                    # Delete counter (reset to defaults)

# Read compare value from Modbus (v4.2.4 - BUG-030)
read h-reg <base+11> <words>             # Counter 1: HR111, Counter 2: HR131, etc.
write h-reg <base+11> value <threshold>  # Modify compare threshold runtime
```

**Register Layout (v4.2.4 - Smart Defaults, BUG-028):**
```
Counter 1: HR100-114 (index+raw+freq+overload+ctrl+compare, 5 reserved)
Counter 2: HR120-134
Counter 3: HR140-154
Counter 4: HR160-174

Example (Counter 1, 32-bit):
  HR100-101: Index (scaled, 2 words)
  HR104-105: Raw (prescaled, 2 words)
  HR108:     Frequency (Hz, 1 word)
  HR109:     Overload flag (1 word)
  HR110:     Control register (1 word):
             - Bit 0: counter-reg-reset-on-read flag (persistent) / one-shot reset cmd
             - Bit 1: auto-start flag (persistent) / one-shot start cmd
             - Bit 2: one-shot stop cmd (write-only, auto-clears)
             - Bit 4: compare-match status (read-only)
             - Bit 7: running state (persistent)
  HR111-112: Compare value (2 words, writable via FC06/FC16)
```

**Timer Commands:**
```bash
# Mode 1: One-shot
set timer <id> mode 1 parameter \
  phase1-duration:<ms> phase1-output:<0|1> \
  phase2-duration:<ms> phase2-output:<0|1> \
  phase3-duration:<ms> phase3-output:<0|1> \
  output-coil:<0-255> ctrl-reg:<0-255>

# Mode 2: Monostable
set timer <id> mode 2 parameter \
  pulse-duration:<ms> trigger-level:<0|1> \
  phase1-output:<0|1> phase2-output:<0|1> \
  output-coil:<0-255> ctrl-reg:<0-255>

# Mode 3: Astable
set timer <id> mode 3 parameter \
  on-ms:<ms> off-ms:<ms> \
  phase1-output:<0|1> phase2-output:<0|1> \
  output-coil:<0-255> ctrl-reg:<0-255>

# Mode 4: Input-triggered
set timer <id> mode 4 parameter \
  input-dis:<idx> delay-ms:<ms> \
  trigger-edge:<rising|falling> \
  phase1-output:<0|1> \
  output-coil:<0-255> ctrl-reg:<0-255>

set timer <id> control start                             # Start timer
set timer <id> control stop                              # Stop timer
set timer <id> control reset                             # Reset timer
set timer <id> enable on|off                             # Enable/disable
```

**GPIO Mapping Commands:**
```bash
set gpio <pin> input <idx>    # GPIO input → discrete input
set gpio <pin> coil <idx>     # Coil → GPIO output
no set gpio <pin>             # Remove GPIO mapping
```

**ST Logic Commands:**
```bash
set logic <id> upload                    # Upload ST source (multiline)
set logic <id> enable on|off             # Enable/disable program
set logic <id> delete                    # Delete program
set logic <id> bind <var> reg:<addr>     # Bind variable to register
set logic <id> bind <var> coil:<addr>    # Bind variable to coil
```

**Modbus Master Commands (v4.4+):**
```bash
# Configuration
set modbus-master enabled on|off                # Enable/disable Modbus Master
set modbus-master baudrate <rate>               # Set baudrate (default: 9600)
set modbus-master parity <none|even|odd>        # Set parity (default: none)
set modbus-master stop-bits <1|2>               # Set stop bits (default: 1)
set modbus-master timeout <ms>                  # Set timeout (default: 500ms)
set modbus-master inter-frame-delay <ms>        # Inter-frame delay (default: 10ms)
set modbus-master max-requests <count>          # Max requests per cycle (default: 10)

# Status & Help
show modbus-master                              # Show config & statistics
set modbus-master ?                             # Detailed help menu
```

**ST Logic Modbus Master Functions:**
```structured-text
(* Available in ST Logic programs - see section above for examples *)
MB_READ_COIL(slave_id, address) → BOOL
MB_READ_INPUT(slave_id, address) → BOOL
MB_READ_HOLDING(slave_id, address) → INT
MB_READ_INPUT_REG(slave_id, address) → INT

(* v4.6.0+ New assignment syntax for writes *)
MB_WRITE_COIL(slave_id, address) := boolean_value
MB_WRITE_HOLDING(slave_id, address) := int_value

(* v4.6.1+ Type support - All types auto-converted *)
(* slave_id, address: INT/DINT/DWORD → INT (clamped) *)
(* MB_WRITE_COIL value: BOOL/INT/REAL/DINT/DWORD → BOOL (non-zero=TRUE) *)
(* MB_WRITE_HOLDING value: INT/REAL/DINT/DWORD → INT (converted) *)

(* Global status variables *)
mb_last_error (INT)   (* 0=OK, 1=TIMEOUT, 2=CRC, 3=EXCEPTION, 4=MAX_REQ, 5=DISABLED *)
mb_success (BOOL)     (* TRUE if last operation succeeded *)
```

**Register/Coil Direct Access:**
```bash
# READ COMMANDS
read h-reg <addr> [count] [int|uint|real]  # Read holding register(s)
                                            # - int:  signed 16-bit (-32768 to 32767)
                                            # - uint: unsigned 16-bit (0 to 65535, default)
                                            # - real: 32-bit float (uses 2 consecutive registers)
read i-reg <addr> [count] [int|uint|real]  # Read input register(s)
read input <addr>                          # Read discrete input
read coil <addr>                           # Read coil

# WRITE COMMANDS
write h-reg <addr> value int <val>         # Write signed 16-bit INT (-32768 to 32767)
write h-reg <addr> value uint <val>        # Write unsigned 16-bit (0 to 65535)
write h-reg <addr> value dint <val>        # Write signed 32-bit DINT (-2147483648 to 2147483647, uses 2 registers)
write h-reg <addr> value dword <val>       # Write unsigned 32-bit DWORD (0 to 4294967295, uses 2 registers)
write h-reg <addr> value real <val>        # Write 32-bit REAL/float (uses 2 consecutive registers)
write coil <addr> value <on|off>           # Write coil

# EXAMPLES
read h-reg 100 int                         # Read reg 100 as signed INT
read h-reg 100 5 uint                      # Read 5 registers from 100 as unsigned
read h-reg 100 real                        # Read reg 100-101 as REAL (32-bit float)
read i-reg 220 int                         # Read input register 220 as signed INT

write h-reg 100 value int -500             # Write -500 as signed INT
write h-reg 100 value uint 65000           # Write 65000 as unsigned
write h-reg 100 value dint 100000          # Write 100000 as DINT (reg 100-101, LSW first)
write h-reg 100 value dword 3000000000     # Write 3000000000 as DWORD (reg 100-101, LSW first)
write h-reg 100 value real 3.14159         # Write 3.14159 as REAL (reg 100-101)
```

**System Commands:**
```bash
help                                     # Show command help
exit                                     # Exit telnet session (telnet only)
```

### System Features

#### Persistent Configuration (NVS Storage)
- **Storage Backend:** ESP32 NVS (Non-Volatile Storage)
- **CRC Protection:** CRC16-CCITT checksum on all saved data
- **Schema Versioning:** v8 (current)
  - v5 → v6: Added `hostname` field
  - v6 → v7: Added `remote_echo` field
  - v7 → v8: Added `persist_regs` (persistence groups)
- **Auto-Migration:** Old configs auto-upgrade on schema mismatch
- **Config Size:** ~30KB (PersistConfig struct)
- **Save Command:** `save` (manual)
- **Load:** Automatic on boot
- **Factory Reset:** `reset` command or schema corruption detection

**Saved Configuration:**
- Modbus slave ID & baudrate
- System hostname (max 31 chars)
- Remote echo setting (on/off)
- Wi-Fi settings (SSID, password, IP config, telnet config)
- All 4 counters (configuration, not values)
- All 4 timers (configuration, not state)
- All ST Logic programs (source code + bytecode)
- GPIO mappings (64 slots)
- GPIO2 user mode (heartbeat on/off)

#### GPIO2 Heartbeat LED
- **Default:** Enabled (LED blink at 500ms interval)
- **User-Controllable:** `set gpio 2 enable|disable`
- **Purpose:** Visual indication of firmware running
- **Pin:** GPIO2 (onboard LED på de fleste ESP32 boards)
- **Mode:** Toggle at 500ms intervals
- **Disable:** Frigør GPIO2 til user applications

---

#### Persistent Registers (v4.0+)

> **📌 Quick Links:**
> [Group Management](#1-group-management-cli) | [Save & Restore](#2-save--restore-operations) | [**Auto-Load (v4.3.0)** ⭐](#3-auto-load-on-boot-v430) | [ST Logic Integration](#4-st-logic-integration) | [Best Practices](#6-best-practices) | [Troubleshooting](#8-troubleshooting)

**Overview:**
The Persistent Registers system allows you to save selected Modbus holding register values to ESP32 Non-Volatile Storage (NVS flash) and restore them after reboot/power cycle. Unlike the main configuration (which saves all timers, counters, and settings), this feature provides selective, runtime-controlled persistence for process data.

**Key Features:**
- **Named Groups:** Organize up to 8 groups, each containing up to 16 registers
- **Selective Persistence:** Save/restore specific groups or all groups
- **Runtime Control:** Trigger saves from ST Logic programs or CLI
- **Auto-Load on Boot:** (v4.3.0) Automatically restore configured groups at startup
- **NVS Storage:** Survives power cycles, reboots, and firmware updates
- **Rate Limiting:** Protection against excessive NVS writes (max 1 save per 5 seconds)

**Common Use Cases:**
- Sensor calibration coefficients (offset, scale, linearity)
- Production counters (total parts produced, batch count)
- Recipe/setpoint management (temperature, pressure, flow rates)
- Last known good configuration backup
- User-defined runtime parameters

---

##### 1. Group Management (CLI)

**Create a Group:**
```bash
# Create group with range notation (recommended)
> set persist group "sensors" add 100-105
✓ Added 6 registers to group 'sensors'

# Or use comma-separated values
> set persist group "calibration" add 200,202,204,206
✓ Added 4 registers to group 'calibration'

# Or combine ranges and individual values
> set persist group "production" add 100-105,110,112,120-122
✓ Added 10 registers to group 'production'

# Old syntax still works (individual arguments)
> set persist group "legacy" add 150 151 152
✓ Added 3 registers to group 'legacy'
```

**Remove Registers:**
```bash
> set persist group "sensors" remove 104
✓ Removed register 104 from group 'sensors'
```

**Delete Entire Group:**
```bash
> set persist group "sensors" delete
✓ Deleted group 'sensors'
```

**View All Groups:**
```bash
> show persist

=== Persistent Registers (v4.0+) ===
System: ENABLED
Groups: 2/8
Auto-Load: ENABLED (groups: #1, #2)

Group #1 "calibration" (3 registers)
  HR100 = 42    (offset)
  HR101 = 100   (scale)
  HR102 = 5     (deadband)
  Last save: 2 minutes ago

Group #2 "production" (2 registers)
  HR200 = 1523  (total_parts)
  HR201 = 47    (batch_count)
  Last save: 10 seconds ago
```

**Enable/Disable System:**
```bash
> set persist enable on
Persistence system ENABLED

> set persist enable off
Persistence system DISABLED
```

---

##### 2. Save & Restore Operations

**Save to RAM (Snapshot):**
```bash
# Save specific group (copies current register values to group buffer)
> save registers group "calibration"
✓ Saved group 'calibration' (3 registers)

# Save all groups
> save registers all
✓ Saved 2 groups (5 registers total)
```

**Persist to NVS Flash:**
```bash
# IMPORTANT: Use 'save' command to write to NVS
> save
✓ Configuration saved to NVS (including persist groups)
```

**Restore from Group Buffer:**
```bash
# Restore specific group (writes saved values back to holding registers)
> load registers group "calibration"
✓ Restored group 'calibration' (3 registers)

# Restore all groups
> load registers all
✓ Restored 2 groups (5 registers total)
```

**Complete Workflow:**
```bash
# 1. Create group (using range notation)
> set persist group "recipe" add 150-152

# 2. Write values to registers
> write h-reg 150 value 250   # Temperature setpoint
> write h-reg 151 value 80    # Pressure setpoint
> write h-reg 152 value 30    # Flow rate setpoint

# 3. Save group (snapshot to RAM)
> save registers group "recipe"

# 4. Persist to NVS
> save

# 5. After reboot, restore manually
> load registers group "recipe"
✓ HR150-152 restored with saved values
```

---

##### 3. Auto-Load on Boot (v4.3.0)

**Problem Solved:**
Before v4.3.0, users had to manually run `load registers group <name>` after every reboot to restore saved values. This was impractical for industrial automation where devices must be operational immediately after power-on.

**Solution:**
Configure which groups should automatically restore at boot, eliminating manual intervention.

**Setup Auto-Load:**
```bash
# Step 1: Enable auto-load system
> set persist auto-load enable
Auto-load on boot: ENABLED

# Step 2: Add groups to auto-load list (by Group ID, see 'show persist')
> set persist auto-load add 1    # Group #1 "calibration"
✓ Added group #1 to auto-load list

> set persist auto-load add 2    # Group #2 "production"
✓ Added group #2 to auto-load list

# Step 3: Save configuration to NVS
> save
✓ Configuration saved to NVS
```

**Remove from Auto-Load:**
```bash
> set persist auto-load remove 1
✓ Removed group #1 from auto-load list

> set persist auto-load disable
Auto-load on boot: DISABLED
```

**Boot Sequence Behavior:**
When auto-load is enabled, the system automatically executes during boot (after config load, before main loop):
1. Reads auto-load configuration from NVS
2. Restores each configured group in order (Group #1, then #2, etc.)
3. Prints boot message: `Auto-loaded 2 persistent register group(s) from NVS`
4. Proceeds with normal startup

**Limitations:**
- Max 7 groups can be configured for auto-load (hardware limitation)
- Groups must exist in configuration to be auto-loaded
- If a group is deleted, it's automatically removed from auto-load list

---

##### 4. ST Logic Integration

**Built-in Functions:**
- `SAVE(group_id)` - Save group to NVS (0 = all groups, 1-8 = specific group)
- `LOAD(group_id)` - Restore group from NVS (0 = all groups, 1-8 = specific group)

**Return Values:**
- `0` = Success
- `-1` = Error (group not found, NVS error, persistence disabled)
- `-2` = Rate limit exceeded (SAVE only, max 1 per 5 seconds)

**Example 1: Calibration Save on Trigger**
```st
PROGRAM CalibrationManager
VAR
  calibration_mode: BOOL;   (* HR200: Enter calibration mode *)
  save_cal: BOOL;           (* HR201: Trigger save *)
  cal_offset: INT;          (* HR202: Calibration offset *)
  cal_scale: INT;           (* HR203: Calibration scale *)
  save_result: INT;         (* HR204: Save status *)
END_VAR

BEGIN
  (* Save calibration when triggered *)
  IF save_cal AND calibration_mode THEN
    save_result := SAVE(1);  (* Save group #1 "calibration" *)

    IF save_result = 0 THEN
      save_cal := 0;  (* Clear trigger on success *)
    END_IF;
  END_IF;

  (* Auto-restore on startup *)
  IF NOT calibration_mode THEN
    save_result := LOAD(1);  (* Restore group #1 *)
  END_IF;
END
```

**Example 2: Production Counter with Periodic Save**
```st
PROGRAM ProductionCounter
VAR
  part_complete: BOOL;      (* HR210: Pulse when part done *)
  total_parts: DWORD;       (* HR211-212: Total counter *)
  last_save_count: DWORD;   (* HR213-214: Counter at last save *)
  save_interval: INT := 100; (* Save every 100 parts *)
END_VAR

BEGIN
  (* Increment on part complete *)
  IF part_complete THEN
    total_parts := total_parts + 1;
    part_complete := 0;
  END_IF;

  (* Periodic save every 100 parts *)
  IF (total_parts - last_save_count) >= save_interval THEN
    IF SAVE(2) = 0 THEN  (* Save group #2 "production" *)
      last_save_count := total_parts;
    END_IF;
  END_IF;
END
```

**Rate Limiting Protection:**
```st
VAR
  save_timer: TON;
  save_request: BOOL;
END_VAR

BEGIN
  (* Debounce save requests (min 5 seconds between saves) *)
  save_timer(IN := save_request, PT := T#5s);

  IF save_timer.Q THEN
    IF SAVE(1) = 0 THEN
      save_request := 0;  (* Clear after successful save *)
    END_IF;
  END_IF;
END
```

---

##### 5. Group ID Reference

Group IDs are assigned automatically in order of creation (1-8). Use `show persist` to see IDs:

```bash
> show persist

Group #1 "sensors"     ← Use SAVE(1) / LOAD(1)
Group #2 "calibration" ← Use SAVE(2) / LOAD(2)
Group #3 "setpoints"   ← Use SAVE(3) / LOAD(3)
```

**Special ID: 0 = All Groups**
```st
SAVE(0);  (* Save ALL groups at once *)
LOAD(0);  (* Restore ALL groups *)
```

---

##### 6. Best Practices

**DO:**
- ✅ Use meaningful group names ("cal_temp", "recipe_A", "counters")
- ✅ Group related registers together (all calibration in one group)
- ✅ Use auto-load for critical startup data (calibration, recipes)
- ✅ Implement rate limiting in ST Logic (max 1 save per 5 seconds)
- ✅ Verify SAVE() return value before clearing triggers
- ✅ Use `save registers` + `save` workflow (snapshot then persist)

**DON'T:**
- ❌ Save on every PLC cycle (NVS has ~100k write limit per sector)
- ❌ Mix process data with configuration (use persist groups for runtime data)
- ❌ Auto-load groups with frequently changing data
- ❌ Exceed 16 registers per group (hard limit)
- ❌ Create more than 8 groups (hard limit)

**NVS Write Endurance:**
- ESP32 NVS flash: ~100,000 write cycles per sector
- With rate limiting (1 save per 5s): ~5.7 years of continuous saves
- Recommended: Save on events (recipe change, calibration update) not periodic

---

##### 7. Technical Details

**Storage Location:**
- Groups stored in `PersistConfig` struct (NVS partition "config")
- Each group: 16-byte name + 16x uint16_t addresses + 16x uint16_t values
- Total size: ~1.5 KB for all 8 groups
- Survives firmware updates (NVS partition preserved)

**Memory Layout:**
```cpp
typedef struct {
  char name[16];                     // Group name (null-terminated)
  uint8_t reg_count;                 // 0-16
  uint16_t reg_addresses[16];        // Register addresses
  uint16_t reg_values[16];           // Saved values
  uint32_t last_save_ms;             // Timestamp
} PersistGroup;

typedef struct {
  uint8_t enabled;                   // System on/off
  uint8_t group_count;               // 0-8
  PersistGroup groups[8];            // All groups
  uint8_t auto_load_enabled;         // v4.3.0
  uint8_t auto_load_group_ids[7];    // v4.3.0
} PersistentRegisterData;
```

**Save/Restore Workflow:**
1. **save registers group "X"** → Copies current HR values to `group.reg_values[]` in RAM
2. **save** → Writes entire `PersistConfig` to NVS (includes persist groups)
3. **Boot/Reboot** → Loads `PersistConfig` from NVS into RAM
4. **Auto-load** (if enabled) → Writes `group.reg_values[]` back to `holding_regs[]`
5. **load registers group "X"** → Manual restore from `group.reg_values[]`

---

##### 8. Troubleshooting

**Problem: "Group not found" when using SAVE(1)**
- **Cause:** Group #1 doesn't exist or was deleted
- **Solution:** Run `show persist` to verify group IDs

**Problem: Auto-load doesn't restore values**
- **Cause:** Auto-load not enabled or group not added to list
- **Solution:**
  ```bash
  set persist auto-load enable
  set persist auto-load add 1
  save
  ```

**Problem: SAVE() returns 2 (rate limit)**
- **Cause:** Called more than once per 5 seconds
- **Solution:** Add timer in ST Logic or reduce save frequency

**Problem: Values not persisting across reboot**
- **Cause:** Forgot to run `save` after `save registers`
- **Solution:** Always use two-step workflow:
  1. `save registers group "X"` (snapshot to RAM)
  2. `save` (persist to NVS)

**Problem: "Max groups reached"**
- **Cause:** Already created 8 groups
- **Solution:** Delete unused group: `set persist group "old" delete`

#### Watchdog Monitor (v4.0+)
- **Auto-Restart:** ESP32 Task Watchdog Timer (30s timeout)
  - Automatically restarts system if main loop hangs
  - Persistent reboot counter in NVS
- **Diagnostics:** Last reset reason and error message
  - Power-on, Panic, Brownout, Watchdog timeout
  - Last error message (max 127 chars)
  - Uptime before last reboot
- **Production Ready:**
  - Prevents system hang in field deployments
  - Debug-friendly: Error visible after reboot

**CLI Commands:**
```bash
show watchdog                                   # Display status
```

**Example Output:**
```
=== Watchdog Monitor (v4.0+) ===
Status: ENABLED
Timeout: 30 seconds
Reboot counter: 12
Current uptime: 3600 seconds
Last reset reason: Power-on
```

#### Build System & Versioning
- **Build Tool:** PlatformIO
- **Auto-Versioning:** Build number auto-increments hver compilation
- **Build Info:** Generated i `include/build_version.h`
  - BUILD_NUMBER (auto-increment)
  - BUILD_TIMESTAMP (ISO 8601)
  - GIT_BRANCH (current branch)
  - GIT_HASH (commit SHA)
- **Version Format:** vMAJOR.MINOR.PATCH (SemVer)
- **Version Display:** `show version` command

#### Debug System
- **Debug Output:** Serial console (UART0)
- **Debug Flags:** Selective enabling af debug output
  - `config-save`: Debug NVS save operations
  - `config-load`: Debug NVS load operations
  - `wifi-connect`: Debug Wi-Fi connection
  - `network-validate`: Debug network config validation
- **Debug Commands:**
```bash
set debug <flag> on|off        # Enable/disable specific debug
show debug                     # Show debug flags status
```

---

## 📋 Hardware Requirements

### MCU Specifications
| Component | Specification |
|-----------|---------------|
| **Microcontroller** | ESP32-WROOM-32 |
| **CPU** | Dual-core Xtensa LX6, 240MHz |
| **RAM (SRAM)** | 520KB total (320KB DRAM + 200KB IRAM) |
| **RAM Usage** | ~120KB used, ~200KB available for application |
| **Flash** | 4MB (SPI Flash) |
| **Flash Usage** | ~835KB used, ~475KB available |
| **Wi-Fi** | 802.11 b/g/n (2.4GHz) |
| **Bluetooth** | BLE 4.2 (not used in this project) |

### Peripheral Hardware
| Component | Specification | Connection |
|-----------|---------------|------------|
| **RS-485 Transceiver** | MAX485, MAX3485, or equivalent | GPIO4 (RX), GPIO5 (TX), GPIO15 (DIR) |
| **Power Supply** | 3.3V regulated, 500mA minimum | USB or external 5V→3.3V regulator |
| **USB-Serial** | Built-in (CP2102 or CH340) | For programming & debug console |

### Pin Configuration (ESP32-WROOM-32)
```
=== MODBUS RTU (UART1) ===
GPIO4   (PIN_UART1_RX)      ← Modbus RX (RS-485 RO pin)
GPIO5   (PIN_UART1_TX)      → Modbus TX (RS-485 DI pin)
GPIO15  (PIN_RS485_DIR)     → RS-485 direction (DE/RE pins)

=== SYSTEM ===
GPIO2   (Heartbeat)         → Onboard LED (500ms blink)

=== DEBUG (UART0) ===
GPIO1   (TXD0)              → USB Serial TX (115200 bps)
GPIO3   (RXD0)              ← USB Serial RX (115200 bps)

=== AVAILABLE FOR USER ===
GPIO12, GPIO13, GPIO14      → Available for counters/GPIO mapping
GPIO16, GPIO17, GPIO18      → Available for counters/GPIO mapping
GPIO19, GPIO21, GPIO22      → Available for PCNT/I2C/GPIO mapping
GPIO23, GPIO25, GPIO26      → Available for GPIO mapping
GPIO27, GPIO32, GPIO33      → Available for GPIO mapping

=== RESERVED (STRAPPING PINS) ===
GPIO0   (BOOT)              ⚠️ Used during flash programming
GPIO2   (BOOT)              ⚠️ Must be LOW during boot (heartbeat compatible)
GPIO15  (RS485_DIR)         ⚠️ Must be HIGH during boot (OK for RS-485)

=== DO NOT USE ===
GPIO6-11                    ❌ Connected to SPI flash (internal use)
GPIO34-39                   ❌ Input-only pins (no output capability)
```

### RS-485 Wiring
```
ESP32         MAX485        Modbus Bus
-----         ------        ----------
GPIO4 (RX) ←  RO
GPIO5 (TX) →  DI
GPIO15     →  DE/RE
3.3V       →  VCC
GND        →  GND
              A          →  A (Data+)
              B          →  B (Data-)
```

**RS-485 Notes:**
- **Termination:** 120Ω resistor mellem A-B på bus ends
- **Biasing:** 120Ω pullup på A, 120Ω pulldown på B (optional)
- **Cable:** Twisted pair, shielded (Cat5e eller bedre)
- **Max Distance:** 1200m (4000ft) at 9600 bps
- **Max Nodes:** 32 devices (247 med repeaters)

---

## 🔧 Installation & Build

### Prerequisites
1. **PlatformIO Core** - [Installation guide](https://platformio.org/install/cli)
   ```bash
   # Via Python pip
   pip install platformio

   # Verify installation
   pio --version
   ```

2. **Python 3.7+** - For PlatformIO scripts
   ```bash
   python --version  # Should show 3.7 or higher
   ```

3. **Git** - For source control
   ```bash
   git --version
   ```

4. **USB Drivers** - For ESP32 programming
   - CP2102: [Silabs drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
   - CH340: [WCH drivers](https://www.wch-ic.com/downloads/CH341SER_ZIP.html)

### Clone Repository
```bash
git clone https://github.com/Jangreenlarsen/Modbus_server_slave_ESP32.git
cd Modbus_server_slave_ESP32
```

### Build Firmware
```bash
# Clean build (recommended first time)
pio run --target clean

# Build firmware
pio run

# Output:
# ✓ Build SUCCESS
# RAM:   [====      ]  36.4% (119416 bytes / 327680 bytes)
# Flash: [======    ]  63.8% (835621 bytes / 1310720 bytes)
```

### Upload to ESP32
```bash
# List available ports
pio device list

# Upload firmware (auto-detect port)
pio run --target upload

# Upload to specific port
pio run --target upload --upload-port COM3    # Windows
pio run --target upload --upload-port /dev/ttyUSB0  # Linux
```

### Monitor Serial Output
```bash
# Start serial monitor (115200 bps)
pio device monitor

# Monitor with filters
pio device monitor --filter colorize --filter time

# Exit monitor: Ctrl+C
```

### All-in-One Build & Upload
```bash
# Build, upload, and monitor in one command
pio run --target upload && pio device monitor
```

### Configuration Files

#### `platformio.ini` (Build Configuration)
```ini
[env:esp32]
platform = espressif32@6.12.0
board = esp32doit-devkit-v1
framework = arduino

# Serial monitor
monitor_speed = 115200
monitor_filters = colorize, time

# Build flags
build_flags =
    -DCORE_DEBUG_LEVEL=0
    -DBOARD_HAS_PSRAM=0

# Upload settings
upload_speed = 921600
upload_port = COM3    # Change to your port

# Extra scripts
extra_scripts =
    pre:scripts/generate_build_info.py
```

---

## 📖 Usage Examples

### Quick Start (First Boot)
1. **Upload firmware** via USB
   ```bash
   pio run --target upload
   ```

2. **Connect serial terminal** (115200 bps)
   ```bash
   pio device monitor
   ```

3. **Check firmware version**
   ```
   > show version
   Version: 3.2.0 Build #545
   Built: 2025-12-09 16:52:33 (main@ca6b723)
   ```

4. **View default configuration**
   ```
   > show config
   Hostname: modbus-esp32
   Unit-ID: 1 (SLAVE)
   Baud: 9600
   ```

5. **Configure Modbus slave ID**
   ```
   > set id 14
   Slave ID set to: 14 (will apply on next boot)
   NOTE: Use 'save' to persist to NVS

   > save
   [OK] Configuration saved to NVS
   ```

### Example 1: Hardware Counter med PCNT
```bash
# Tilslut pulse signal til GPIO19

# Konfigurer counter 1 i hardware mode
> set counter 1 mode 1 parameter hw-mode:hw edge:rising prescaler:100 scale:0.1 value-reg:20 raw-reg:30 freq-reg:35 ctrl-reg:40 hw-gpio:19

# Enable counter
> set counter 1 enable on

# Check counter status
> show counter 1

=== COUNTER 1 ===
Status: ENABLED
Hardware Mode: HW (PCNT)
Edge Type: rising
Prescaler: 100
Scale Factor: 0.1
PCNT GPIO: 19

Register Mappings:
  Value Register (scaled value): 20
  Raw Register (prescaled): 30
  Frequency Register (Hz): 35
  Control Register (bit 3=overflow): 40

Current Values:
  Raw Value: 12345
  Prescaled Value: 123
  Scaled Value: 12
  Frequency: 1523 Hz

# Read registers via Modbus
> read h-reg 20    # Scaled value
> read h-reg 30    # Prescaled value
> read h-reg 35    # Frequency in Hz

# Reset counter
> write h-reg 40 value 1
> read h-reg 40    # Should show 0 (auto-cleared)
```

### Example 2: Astable Timer (LED Blink)
```bash
# Konfigurer timer 1 til blink mode
> set timer 1 mode 3 parameter on-ms:1000 off-ms:500 phase1-output:1 phase2-output:0 output-coil:50 ctrl-reg:45

# Enable timer
> set timer 1 enable on

# Start blinking
> write h-reg 45 value 2    # Bit 1 = START

# Check status
> show timer 1

=== TIMER 1 ===
Status: ENABLED
Mode: ASTABLE (Mode 3 - Oscillator/Blink)
ON Duration: 1000ms
OFF Duration: 500ms
Output Coil: 50
Control Register: 45

# Stop blinking
> write h-reg 45 value 4    # Bit 2 = STOP

# Read coil status
> read coil 50
```

### Example 3: ST Logic Program
```bash
# Upload ST Logic program
> set logic 1 upload
PROGRAM ThermostatControl
VAR_INPUT
  sensor : INT;      (* Temperature sensor input *)
  setpoint : INT;    (* Desired temperature *)
END_VAR

VAR_OUTPUT
  heater : INT;      (* Heater control *)
  alarm : INT;       (* Temperature alarm *)
END_VAR

VAR
  hysteresis : INT := 5;
END_VAR

BEGIN
  (* Simple thermostat control *)
  IF sensor < (setpoint - hysteresis) THEN
    heater := 1;     (* Turn ON heater *)
  ELSIF sensor > (setpoint + hysteresis) THEN
    heater := 0;     (* Turn OFF heater *)
  END_IF;

  (* High temperature alarm *)
  IF sensor > 100 THEN
    alarm := 1;
  ELSE
    alarm := 0;
  END_IF;
END

<blank line to finish upload>

# Bind variables to registers
> set logic 1 bind sensor reg:100
> set logic 1 bind setpoint reg:101
> set logic 1 bind heater reg:102
> set logic 1 bind alarm reg:103

# Enable program
> set logic 1 enable on

# Check program status
> show logic 1

=== LOGIC PROGRAM 1 ===
Status: ENABLED
Execution Count: 1523
Error Count: 0
Last Execution: 12345ms

Variables:
  sensor (INPUT) ← Reg 100
  setpoint (INPUT) ← Reg 101
  heater (OUTPUT) → Reg 102
  alarm (OUTPUT) → Reg 103

# Write setpoint via Modbus
> write h-reg 101 value 75

# Simulate sensor reading
> write h-reg 100 value 68    # Below setpoint → heater ON
> read h-reg 102              # Should show 1 (heater ON)

> write h-reg 100 value 82    # Above setpoint → heater OFF
> read h-reg 102              # Should show 0 (heater OFF)
```

### Example 4: Wi-Fi & Telnet Setup
```bash
# Configure Wi-Fi (via serial console)
> set wifi ssid MyNetwork
Wi-Fi SSID set to: MyNetwork

> set wifi password MyPassword123
Wi-Fi password set (not shown for security)

> set wifi dhcp on
DHCP enabled (automatic IP assignment)

> set wifi telnet enable
Telnet enabled

> set wifi telnet-user admin
Telnet username set to: admin

> set wifi telnet-pass secret123
Telnet password set (hidden for security)

> save
[OK] Configuration saved to NVS

> connect wifi
Connecting to Wi-Fi: MyNetwork
Wi-Fi connection started (async)
Use 'show wifi' to check connection status

# Wait 5 seconds...

> show wifi
Wi-Fi Status: CONNECTED
SSID: MyNetwork
IP Address: 192.168.1.145
Gateway: 192.168.1.1
Netmask: 255.255.255.0
DNS: 192.168.1.1
Signal Strength: -45 dBm (Excellent)
Telnet: ENABLED (port 23)

# Now connect via telnet from another computer
# telnet 192.168.1.145 23
# Username: admin
# Password: secret123
```

### Example 5: Persistent Registers with ST Logic (v4.0+)

> **📖 For complete documentation, see:** [Persistent Registers (v4.0+)](#persistent-registers-v40) | [Auto-Load on Boot (v4.3.0)](#3-auto-load-on-boot-v430)

```bash
# Create persistence group for sensor calibration (using range notation)
> set persist group "calibration" add 200-202
✓ Added 3 registers to group 'calibration'

# Enable persistence system
> set persist enable on
Persistence system enabled

# Upload ST Logic program with SAVE/LOAD
> set logic 1 upload
PROGRAM CalibrationManager
VAR
  cal_offset: INT;      (* Calibration offset *)
  cal_scale: INT;       (* Calibration scale factor *)
  save_trigger: BOOL;   (* Manual save trigger *)
  load_trigger: BOOL;   (* Manual restore trigger *)
  save_result: INT;     (* SAVE()/LOAD() return value: 0=success, -1=error, -2=rate limited *)
  load_result: INT;     (* Separate LOAD() result for clarity *)
END_VAR

BEGIN
  (* Manual save when triggered *)
  IF save_trigger THEN
    save_result := SAVE(1);  (* Save group #1 "calibration" to NVS *)
                             (* Returns: 0=success, -1=error, -2=rate limited (5s) *)
    save_trigger := 0;       (* Clear trigger *)
  END_IF;

  (* Manual restore when triggered *)
  IF load_trigger THEN
    load_result := LOAD(1);  (* Restore group #1 from NVS *)
                             (* Returns: 0=success, -1=error (not found/NVS error) *)
    load_trigger := 0;       (* Clear trigger *)
  END_IF;
END

<blank line to finish upload>

**SAVE()/LOAD() Return Codes:**
- `0` = Success - operation completed
- `-1` = Error - gruppe ikke fundet, NVS fejl, persistence disabled
- `-2` = Rate limited (kun SAVE) - max 1 save per 5 sekunder

# Bind variables to persistence group registers
> set logic 1 bind cal_offset reg:200
> set logic 1 bind cal_scale reg:201
> set logic 1 bind save_trigger reg:202
> set logic 1 bind load_trigger reg:203
> set logic 1 bind save_result reg:204

# Enable program
> set logic 1 enabled:true

# Write calibration values
> write h-reg 200 value 42    # Set offset
> write h-reg 201 value 100   # Set scale

# Trigger save from ST Logic
> write h-reg 202 value 1     # Trigger SAVE()

# Check save result
> read h-reg 204 1
Result: 0 (success)

# View persistence status
> show persist

=== Persistent Registers (v4.0+) ===
Enabled: YES
Groups: 1

Group "calibration" (3 registers)
  Reg 200 = 42
  Reg 201 = 100
  Reg 202 = 0
  Last save: 5 sec ago

# Save to NVS (manual CLI save)
> save registers all
✓ Saved 1 groups to NVS

# Reboot to test restore
> reboot

# After reboot, check if values restored
> read h-reg 200 1
Result: 42 ✓ (restored from NVS)

> read h-reg 201 1
Result: 100 ✓ (restored from NVS)
```

### Example 6: API Hello World — ST Logic + GPIO via REST API

Komplet opsætning af et blink-program udelukkende via HTTP REST API — ingen CLI, ingen serial.
Se [docs/API_HELLO_WORLD_GUIDE.md](docs/API_HELLO_WORLD_GUIDE.md) for fuld dokumentation med fejlfinding og udvidelser.

**Forudsætninger:** WiFi forbundet, HTTP API aktiveret med Basic Auth.

```bash
# Auth setup (Base64 encode credentials)
AUTH="Authorization: Basic $(echo -n 'api_user:password' | base64)"
HOST="http://10.1.1.126"

# Trin 1: Upload ST Logic blink-program
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/source" \
  -d '{"source": "VAR\n  led : BOOL := FALSE;\n  ticks : INT := 0;\nEND_VAR\nBEGIN\n  ticks := ticks + 1;\n  IF ticks >= 50 THEN\n    led := NOT led;\n    ticks := 0;\n  END_IF;\nEND"}'
# → {"status":200,"compiled":true,"source_size":154}

# Trin 2: Bind led-variabel til Modbus Coil 10 (output)
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/bind" \
  -d '{"variable": "led", "binding": "coil:10", "direction": "output"}'
# → {"status":200,"mappings_created":1}

# Trin 3: Map GPIO 19 som output styret af Coil 10
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/gpio/19/config" \
  -d '{"direction": "output", "coil": 10}'
# → {"status":200,"pin":19,"direction":"output","coil":10}

# Trin 4: Aktivér programmet
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/logic/1/enable"
# → {"status":200,"enabled":true}

# Trin 5: Gem til NVS (overlever genstart)
curl -s -H "$AUTH" -H "Content-Type: application/json" \
  -X POST "$HOST/api/system/save"
# → {"status":200,"message":"Configuration saved to NVS"}
```

**Verifikation:**
```bash
# Se program status (execution count stiger ~100/sek)
curl -s -H "$AUTH" "$HOST/api/logic/1"
# → {"enabled":true,"compiled":true,"execution_count":15733,"error_count":0,...}

# Se GPIO 19 (value skifter mellem 0 og 1)
curl -s -H "$AUTH" "$HOST/api/gpio/19"
# → {"pin":19,"value":1,"configured":true,"direction":"output","coil":10}
```

**Dataflow:**
```
ST Logic (10ms) → led := NOT led → Coil 10 → GPIO 19 → LED blinker (500ms on/off)
```

**API endpoints brugt:**

| Trin | Endpoint | Formål |
|------|----------|--------|
| 1 | `POST /api/logic/1/source` | Upload + kompilér ST kildekode |
| 2 | `POST /api/logic/1/bind` | Bind variabel → Modbus coil |
| 3 | `POST /api/gpio/19/config` | Map fysisk pin → coil |
| 4 | `POST /api/logic/1/enable` | Start execution |
| 5 | `POST /api/system/save` | Persistér til NVS flash |

---

## 🖥️ CLI Commands Reference

Modbus ESP32 serveren har et omfattende Command Line Interface (CLI) tilgængeligt både via **Serial (UART0, 115200 baud)** og **Telnet (port 23)**.

### Quick Reference: `show config` → `set` Command Mapping

**`show config`** giver dig et komplet overblik over systemets konfiguration organiseret i logiske sektioner. Hver linje i output'et svarer direkte til en `set` kommando.

**Workflow:**
1. Kør `show config` for at se nuværende konfiguration
2. Find den indstilling du vil ændre i output'et
3. Brug tilsvarende `set` kommando til at ændre værdien
4. Kør `save` for at gemme til NVS (persistent storage)

---

### `show config` Output Organization (v4.4.0+)

Output er organiseret i **8 hovedsektioner** med klare headers:

```
[SYSTEM]           - Hostname, unit ID, build info
[MODBUS]           - Slave + Master interface settings
[COUNTERS]         - Counter mode, registers, status
[TIMERS]           - Timer mode, registers, status
[GPIO]             - GPIO pin mappings med ST variable annotation
[ST LOGIC]         - ST Logic status, programs, variable bindings
[NETWORK]          - WiFi + Telnet settings
[PERSISTENCE]      - Persistent register groups + auto-load
```

---

### Section 1: `[SYSTEM]` - System Configuration

**Show output:**
```
[SYSTEM]
  build: 873 (2025-12-30 19:52:32, main@03431e2)
  hostname: modbus-esp32
  unit-id: 1
  uart-slave: 9600 8N1
  uart-master: 9600 8N1
```

**Tilsvarende `set` kommandoer:**
```bash
# Hostname (persistent - kræver 'save')
> set hostname <name>
> save

# Unit ID (persistent - kræver 'save')
> set id <1-247>
> save

# Baud rate (persistent - kræver 'save')
> set baud <9600|19200|38400|57600|115200>
> save
```

**Bemærk:** Build info er read-only og kan ikke ændres via CLI.

---

### Section 2: `[MODBUS]` - Modbus Interface Configuration

**Show output:**
```
[MODBUS]
modbus slave:
  status: enabled
  unit-id: 1
  baud: 9600

modbus master:
  status: disabled
  baud: 9600
```

**Tilsvarende `set` kommandoer:**
```bash
# Slave enable/disable
> set modbus slave enable
> set modbus slave disable

# Master enable/disable
> set modbus master enable
> set modbus master disable

# Baud rate (begge interfaces)
> set baud <9600|19200|38400|57600|115200>
> save
```

**Info:** Modbus Slave (UART0) modtager kommandoer fra SCADA/HMI. Modbus Master (UART1) sender kommandoer til eksterne enheder.

---

### Section 3: `[COUNTERS]` - Counter Configuration

**Show output:**
```
[COUNTERS]
counters:
  counter1: mode=1 (hw), enabled, hr100-119
  counter2: mode=0 (disabled)
  counter3: mode=2 (modbus), enabled, hr140-159
  counter4: mode=0 (disabled)
```

**Tilsvarende `set` kommandoer:**
```bash
# Mode 0: Disabled
> set counter <1-4> mode 0

# Mode 1: Hardware PCNT
> set counter <1-4> mode 1 parameter \
  hw-mode:hw \
  edge:rising \
  prescaler:100 \
  scale:0.1 \
  value-reg:100 \
  raw-reg:101 \
  freq-reg:102 \
  ctrl-reg:103 \
  hw-gpio:19

# Mode 2: Modbus Slave Input
> set counter <1-4> mode 2 parameter \
  modbus-mode:slave \
  slave-id:10 \
  register:5 \
  value-reg:140 \
  ctrl-reg:143

# Enable/Disable
> set counter <1-4> enable on
> set counter <1-4> enable off

# Control flags
> set counter <1-4> control \
  counter-reg-reset-on-read:1 \
  compare-reg-reset-on-read:1 \
  compare-enabled:1 \
  compare-mode:1 \
  compare-source:1
```

**Info:** Se `show counter <id>` for detaljeret status for specifik counter.

---

### Section 4: `[TIMERS]` - Timer Configuration

**Show output:**
```
[TIMERS]
timers:
  timer1: mode=1 (on-delay), enabled, target=5000ms, hr180-189
  timer2: mode=0 (disabled)
  timer3: mode=3 (pulse), enabled, hr200-209
  timer4: mode=0 (disabled)
```

**Tilsvarende `set` kommandoer:**
```bash
# Mode 0: Disabled
> set timer <1-4> mode 0

# Mode 1: On-Delay (TON)
> set timer <1-4> mode 1 parameter \
  target:5000 \
  input-reg:10 \
  output-reg:15 \
  elapsed-reg:16

# Mode 2: Off-Delay (TOF)
> set timer <1-4> mode 2 parameter \
  target:3000 \
  input-reg:20 \
  output-reg:25 \
  elapsed-reg:26

# Mode 3: Pulse (TP)
> set timer <1-4> mode 3 parameter \
  target:1000 \
  trigger-reg:30 \
  output-reg:35 \
  elapsed-reg:36

# Enable/Disable
> set timer <1-4> enable on
> set timer <1-4> enable off
```

**Info:** Se `show timer <id>` for detaljeret status for specifik timer.

---

### Section 5: `[GPIO]` - GPIO Pin Mappings

**Show output (v4.4.0+ med ST variable annotation):**
```
[GPIO]
mappings:
  set gpio 21 coil 0 [ST VAR:logic1.count]
  set gpio 18 input 20 [Manual]
  set gpio 19 reg 100 [Counter1]
```

**Tilsvarende `set` kommandoer:**
```bash
# Map GPIO pin til coil (read/write)
> set gpio <pin> coil <address>

# Map GPIO pin til discrete input (read-only)
> set gpio <pin> input <address>

# Map GPIO pin til holding register (read/write)
> set gpio <pin> reg <address>

# Fjern GPIO mapping
> set gpio <pin> none
```

**Annotation forklaring:**
- `[ST VAR:logic1.count]` - GPIO bruges af ST Logic program 1, variabel "count"
- `[Counter1]` - GPIO bruges af Counter 1 (hardware PCNT)
- `[Timer2]` - GPIO bruges af Timer 2
- `[Manual]` - Manuelt konfigureret mapping (ingen automatisk association)

**Info:** Annotationerne hjælper dig med at identificere konflikter og forstå GPIO forbrug.

---

### Section 6: `[ST LOGIC]` - ST Logic Programming

**Show output (v4.4.0+ grupperet per program):**
```
[ST LOGIC]
  status: enabled
  interval: 50 ms
  cycles: 6163

programs:
  logic1: enabled, register bindings=3
  logic2: disabled, register bindings=0
  logic3: disabled, register bindings=0
  logic4: disabled, register bindings=0

variable bindings:
logic1:
  var start_count <-- coil:0 as input
  var threshold   <-- reg:20 as input
  var count       --> reg:21 as output
```

**Tilsvarende `set` kommandoer:**
```bash
# Upload ST source code
> set logic <1-4> upload "
VAR
  start_count : BOOL;
  threshold : INT;
  count : INT;
END_VAR

IF start_count THEN
  count := threshold + 10;
END_IF
"

# Enable/disable program
> set logic <1-4> enabled:true
> set logic <1-4> enabled:false

# Bind variable til Modbus register
> set logic <1-4> bind <var_name> reg:<address> input
> set logic <1-4> bind <var_name> reg:<address> output
> set logic <1-4> bind <var_name> coil:<address> input
> set logic <1-4> bind <var_name> coil:<address> output
> set logic <1-4> bind <var_name> input-dis:<address> input

# Slet program
> set logic <1-4> delete

# Sæt execution interval (OS scheduling)
> set logic interval:<ms>   # 10, 20, 25, 50, 75, 100

# Enable/disable debug output
> set logic debug:true
> set logic debug:false
```

**Variable Bindings Format:**
- `var <name> <-- <source>` = INPUT mode (ST læser fra Modbus)
- `var <name> --> <target>` = OUTPUT mode (ST skriver til Modbus)
- Grupperet per program (logic1:, logic2:, osv.)

**EXPORT vs BIND - Forskelle og Anvendelse:**

Der er to måder at mappe ST variabler til Modbus registre:

| Aspekt | EXPORT keyword | BIND kommando |
|--------|---------------|---------------|
| **Register-valg** | Automatisk (systemet vælger fra IR pool 220-251) | Manuel (du vælger specifikt register) |
| **Hvornår brugt** | I ST program deklaration (`VAR temp : REAL EXPORT; END_VAR`) | CLI kommando efter upload (`set logic 1 bind temp reg:50 input`) |
| **Register stabilitet** | Kan ændre sig hvis andre EXPORT variabler tilføjes | Altid fast register-nummer |
| **Fleksibilitet** | Kræver re-compile for at ændre mapping | Kan ændres via CLI uden re-compile |
| **Use case** | Hurtig prototyping, intern brug, debug variabler | SCADA integration, faste registre, dokumenteret layout |

**EXPORT - Automatisk Tildeling:**
```st
VAR
  temp : REAL EXPORT;      // Systemet tildeler automatisk fx reg:220-221
  counter : INT EXPORT;    // Systemet tildeler automatisk fx reg:222
END_VAR
```
- System vælger ledige registre fra IR pool (220-251)
- Se tildelte registre med `show config` kommandoen
- Nemt og hurtigt - ingen manuel register-styring
- Register-nummer kan ændre sig hvis konfigurationen ændres

**BIND - Manuel Tildeling:**
```bash
# ST program (variabler UDEN EXPORT)
VAR
  temp : REAL;        // Ingen EXPORT
  setpoint : INT;     // Ingen EXPORT
END_VAR

# Derefter i CLI:
> set logic 1 bind temp reg:50 input
> set logic 1 bind setpoint reg:52 output
```
- Fuld kontrol over register-mapping
- Fast register-layout - ændrer sig aldrig
- Nyttigt når eksterne systemer forventer data på specifikke registre
- Lettere at debugge (du ved præcis hvor data er)

**Vigtig regel:**
- En variabel kan **enten** være EXPORT (automatisk) **eller** bound manuelt (ikke begge dele)
- Variabler uden EXPORT og uden BIND er kun interne i ST programmet

**Anbefaling:**
- **Brug EXPORT** til hurtig udvikling og interne variabler
- **Brug BIND** til professionel integration med SCADA/PLC systemer der kræver fast register-layout

**Show kommandoer for ST Logic:**
```bash
# Vis program detaljer
> show logic <1-4>

# Vis source code med linjebrud
> show logic <1-4> code

# Vis timing info (execution times, jitter)
> show logic <1-4> timing

# Vis compileret bytecode (v4.4.0+)
> show logic <1-4> bytecode

# Vis alle programmer
> show logic all

# Vis kun programmer med fejl
> show logic errors

# Vis execution statistik
> show logic stats

# Nulstil statistik
> reset logic stats
> reset logic stats <1-4>
```

**Info:**
- ST Logic kører med fast interval (default 50ms)
- Variable bindings synkroniseres før/efter execution
- Se IEC 61131-3 standard for ST syntax reference

---

### Section 7: `[NETWORK]` - WiFi & Telnet

**Show output:**
```
[NETWORK]
wifi:
  status: enabled
  ssid: MyNetwork
  ip: 192.168.1.100
  mode: static

telnet:
  status: enabled
  port: 23
  username: admin
```

**Tilsvarende `set` kommandoer:**
```bash
# WiFi enable/disable
> set wifi enable
> set wifi disable

# WiFi credentials (persistent - kræver 'save')
> set wifi ssid <ssid>
> set wifi password <password>
> save

# WiFi static IP
> set wifi static <ip> <gateway> <subnet> <dns>
> save

# WiFi DHCP mode
> set wifi dhcp
> save

# Telnet enable/disable
> set telnet enable
> set telnet disable

# Telnet credentials (persistent - kræver 'save')
> set telnet username <username>
> set telnet password <password>
> save
```

**Info:**
- WiFi SSID/password gemmes i NVS
- Telnet kræver authentication (default: admin/admin)
- Se `show wifi` for detaljeret WiFi status

---

### Section 8: `[PERSISTENCE]` - Persistent Register Groups

**Show output:**
```
[PERSISTENCE]
  status: enabled

auto-load:
  status: enabled
  groups: 1, 2 (2 groups)

groups:
  group1: hr200-210 (10 regs), crc=0x1A2B
  group2: hr300-305 (5 regs), crc=0xF3D4
```

**Tilsvarende `set` kommandoer:**
```bash
# Enable/disable persistence system
> set persist enable
> set persist disable

# Create register group
> set persist group <1-7> add <start_reg> <count>

# Delete register group
> set persist group <1-7> delete

# Save group to NVS
> persist save <group_id>

# Restore group from NVS
> persist load <group_id>

# Auto-load system (v4.3.0+)
> set persist auto-load enable
> set persist auto-load disable
> set persist auto-load add <group_id>
> set persist auto-load remove <group_id>

# Show persistence status
> show persist
> show persist groups
```

**Info:**
- Auto-load gendanner register grupper automatisk ved boot
- Max 7 grupper, hver op til 50 registre
- CRC32 checksum validering ved load
- Se ST Logic SAVE()/LOAD() funktioner for programmatisk adgang

---

### Common CLI Workflows

**1. Konfigurer Counter fra bunden:**
```bash
# Find counter status
> show config
[COUNTERS]
  counter1: mode=0 (disabled)

# Konfigurer hardware mode
> set counter 1 mode 1 parameter hw-mode:hw edge:rising prescaler:100 scale:0.1 value-reg:100 raw-reg:101 freq-reg:102 ctrl-reg:103 hw-gpio:19

# Enable counter
> set counter 1 enable on

# Verificer konfiguration
> show counter 1

# Gem til NVS
> save
```

**2. Upload og test ST Logic program:**
```bash
# Check ST Logic status
> show config
[ST LOGIC]
  status: enabled
  interval: 50 ms

# Upload program
> set logic 1 upload "
VAR
  input_val : INT;
  output_val : INT;
END_VAR

output_val := input_val * 2;
"

# Bind variables
> set logic 1 bind input_val reg:200 input
> set logic 1 bind output_val reg:201 output

# Enable program
> set logic 1 enabled:true

# Test med Modbus write
> write h-reg 200 value int 42

# Verificer output
> read h-reg 201 1
Result: 84 ✓ (42 * 2)

# Se bytecode (debug)
> show logic 1 bytecode
```

**3. Setup persistent calibration data:**
```bash
# Check persistence status
> show config
[PERSISTENCE]
  status: disabled

# Enable system
> set persist enable

# Create group for calibration registers (HR 200-209)
> set persist group 1 add 200 10

# Write calibration values
> write h-reg 200 value int 100
> write h-reg 201 value int 250

# Save to NVS
> persist save 1

# Add to auto-load (restore on boot)
> set persist auto-load enable
> set persist auto-load add 1

# Gem konfiguration
> save

# Test: Reboot og verificer auto-load
> reboot
[After boot]
> read h-reg 200 1
Result: 100 ✓ (restored from NVS)
```

---

### Advanced: Using `show config` Output Directly

**Workflow for batch reconfiguration:**

1. **Export current config til fil:**
   ```bash
   > show config
   # Copy output til text editor
   ```

2. **Modificer settings i editor:**
   ```
   [GPIO]
   mappings:
     set gpio 21 coil 0 [ST VAR:logic1.count]  ← Copy this line
     set gpio 18 input 20 [Manual]             ← Copy this line
   ```

3. **Paste commands tilbage til CLI:**
   ```bash
   > set gpio 21 coil 0
   > set gpio 18 input 20
   > save
   ```

**Fordel:** `show config` output er **copy/paste ready** - linjer kan bruges direkte som kommandoer (ignorer annotations i [brackets]).

---

### CLI Help System

Alle kommandoer har indbygget help:

```bash
# Global help
> help
> ?

# Show kommando help
> show ?
> show help

# Set kommando help
> set ?
> set help

# Logic-specifik help
> show logic ?
> show logic help

# Counter-specifik help
> show counter ?
> set counter ?

# Timer-specifik help
> show timer ?
> set timer ?
```

**Tip:** Brug `?` eller `help` som argument til enhver kommando for at se tilgængelige subkommandoer.

---

### Keyboard Shortcuts (Telnet + Serial)

| Shortcut | Function |
|----------|----------|
| `↑` / `↓` | Command history (sidste 10 kommandoer) |
| `←` / `→` | Cursor navigation (insert mode) |
| `Backspace` | Delete character before cursor |
| `Delete` | Delete character at cursor |
| `Ctrl+C` | Cancel current line |
| `Tab` | (ikke implementeret endnu) |

**Info:** Cursor editing virker både i Serial Monitor og Telnet sessions.

---

### Error Handling & Validation

CLI validerer alle inputs og giver klare fejlmeddelelser:

```bash
# Ugyldig program ID
> set logic 5 enabled:true
ERROR: Invalid program ID (1-4)

# Manglende parameter
> set counter 1 mode 1
ERROR: Missing 'parameter' keyword

# Ugyldig register range
> set persist group 1 add 500 10
ERROR: Register range 500-509 out of bounds (0-999)

# Compilation error (ST Logic)
> set logic 1 upload "count = 10"  # Missing VAR block
ERROR: Expected VAR keyword (line 1)
```

**Tip:** Læs fejlmeddelelser nøje - de angiver præcist hvad der er galt og hvordan man retter det.

---

### Version Information

CLI kommandoer dokumenteret for **v4.4.0** (Build #873+):
- `show config` med 8 sektioner og ST variable annotations
- `show logic <id> bytecode` bytecode debugging
- Execution interval visibility i ST Logic output
- Grupperet variable bindings per program

Se [Version History](#-version-history) for changelog og tidligere versioner.

---

## 📁 Project Structure

```
Modbus_server_slave_ESP32/
│
├── README.md                      # This comprehensive documentation
├── CHANGELOG.md                   # Detailed version history
├── CLAUDE.md                      # Developer guidelines (Danish)
├── platformio.ini                 # PlatformIO build configuration
├── build_number.txt               # Auto-generated build counter
│
├── include/                       # C++ Header Files (~40 files)
│   ├── types.h                   # ★ ALL struct definitions (single source)
│   ├── constants.h               # ★ ALL #defines and enums (single source)
│   │
│   ├── config_struct.h           # Configuration persistence
│   ├── config_save.h             # Save to NVS
│   ├── config_load.h             # Load from NVS
│   ├── config_apply.h            # Apply config to system
│   │
│   ├── modbus_frame.h            # Modbus frame struct & CRC
│   ├── modbus_parser.h           # Parse raw bytes → request
│   ├── modbus_serializer.h       # Build response frames
│   ├── modbus_fc_read.h          # FC01-04 implementations
│   ├── modbus_fc_write.h         # FC05-06, FC0F-10 implementations
│   ├── modbus_fc_dispatch.h      # Route FC → handler
│   ├── modbus_server.h           # Main state machine
│   ├── modbus_rx.h               # Serial RX & frame detection
│   ├── modbus_tx.h               # RS-485 TX with DIR control
│   │
│   ├── registers.h               # Holding/input register arrays
│   ├── coils.h                   # Coil/discrete input bit arrays
│   ├── gpio_mapping.h            # GPIO ↔ coil/register bindings
│   │
│   ├── counter_config.h          # CounterConfig struct & validation
│   ├── counter_sw.h              # SW polling mode
│   ├── counter_sw_isr.h          # SW-ISR interrupt mode
│   ├── counter_hw.h              # HW PCNT mode
│   ├── counter_frequency.h       # Frequency measurement
│   ├── counter_engine.h          # Orchestration & prescaler
│   │
│   ├── timer_config.h            # TimerConfig struct & validation
│   ├── timer_engine.h            # Timer state machines
│   │
│   ├── st_types.h                # ST Logic types & bytecode
│   ├── st_compiler.h             # ST → bytecode compiler
│   ├── st_parser.h               # ST syntax parser
│   ├── st_vm.h                   # Bytecode VM executor
│   ├── st_logic_config.h         # Logic program config
│   ├── st_logic_engine.h         # Main ST execution loop
│   │
│   ├── cli_parser.h              # Command tokenizer & dispatcher
│   ├── cli_commands.h            # `set` command handlers
│   ├── cli_show.h                # `show` command handlers
│   ├── cli_shell.h               # CLI main loop & I/O
│   ├── cli_history.h             # Command history buffer
│   │
│   ├── network_manager.h         # Wi-Fi connection manager
│   ├── network_config.h          # Network config validation
│   ├── wifi_driver.h             # ESP32 Wi-Fi HAL wrapper
│   ├── telnet_server.h           # Telnet protocol handler
│   │
│   ├── gpio_driver.h             # GPIO abstraction layer
│   ├── uart_driver.h             # UART abstraction layer
│   ├── pcnt_driver.h             # PCNT (Pulse Counter) abstraction
│   ├── nvs_driver.h              # NVS (storage) abstraction
│   │
│   ├── heartbeat.h               # GPIO2 LED blink
│   ├── debug.h                   # Debug printf wrappers
│   ├── debug_flags.h             # Selective debug output
│   └── build_version.h           # Auto-generated build info
│
├── src/                           # C++ Implementation Files (~40 files)
│   ├── main.cpp                  # ★ Entry point: setup() & loop()
│   │
│   ├── config_struct.cpp         # Config defaults
│   ├── config_save.cpp           # NVS save with CRC
│   ├── config_load.cpp           # NVS load & migration
│   ├── config_apply.cpp          # Apply config to hardware
│   │
│   ├── modbus_frame.cpp          # CRC16 calculation
│   ├── modbus_parser.cpp         # Byte → request parsing
│   ├── modbus_serializer.cpp     # Response building
│   ├── modbus_fc_read.cpp        # FC01-04 handlers
│   ├── modbus_fc_write.cpp       # FC05-06, FC0F-10 handlers
│   ├── modbus_fc_dispatch.cpp    # FC routing logic
│   ├── modbus_server.cpp         # State machine (idle→RX→process→TX)
│   ├── modbus_rx.cpp             # RX with 3.5 char timeout
│   ├── modbus_tx.cpp             # TX with RS-485 DIR control
│   │
│   ├── registers.cpp             # Register array access
│   ├── coils.cpp                 # Coil bit array access
│   ├── gpio_mapping.cpp          # GPIO ↔ register mapping
│   │
│   ├── counter_config.cpp        # Counter validation
│   ├── counter_sw.cpp            # SW polling implementation
│   ├── counter_sw_isr.cpp        # SW-ISR interrupt handling
│   ├── counter_hw.cpp            # HW PCNT setup & read
│   ├── counter_frequency.cpp     # Hz calculation (~1sec)
│   ├── counter_engine.cpp        # Main counter loop
│   │
│   ├── timer_config.cpp          # Timer validation
│   ├── timer_engine.cpp          # Timer state machines
│   │
│   ├── st_compiler.cpp           # ST lexer & code generator
│   ├── st_parser.cpp             # ST syntax parsing
│   ├── st_vm.cpp                 # Bytecode execution
│   ├── st_logic_config.cpp       # Program storage
│   ├── st_logic_engine.cpp       # Main ST loop (10ms rate)
│   │
│   ├── cli_parser.cpp            # Command parsing & dispatch
│   ├── cli_commands.cpp          # `set` implementations
│   ├── cli_show.cpp              # `show` implementations
│   ├── cli_shell.cpp             # CLI I/O & line editing
│   ├── cli_history.cpp           # History buffer (arrow keys)
│   │
│   ├── network_manager.cpp       # Wi-Fi state machine
│   ├── network_config.cpp        # Config validation
│   ├── wifi_driver.cpp           # ESP32 WiFi.h wrapper
│   ├── telnet_server.cpp         # Telnet protocol (IAC, echo)
│   │
│   ├── gpio_driver.cpp           # GPIO init/read/write
│   ├── uart_driver.cpp           # UART init/TX/RX
│   ├── pcnt_driver.cpp           # PCNT unit configuration
│   ├── nvs_driver.cpp            # NVS read/write/commit
│   │
│   ├── heartbeat.cpp             # LED blink loop
│   ├── debug.cpp                 # Debug output functions
│   ├── debug_flags.cpp           # Debug flag storage
│   └── version.cpp               # Version strings
│
├── scripts/                       # Build & Test Scripts
│   ├── generate_build_info.py    # Auto-generate build_version.h
│   ├── quick_count_test.py       # Counter testing
│   ├── read_boot_debug.py        # Read serial boot output
│   ├── reconfigure_counters.py   # Counter CLI automation
│   ├── run_counter_tests.py      # Counter integration tests
│   └── ...                       # Additional test scripts
│
├── docs/                          # Documentation
│   ├── README_ST_LOGIC.md        # ST Logic programming guide
│   ├── FEATURE_GUIDE.md          # Feature-by-feature documentation
│   ├── ESP32_Module_Architecture.md  # Architecture deep-dive
│   ├── GPIO_MAPPING_GUIDE.md     # GPIO configuration guide
│   ├── COUNTER_COMPARE_REFERENCE.md  # Counter compare feature
│   ├── ST_USAGE_GUIDE.md         # ST Logic usage examples
│   └── ARCHIVED/                 # Historical reports (41 files)
│
├── .pio/                          # PlatformIO build artifacts (ignored)
├── .vscode/                       # VS Code workspace settings
├── .git/                          # Git version control
└── .gitignore                     # Git ignore patterns
```

---

## 🏗️ Architecture

### 8-Layer Modular Design

#### **Layer 0: Hardware Drivers** (GPIO, UART, PCNT, NVS)
**Purpose:** Abstract ESP32 hardware APIs
**Files:** `gpio_driver.cpp`, `uart_driver.cpp`, `pcnt_driver.cpp`, `nvs_driver.cpp`
**Principle:** Only layer that knows about ESP32 registers. Can be mocked for testing.

#### **Layer 1: Protocol Core** (Modbus Frame Handling)
**Purpose:** Modbus RTU frame parsing & serialization
**Files:** `modbus_frame.cpp`, `modbus_parser.cpp`, `modbus_serializer.cpp`
**Principle:** Pure functions, testable without hardware.

#### **Layer 2: Function Code Handlers** (FC01-06, FC0F-10)
**Purpose:** Implement Modbus function code logic
**Files:** `modbus_fc_read.cpp`, `modbus_fc_write.cpp`, `modbus_fc_dispatch.cpp`
**Principle:** Each FC isolated, adding new FC = one file change.

#### **Layer 3: Modbus Server Runtime**
**Purpose:** Main Modbus state machine
**Files:** `modbus_server.cpp`, `modbus_rx.cpp`, `modbus_tx.cpp`
**State Machine:** idle → RX → process → TX → idle
**Timing:** 3.5 character timeout for frame detection.

#### **Layer 4: Data Storage** (Registers & Coils)
**Purpose:** Holding/input register arrays, coil/discrete input bit arrays
**Files:** `registers.cpp`, `coils.cpp`, `gpio_mapping.cpp`
**Access:** All register/coil access goes through these APIs.

#### **Layer 5: Feature Engines** (Counters, Timers, ST Logic)
**Counter Engine:**
- `counter_engine.cpp`: Orchestration + prescaler division
- `counter_sw.cpp`: Software polling mode
- `counter_sw_isr.cpp`: Interrupt mode
- `counter_hw.cpp`: PCNT hardware mode
- `counter_frequency.cpp`: Hz measurement

**Timer Engine:**
- `timer_engine.cpp`: 4 timer state machines

**ST Logic Engine:**
- `st_compiler.cpp`: Source → bytecode
- `st_vm.cpp`: Bytecode executor
- `st_logic_engine.cpp`: Main loop (10ms rate)

#### **Layer 6: Persistence** (Config Save/Load/Apply)
**Purpose:** Configuration management
**Files:**
- `config_struct.cpp`: Default config
- `config_save.cpp`: Save to NVS with CRC
- `config_load.cpp`: Load from NVS, schema migration
- `config_apply.cpp`: Apply config to running system

**Principle:**
- Load = read from NVS (can fail gracefully)
- Save = write to NVS (atomic with CRC)
- Apply = activate in running system (idempotent)

#### **Layer 7: User Interface** (CLI & Telnet)
**CLI Components:**
- `cli_parser.cpp`: Tokenize & dispatch
- `cli_commands.cpp`: `set` command handlers
- `cli_show.cpp`: `show` command handlers
- `cli_shell.cpp`: Main CLI loop, line editing
- `cli_history.cpp`: Command history buffer

**Network UI:**
- `telnet_server.cpp`: Telnet protocol (IAC, WILL/DO/DONT, echo)
- `network_manager.cpp`: Wi-Fi connection state machine

#### **Layer 8: System** (Main Loop & Utilities)
**Entry Point:**
- `main.cpp`: setup() og loop() only

**Utilities:**
- `heartbeat.cpp`: GPIO2 LED blink
- `debug.cpp`: Debug output wrapper
- `version.cpp`: Version strings

### Key Design Principles

**1. Modularity**
- 40+ files, hver med single responsibility
- Typical file size: 100-250 lines
- No file depends on "everything"

**2. No Circular Dependencies**
- Dependency graph is acyclic (DAG)
- Layer N can only depend on Layer N-1 or lower
- Each module independently testable

**3. Single Source of Truth**
- **types.h:** ALL struct definitions
- **constants.h:** ALL #defines and enums
- No duplicate definitions across files

**4. CRC-Protected Persistence**
- All saved data has CRC16 checksum
- CRC calculated over entire struct (dynamic size)
- Corruption detected on load → factory defaults

**5. Schema Versioning**
- Each config has schema version number
- Mismatch → automatic migration or reset
- Backward compatible (old configs auto-upgrade)

**6. Dual-Core Friendly**
- Core 0: FreeRTOS, Wi-Fi, Bluetooth stack
- Core 1: Application (Modbus, CLI, counters, timers)
- No mutex needed (single-threaded application)

---

## 📊 Specifications

### Modbus RTU Protocol
| Parameter | Value |
|-----------|-------|
| **Function Codes** | FC01, FC02, FC03, FC04, FC05, FC06, FC0F (15), FC10 (16) |
| **Holding Registers** | 256 (addresses 0-255) |
| **Input Registers** | 256 (addresses 0-255) |
| **Coils** | 256 (bit-addressable 0-255) |
| **Discrete Inputs** | 256 (bit-addressable 0-255) |
| **Slave ID Range** | 1-247 |
| **Baudrate Range** | 300-115200 bps |
| **Frame Timeout** | 3.5 character times (baudrate-adaptive) |
| **CRC Algorithm** | CRC16-CCITT (XMODEM polynomial) |
| **Error Codes** | 01 (Illegal Function), 02 (Illegal Address), 03 (Illegal Value) |
| **Max Frame Size** | 256 bytes |
| **Response Time** | <100ms typical, <500ms worst-case |

### Counters (4 Independent) - v4.2.4
| Feature | Specification |
|---------|---------------|
| **Modes** | 3 (SW polling, SW-ISR interrupt, HW PCNT) |
| **Max Frequency** | 500Hz (SW), 10kHz (ISR), 40MHz (HW) |
| **Counter Range** | 64-bit internal (0 to 18,446,744,073,709,551,615) |
| **Prescaler Range** | 1-65535 |
| **Scale Factor** | 0.0001-10000.0 (float) |
| **Bit Width Output** | 8, 16, 32, or 64-bit (multi-word Modbus support) |
| **Edge Detection** | Rising, falling, or both |
| **Direction** | Up or down counting |
| **Frequency Measurement** | 0-20kHz, ±1Hz accuracy |
| **Compare Feature** | ✅ Edge detection (v4.2.4 - BUG-029) |
| **Compare Modes** | ≥ (mode 0), > (mode 1), === (mode 2) |
| **Compare Threshold** | Runtime modifiable via Modbus (v4.2.4 - BUG-030) |
| **Register Spacing** | 20 registers per counter (v4.2.4 - BUG-028) |
| **Smart Defaults** | HR100-114 (C1), HR120-134 (C2), HR140-154 (C3), HR160-174 (C4) |
| **Debounce** | Software (1-1000ms) |

### Timers (4 Independent)
| Feature | Specification |
|---------|---------------|
| **Modes** | 4 (One-shot, Monostable, Astable, Input-triggered) |
| **Timing Precision** | ±1ms (millis() based) |
| **Min Duration** | 1ms |
| **Max Duration** | 4,294,967,295ms (~49 days) |
| **Output** | Coil register (0-255) |
| **Control** | Holding register (start/stop/reset bits) |
| **Retriggerable** | Yes (Mode 2: Monostable) |

### ST Logic Programming (4 Programs)
| Feature | Specification |
|---------|---------------|
| **Source Code Size** | 2KB max per program |
| **Bytecode Size** | 1KB max per program |
| **Variables** | 32 max per program |
| **Variable Types** | INT (16-bit), BOOL (1-bit), REAL (float) |
| **Execution Rate** | 10ms default (configurable) |
| **Compilation** | Real-time on upload |
| **Language** | IEC 61131-3 Structured Text subset |

### Networking
| Feature | Specification |
|---------|---------------|
| **Wi-Fi Standard** | 802.11 b/g/n (2.4GHz) |
| **Security** | WPA/WPA2-PSK |
| **IP Assignment** | DHCP or static |
| **Telnet Port** | 23 (default, configurable) |
| **Concurrent Telnet** | 1 client |
| **Authentication** | Username/password (optional) |
| **Session Timeout** | Configurable |

### System Resources
| Resource | Specification |
|----------|---------------|
| **RAM Total** | 520KB (320KB DRAM + 200KB IRAM) |
| **RAM Used** | ~120KB |
| **RAM Available** | ~200KB for application |
| **Flash Total** | 4MB |
| **Flash Used** | ~835KB (firmware + partition table) |
| **Flash Available** | ~475KB |
| **NVS Size** | 20KB (persistent storage) |
| **CPU Speed** | 240MHz (dual-core) |
| **Watchdog** | Disabled (can be enabled) |

---

## 🔐 Security Considerations

### Authentication
- **Telnet:** Optional username/password authentication
- **Default:** Empty username, empty password (disabled by default)
- **Password Storage:** Plain text in NVS (⚠️ not encrypted)
- **Recommendation:** Enable authentication for production deployments

### Network Security
- **Encryption:** ❌ None (telnet protocol is plain text)
- **Firewall:** ⚠️ ESP32 has no built-in firewall
- **Recommendation:**
  - Use on trusted networks only
  - Consider VPN tunnel for remote access
  - Do NOT expose telnet to Internet directly

### Data Integrity
- **CRC16 Validation:** ✅ All Modbus frames
- **CRC16 Validation:** ✅ All NVS stored data
- **Schema Versioning:** ✅ Automatic migration with safety checks
- **Corruption Detection:** ✅ Reverts to factory defaults on corruption

### Access Control
- **Physical:** USB serial console has full access (no authentication)
- **Network:** Telnet has optional authentication
- **Modbus:** No authentication (standard Modbus limitation)

### Recommendations
⚠️ **Not suitable for:**
- Internet-exposed devices (no encryption)
- Critical infrastructure (no redundancy/watchdog)
- Financial/medical applications (no audit logging)

✅ **Suitable for:**
- Industrial automation (local network)
- Building control systems (isolated VLAN)
- Test/development environments
- Educational/hobby projects

**Security Hardening Checklist:**
- [ ] Enable telnet authentication
- [ ] Change default passwords
- [ ] Use dedicated VLAN for Modbus devices
- [ ] Implement network firewall rules
- [ ] Enable watchdog timer
- [ ] Disable unused features (telnet if not needed)
- [ ] Regular firmware updates

---

## 🧪 Testing

### Unit Testing (Local)
```bash
# Run PlatformIO unit tests (if available)
pio test
```

**Note:** Unit tests are currently minimal. Expansion recommended:
- Modbus frame parsing tests
- CRC calculation tests
- Counter logic tests
- Timer state machine tests

### Integration Testing (Hardware Required)
1. **Upload firmware**
   ```bash
   pio run --target upload
   ```

2. **Connect serial monitor**
   ```bash
   pio device monitor
   ```

3. **Run manual CLI tests**
   ```bash
   > show config
   > set counter 1 mode 1 parameter hw-mode:sw
   > show counter 1
   ```

4. **Verify via Modbus master**
   - Use Modbus Poll (Windows) or pymodbus (Python)
   - Read holding register 0: `READ 03 00 00 00 01`
   - Write register 0: `WRITE 06 00 00 00 64`

### Python Test Scripts
Located in `scripts/` folder:

```bash
# Counter accuracy test
python scripts/quick_count_test.py

# Timer timing verification
python scripts/run_counter_tests.py

# Serial boot debug
python scripts/read_boot_debug.py

# Counter reconfiguration automation
python scripts/reconfigure_counters.py
```

**Example Test Workflow:**
```bash
# 1. Build & upload firmware
pio run --target upload

# 2. Run Python test script (separate terminal)
python scripts/quick_count_test.py

# 3. Monitor serial output
pio device monitor --filter colorize
```

### Modbus Master Testing (pymodbus)
```python
from pymodbus.client import ModbusSerialClient

# Connect to ESP32 via USB-RS485 adapter
client = ModbusSerialClient(
    port='COM3',         # Change to your port
    baudrate=9600,
    parity='N',
    stopbits=1,
    bytesize=8,
    timeout=1
)

if client.connect():
    # Read holding registers 0-9
    result = client.read_holding_registers(address=0, count=10, slave=1)
    print(f"Registers: {result.registers}")

    # Write holding register 5
    client.write_register(address=5, value=1234, slave=1)

    # Read coils 0-15
    result = client.read_coils(address=0, count=16, slave=1)
    print(f"Coils: {result.bits}")

    client.close()
```

---

## ⭐ What's New in v4.7.0 - Advanced ST Functions

**Release Date:** 2026-01-01 | **Build:** #920 | **Type:** Major Feature Release

Version 4.7 introduces **13 nye ST funktioner** med fokus på avanceret matematik og stateful function blocks, samtidig med kritiske DRAM optimeringer og robuste input validations.

### 🎯 New ST Functions (13 total)

#### Eksponentielle Funktioner (4 functions)
```structured-text
result := EXP(x)      (* Exponential function (e^x) *)
result := LN(x)       (* Natural logarithm (base e) *)
result := LOG(x)      (* Base-10 logarithm *)
result := POW(x, y)   (* Power function (x^y) *)
```

**Features:**
- ✅ Full input validation (NaN/INF protection)
- ✅ Overflow/underflow protection
- ✅ Returns 0.0 for invalid inputs (consistent with existing functions)
- ✅ IEC 61131-3 compliant

**Example:**
```structured-text
temp_celsius := (temp_fahrenheit - 32.0) * 5.0 / 9.0;
growth_rate := EXP(0.05 * time);
ph_value := -1.0 * LOG(hydrogen_concentration);
power_watts := POW(voltage, 2.0) / resistance;
```

#### Edge Detection (2 functions)
```structured-text
button_pressed := R_TRIG(button_input);  (* Rising edge 0→1 *)
button_released := F_TRIG(button_input); (* Falling edge 1→0 *)
```

**Features:**
- ✅ Single-cycle edge detection
- ✅ Stateful storage (automatic instance allocation)
- ✅ Max 8 instances per program

**Example:**
```structured-text
IF R_TRIG(start_button) THEN
  counter := counter + 1;
END_IF;
```

#### Timers (3 functions)
```structured-text
motor_on := TON(start_button, 2000);     (* On-delay: 2 second delay *)
light_on := TOF(presence_sensor, 5000);  (* Off-delay: 5 second delay *)
valve_pulse := TP(trigger, 500);         (* Pulse: 500ms pulse *)
```

**Features:**
- ✅ Millisecond precision via `millis()`
- ✅ Negative PT protection (treated as 0)
- ✅ Correct millis() overflow handling (49-day rollover safe)
- ✅ Non-retriggerable pulse (TP)
- ✅ Max 8 timer instances per program

#### Counters (3 functions)
```structured-text
batch_done := CTU(sensor, reset_button, 100);    (* Count up to 100 *)
empty := CTD(dispense, reload, 50);              (* Count down from 50 *)
(* CTUD available but requires VM update for full support *)
```

**Features:**
- ✅ Overflow protection (CV clamped at INT32_MAX)
- ✅ Underflow protection (CV clamped at 0)
- ✅ Edge-triggered counting (not level)
- ✅ Max 8 counter instances per program
- ⚠️ CTUD requires 5-argument support (planned for v4.8)

### 🔧 Technical Improvements

**Memory Optimization:**
- **Challenge:** Initial implementation caused +17.8 KB DRAM overflow
- **Solution:** Union-based bytecode instruction optimization
- **Result:** Only +32 bytes DRAM (99.8% reduction!)
- **Bytecode size:** Reduced from 12 bytes → 8 bytes per instruction

**Input Validation:**
- EXP/LN/LOG/POW: NaN/INF checks → returns 0.0 for invalid inputs
- Timers: Negative PT values → treated as 0
- Counters: Overflow protection at INT32_MAX

**Compiler Integration:**
- Automatic instance ID allocation during compilation
- Compiler error if max instances exceeded (easy to detect)
- Zero runtime overhead for instance management

### 📊 Performance Metrics

**Memory Usage:**
- **RAM (DRAM):** +32 bytes (+0.03% from baseline)
- **Flash:** +5,892 bytes (+0.6%)
- **Per-Program Stateful Storage:** 420 bytes (8 timers + 8 edges + 8 counters)

**Code Quality:**
- ~1,700 lines of new code across 9 files
- 100% code review passed
- All validation warnings fixed
- Build verification: SUCCESS

### 📚 Documentation

**Comprehensive Documentation:**
- **[ST_FUNCTIONS_V47.md](ST_FUNCTIONS_V47.md)** - Complete function reference (700+ lines)
  - Syntax & parameters for all 13 functions
  - 4 comprehensive code examples
  - Timing diagrams for timers
  - Truth tables for edge detection
  - Troubleshooting guide (5 common problems)
  - Memory usage breakdown

- **[RELEASE_NOTES_V47.md](RELEASE_NOTES_V47.md)** - Release notes & migration guide (400+ lines)
  - Performance metrics & benchmarks
  - Breaking changes (instance limits 16→8)
  - Known issues (CTUD VM support pending)
  - Migration guide from v4.6 to v4.7
  - Upgrade checklist
  - v4.8 roadmap

### ⚠️ Breaking Changes

**Instance Limits Reduced:**
- Old: Max 16 instances per type
- New: Max 8 instances per type (50% DRAM savings)
- Impact: LOW (most programs use <4 instances)
- Compiler error if limit exceeded

**Bytecode Structure Changed:**
- Backward compatible (programs recompile automatically)
- No user action required

### 🐛 Known Issues

**CTUD VM Support Incomplete:**
- Issue: CTUD function recognized but not executable
- Reason: VM does not support 5-argument calls yet
- Status: ⚠️ Planned for v4.8
- Workaround: Use separate CTU + CTD

### 🚀 Upgrade Guide

1. **Update firmware** to Build #920+
2. **Read documentation:** [ST_FUNCTIONS_V47.md](ST_FUNCTIONS_V47.md)
3. **Test new functions** in standalone program
4. **Verify DRAM usage** acceptable (check diagnostics)
5. **Update production programs** as needed

---

## 📝 Version History

- **v5.3.0** (2026-01-19) - 🔧 ST Logic Debugger (FEAT-008)
  - **NEW FEATURE: Interactive Debugger**
    - Pause/continue program execution at any time
    - Single-step through bytecode instructions
    - Breakpoints at PC addresses (up to 8 per program)
    - Variable inspection when program is paused
    - Instruction disassembly at current PC
    - Halt/error snapshot capture for post-mortem debugging
  - **CLI Commands:**
    - `set logic <id> debug pause` - Pause at next instruction
    - `set logic <id> debug continue` - Continue until breakpoint/halt
    - `set logic <id> debug step` - Execute one instruction
    - `set logic <id> debug stop` - Stop debugging, resume normal execution
    - `set logic <id> debug break <pc>` - Add breakpoint at PC address
    - `set logic <id> debug clear [<pc>]` - Clear breakpoint(s)
    - `show logic <id> debug` - Show debug state
    - `show logic <id> debug vars` - Show all variable values
    - `show logic <id> debug stack` - Show stack depth
  - **New Files:**
    - `include/st_debug.h` - Debug state structures and enums
    - `src/st_debug.cpp` - Debug API and display functions
  - **Bug Fixes:**
    - **BUG-190:** total_steps_debugged counter now only counts in debug mode
    - **BUG-191:** Snapshot saved on halt/error during debugging
  - **Documentation:**
    - New: [ST_DEBUG_GUIDE.md](ST_DEBUG_GUIDE.md) - Complete debugger usage guide
    - Updated: README.md, BUGS_INDEX.md, ST_COMPLETE_TEST_PLAN.md
  - **Build:** #1082-1083

- **v5.2.0** (2026-01-18) - ⏱️ TIME Literal Support (FEAT-006)
  - **NEW FEATURE: IEC 61131-3 TIME Literals**
    - Native TIME syntax: `T#5s`, `T#100ms`, `T#1h30m`
    - Compound TIME: `T#1h30m5s100ms` for complex durations
    - Stored internally as DINT milliseconds
    - Use with timers: `TON(enable, T#5s)` instead of `TON(enable, 5000)`
  - **Supported Units:**
    - `d` - days (× 86400000 ms)
    - `h` - hours (× 3600000 ms)
    - `m` - minutes (× 60000 ms)
    - `s` - seconds (× 1000 ms)
    - `ms` - milliseconds
  - **Implementation:**
    - ST Lexer: TIME literal parsing (st_lexer.cpp)
    - ST Parser: TIME token to DINT AST conversion (st_parser.cpp)
  - **Build:** #1075-1081

- **v5.1.0** (2026-01-11) - 🎯 EXPORT Keyword & Dynamic IR Pool Allocation (BUG-143 RESOLVED)
  - **NEW FEATURE: EXPORT Keyword (IEC 61131-3 compliant)**
    - Mark ST Logic variables for Modbus Input Register (IR) visibility
    - Syntax: `VAR temp : INT EXPORT; END_VAR` exposes variable to IR 220-251
    - Variables without EXPORT are program-internal only (no Modbus access)
    - Compatible with all ST types: INT, BOOL, REAL, DINT, DWORD
  - **NEW FEATURE: Dynamic IR Pool Allocation**
    - IR 220-251 (32 registers) now shared flexibly across 4 ST Logic programs
    - OLD LIMITATION: Fixed 8 registers per program (IR 220-227, 228-235, 236-243, 244-251)
    - NEW SYSTEM: Dynamic allocation based on EXPORT count per program
    - Example allocation: Logic1=5 regs, Logic2=12 regs, Logic3=3 regs, Logic4=10 regs = 30/32 used
    - Automatic pool compaction on program delete/recompile
    - Type-aware allocation: REAL/DINT/DWORD use 2 registers, INT/BOOL use 1 register
  - **IR Pool Manager (new module: ir_pool_manager.h/cpp)**
    - `ir_pool_calculate_size()` - calculates required IR space based on EXPORT flags
    - `ir_pool_allocate()` - allocates contiguous IR space for program
    - `ir_pool_free()` - releases IR space on program delete
    - `ir_pool_get_total_used()` / `ir_pool_get_free_space()` - pool statistics
    - `ir_pool_compact()` - optional defragmentation to reclaim gaps
    - `ir_pool_reallocate_all()` - restore IR allocations after NVS load on boot
  - **Compiler Integration**
    - ST Lexer: Added ST_TOK_EXPORT token (st_lexer.cpp)
    - ST Parser: Parse EXPORT modifier after variable type (st_parser.cpp)
    - ST Compiler: Store export flags in bytecode (st_compiler.cpp)
    - Bytecode: Added var_export_flags[32] and exported_var_count fields
  - **Register Mapping Updates (registers.cpp)**
    - Replaced fixed `(prog_id * 8)` allocation with dynamic pool lookup
    - Only EXPORT variables are written to IR 220-251 (efficient register usage)
    - Type-aware multi-register handling (REAL/DINT/DWORD span 2 registers)
    - LSW-first byte order maintained (per BUG-124/BUG-125 specification)
  - **Persistence (NVS)**
    - IR pool allocations restored on boot via `ir_pool_reallocate_all()`
    - Called in main.cpp:setup() after `st_logic_load_from_persist_config()`
    - Ensures consistent IR mapping across reboots
  - **Documentation Updates**
    - README.md: Added EXPORT to ST Language Features
    - MODBUS_REGISTER_MAP.md: Updated IR 220-251 section with dynamic allocation details
    - Added practical examples of EXPORT usage in ST programs
  - **Backward Compatibility**
    - Programs compiled before v5.1.0 continue to work (no EXPORT = no IR mapping)
    - Existing bindings (INPUT/OUTPUT/BOTH) unchanged and functional
  - **Bug Fixes**
    - **BUG-143 RESOLVED:** IR 220-251 limitation removed - now dynamic pool allocation
  - **Build:** #1032

- **v4.7.2** (2026-01-04) - 📋 CLI Consistency & Documentation - Holding-Reg vs Input-Reg Naming
  - **CLI Naming Standardization:**
    - All commands now consistently use `h-reg` vs `i-reg` shorthand (Build #1051)
    - Read Holding Registers: `read h-reg <addr> [count] [type]` (alias: `read reg`, `read holding-reg`)
    - Read Input Registers: `read i-reg <addr> [count] [type]` (alias: `read input-reg`) ⚠️ **Not "read reg"!**
    - Write Holding Registers: `write h-reg <addr> value <type> <val>` (alias: `write reg`)
    - Set Holding Registers: `set h-reg STATIC/DYNAMIC ...` (alias: `set reg`)
  - **Bug Fixes:**
    - **BUG-144:** Fixed confusing CLI - ST variable VALUES are in IR, not HR
    - **BUG-145:** Fixed missing "read input-reg" option in help message
    - **BUG-143:** Documented IR 220-251 limitation (8 variables/program, not 32)
  - **Documentation Updates:**
    - MODBUS_REGISTER_MAP.md: Added CLI command guide at top
    - **NEW:** Complete ST Logic Variable Bindings guide (INPUT/OUTPUT/BOTH modes)
    - **NEW:** Modbus Function Codes reference (FC03/FC04/FC06/FC16)
    - **NEW:** Practical examples with Python Modbus code and CLI commands
    - **NEW:** Data flow diagrams for binding modes
    - Clarified: ST Logic variable VALUES are in IR 220-251 (read-only)
    - Clarified: IR 220-251 automatisk mapping vs manual bindings
    - Updated all help messages to use consistent "holding-reg" / "input-reg" terminology
  - **Build:** #973-975

- **v4.7.1** (2026-01-04) - 🔧 Persistent Register Type Support & Documentation Updates
  - **NEW FEATURE: `set reg STATIC` Multi-Type Support (FEAT-001)**
    - Added support for DINT, DWORD, and REAL persistent static registers
    - Extends CLI command: `set reg STATIC <addr> Value [type] <value>`
    - Supported types: `uint`, `int`, `dint`, `dword`, `real`
    - Multi-register types (DINT/DWORD/REAL) use 2 consecutive registers
    - Full backward compatibility: Legacy syntax (no type) defaults to `uint`
  - **Schema Migration:**
    - Config schema version bumped: v8 → v9
    - Automatic migration from v8 to v9 on config load
    - StaticRegisterMapping struct expanded with type field and union
  - **Register Range Validation:**
    - Added ST Logic reserved range protection (HR 200-237)
    - Prevents STATIC register allocation in system ranges
    - Multi-register boundary checking (prevents crossing into reserved zones)
  - **Documentation Corrections:**
    - **CRITICAL:** Fixed MODBUS_REGISTER_MAP.md incorrect ranges
    - Removed non-existent HR/IR 294-499 range (HOLDING_REGS_SIZE = 256)
    - Documented HR 238-255 as available (18 safe registers)
    - Detailed ST Logic register breakdown (HR 200-237, IR 200-293)
  - **Bug Tracking:**
    - Documented BUG-142: STATIC range validation too broad (HR200-299 vs HR200-237)
  - **Build:** #966-969

- **v4.7.0** (2026-01-01) - ⭐ Advanced ST Functions & Memory Optimization
  - **NEW FEATURE: 13 ST Functions**
    - **Exponential Functions (4):** `EXP()`, `LN()`, `LOG()`, `POW()`
    - **Edge Detection (2):** `R_TRIG()`, `F_TRIG()`
    - **Timers (3):** `TON()`, `TOF()`, `TP()` with millisecond precision
    - **Counters (3):** `CTU()`, `CTD()`, `CTUD()` with edge-triggered counting
  - **Memory Optimization:**
    - Union-based bytecode instruction optimization
    - DRAM impact: Only +32 bytes (99.8% reduction from initial +17.8 KB overflow)
    - Bytecode size: 12 bytes → 8 bytes per instruction
    - Instance limits: 16 → 8 per type (50% DRAM savings)
  - **Input Validation:**
    - EXP/LN/LOG/POW: NaN/INF protection → returns 0.0 for invalid inputs
    - Timers: Negative PT values treated as 0
    - Counters: Overflow protection at INT32_MAX
  - **Compiler Integration:**
    - Automatic instance ID allocation during compilation
    - Compiler error if max instances exceeded
    - Zero runtime overhead for instance management
  - **Stateful Storage Infrastructure:**
    - Created `st_stateful.h/cpp` (395 lines) - Storage architecture
    - Created `st_builtin_edge.h/cpp` (122 lines) - Edge detection
    - Created `st_builtin_timers.h/cpp` (281 lines) - Timer functions
    - Created `st_builtin_counters.h/cpp` (272 lines) - Counter functions
    - Per-program storage: 420 bytes (8 timers + 8 edges + 8 counters)
  - **Documentation:**
    - **[ST_FUNCTIONS_V47.md](ST_FUNCTIONS_V47.md)** - Complete function reference (700+ lines)
    - **[RELEASE_NOTES_V47.md](RELEASE_NOTES_V47.md)** - Release notes & migration guide (400+ lines)
    - **[CODE_REVIEW_ST_FUNCTIONS.md](CODE_REVIEW_ST_FUNCTIONS.md)** - Detailed code review (700+ lines)
    - **[SYSTEM_FEATURE_ANALYSIS.md](SYSTEM_FEATURE_ANALYSIS.md)** - IEC 61131-3 compliance analysis (1000+ lines)
  - **Performance:**
    - Flash: +5,892 bytes (+0.6%)
    - ~1,700 lines of new code across 9 files
    - 100% code review passed
    - Build verification: SUCCESS
  - **Known Issues:**
    - ⚠️ CTUD VM support incomplete (requires 5-argument support, planned for v4.8)
    - Workaround: Use separate CTU + CTD
  - **Breaking Changes:**
    - Instance limits reduced to 8 per type (compiler error if exceeded)
    - Bytecode structure changed (backward compatible, auto-recompile)
  - **Build:** #920

- **v4.4.0** (2025-12-30) - ⭐ CLI Documentation & ST Logic Debugging Enhancements
  - **NEW FEATURE: Comprehensive CLI Documentation**
    - Added complete CLI Commands Reference section to README
    - `show config` → `set` command mapping guide
    - 8 organized sections with examples and workflows
    - 650+ lines of detailed CLI documentation
  - **ST Logic Improvements:**
    - **NEW:** `show logic <id> bytecode` command for debugging compiled programs
    - Display execution interval in `show logic <id>` output
    - Grouped variable bindings per program (logic1:, logic2:, etc.)
  - **`show config` Organization (Build #869):**
    - Reorganized output into 8 logical sections with headers
    - Section headers: [SYSTEM], [MODBUS], [COUNTERS], [TIMERS], [GPIO], [ST LOGIC], [NETWORK], [PERSISTENCE]
    - **NEW:** ST variable annotations in GPIO mappings (e.g., `[ST VAR:logic1.count]`)
    - Cross-referenced ST bindings with GPIO pins for conflict detection
    - Moved ST variable bindings from GPIO section to ST LOGIC section
    - Combined related settings (WiFi+Telnet, Slave+Master)
  - **Bug fixes:**
    - **BUG-126 FIXED:** `st_count` redeclaration in cli_show.cpp
    - **BUG-127 FIXED:** `st_state` declaration order (used before declared)
    - **BUG-128 FIXED:** normalize_alias() missing BYTECODE/TIMING keywords (Build #875)
  - **Documentation:**
    - 650+ lines CLI reference with 8 sections
    - 3 complete workflow examples
    - Help system guide and keyboard shortcuts
    - Copy/paste ready command examples
  - **Builds:** #865 (ST timing), #869 (show config organization), #873 (bytecode display)

- **v4.3.0** (2025-12-20) - ⭐ Auto-Load Persistent Register Groups on Boot
  - **NEW FEATURE:** Auto-load on boot (restore saved register groups automatically at startup)
  - **CLI Commands:**
    - `set persist auto-load enable/disable` - Enable/disable auto-load system
    - `set persist auto-load add <group_id>` - Add group to auto-load list (max 7 groups)
    - `set persist auto-load remove <group_id>` - Remove group from auto-load list
  - **Backend:** 5 new functions in registers_persist.cpp
  - **Boot integration:** Auto-restores configured groups after config load
  - **Bug fixes:**
    - **BUG-042 FIXED:** normalize_alias() missing "auto-load" keyword
    - **BUG-043 FIXED:** "set persist enable on" case sensitivity bug
    - **BUG-044 FIXED:** cli_cmd_set_persist_auto_load() case sensitive strcmp
  - **Documentation:** 376 lines comprehensive documentation (9 sections)
  - **Use case:** Ensures calibration data, recipes, and production counters are immediately available after reboot without manual intervention

- **v4.2.9** (2025-12-18) - 🔧 Counter Control Parameter Restructuring
  - **BUG-041 FIXED:** Separated reset-on-read into two distinct parameters
  - **Breaking change:** `reset-on-read` removed from `set counter` mode command
  - **New parameters in `set counter control`:**
    - `counter-reg-reset-on-read` - Reset counter when value registers read (ctrl-reg bit 0)
    - `compare-reg-reset-on-read` - Clear compare bit 4 when ctrl-reg read (cfg.reset_on_read)
  - **Improvements:**
    - Clearer separation: mode command = hardware config, control command = runtime flags
    - Counter reset now controlled by ctrl-reg bit 0 (proper runtime control)
    - Compare reset still uses cfg field (proper config persistence)

- **v4.2.8** (2025-12-18) - ⚙️ Counter Compare Source Selection
  - **BUG-040 FIXED:** Compare now supports source selection (raw/prescaled/scaled)
  - **New feature:** `compare-source` parameter (0=raw, 1=prescaled [default], 2=scaled)
  - **Improvements:**
    - Compare can now use raw hardware counter, prescaled value, or scaled value
    - Default is prescaled (most intuitive - matches raw register)
    - Supports all bit-widths (8/16/32/64) correctly
    - CLI shows compare source in `show counters` output

- **v4.2.7** (2025-12-18) - 🐛 CLI Parameter Fix
  - **BUG-039 FIXED:** CLI now accepts both "compare:1" and "compare-enabled:1" syntax
  - **Improvement:** Better parameter name compatibility for counter compare configuration

- **v4.2.6** (2025-12-18) - 🔧 Race Condition & ISR Fixes
  - **BUG-034 FIXED:** ISR state access now uses volatile pointers (prevents missed pulses at high frequency)
  - **BUG-035 FIXED:** Overflow flag auto-cleared on counter reset (eliminates sticky flags)
  - **BUG-038 FIXED:** ST Logic variable access protected with spinlock (prevents race condition between execute and I/O)
  - **Improvements:**
    - All ISR state access functions use volatile pointer pattern
    - Counter reset now clears internal overflow flags and compare state
    - ST variable synchronization via portMUX_TYPE spinlock

- **v4.2.5** (2025-12-18) - 🔒 Security & Stability Fixes
  - **BUG-031 FIXED:** Counter write lock now used by Modbus FC03 (prevents 64-bit read corruption)
  - **BUG-032 FIXED:** ST parser buffer overflow fixed (strcpy → strncpy with bounds)
  - **BUG-033 FIXED:** Variable declaration bounds check moved before array access
  - **NEW BUGS DOCUMENTED:** BUG-034 to BUG-038 (ISR volatile, overflow clear, etc.)
  - **Documentation updates:**
    - CLAUDE_ARCH.md: Added 15+ missing modules (Network, Console, Watchdog, ST Builtins)
    - All CLAUDE_*.md files updated to v4.2.5
    - BUGS_INDEX.md: 8 new bug entries added
  - **Code quality:** Dynamic version in CLI help banner (no more hardcoded version)

- **v4.2.4** (2025-12-17) - ⭐ Counter Compare Edge Detection & Modbus Control
  - **BUG-030 FIXED:** Compare value accessible via Modbus register (runtime modifiable)
  - **BUG-029 FIXED:** Compare modes use edge detection (rising edge trigger only)
  - **BUG-028 FIXED:** Register spacing increased to 20 per counter (64-bit support)
  - **BUG-027 FIXED:** Counter display overflow clamping (bit-width respected)
  - **Compare feature improvements:**
    - All modes (≥, >, ===) now use edge detection
    - Compare threshold writable via Modbus FC06/FC16
    - Reset-on-read works correctly (bit4 clears and stays cleared)
  - **Register layout updates:**
    - Counter 1: HR100-114, Counter 2: HR120-134, Counter 3: HR140-154, Counter 4: HR160-174
    - Each counter: index (1-4 words), raw (1-4 words), freq, overload, ctrl, compare_value (1-4 words)
  - **Documentation:** Updated CLI help, BUGS.md (BUG-027 to BUG-030), BUGS_INDEX.md, CLAUDE_ARCH.md

- **v4.1.0** (2025-12-12) - ⭐ ST Logic Performance Monitoring & Modbus Integration
  - **Performance monitoring system:** Min/Max/Avg execution time tracking (µs precision)
  - **CLI commands:** `show logic stats`, `show logic X timing`, `set logic stats reset`
  - **Modbus statistics:** IR 252-293 (42 new registers for statistics)
  - **Dynamic interval control:** `set logic interval:X` (10/20/25/50/75/100 ms)
  - **Modbus interval control:** HR 236-237 (32-bit read-write)
  - **Fixed 7 bugs:** BUG-001 to BUG-007 (all CRITICAL-MEDIUM severity)
  - **Documentation:** MODBUS_REGISTER_MAP.md, ST_MONITORING.md, TIMING_ANALYSIS.md, BUGS.md
  - Fixed rate scheduler with deterministic 10ms timing
  - Performance ratings with automatic tuning recommendations

- **v4.0.2** (2025-12-11) - Telnet Auth Fix
  - Fixed password validation failure due to whitespace
  - Added `trim_string()` helper for input sanitization

- **v4.0.1** (2025-12-11) - CLI Persist Enable Fix
  - Fixed `set persist enable` command error

- **v4.0.0** (2025-12-10) - Persistent Registers & Watchdog Monitor
  - Persistent register groups (save/restore to NVS)
  - ST Logic SAVE()/LOAD() functions
  - Watchdog monitor with auto-restart
  - Reboot counter and diagnostics

- **v3.2.0** (2025-12-09) - CLI Commands Complete + Persistent Settings
  - `show counter <id>` and `show timer <id>` detailed views
  - `set hostname` now persistent (saved to NVS)
  - `set echo` now persistent (saved to NVS)
  - Schema v7 with automatic migration

- **v3.1.1** (2025-12-08) - Telnet Insert Mode & ST Upload Copy/Paste
  - Telnet cursor position editing (insert mode)
  - Fixed multi-line ST Logic copy/paste into telnet

- **v3.1.0** (2025-12-05) - Wi-Fi Display & Telnet Auth Improvements
  - Enhanced `show config` with Wi-Fi section
  - `show wifi` displays actual IP (DHCP/static)
  - Wi-Fi connection validation with error messages

- **v3.0.0** (2025-12-02) - Telnet Server & Console Layer
  - Telnet CLI on port 23
  - Console abstraction (Serial/Telnet unified)
  - Remote authentication (username/password)
  - Arrow key command history

See [CHANGELOG.md](CHANGELOG.md) for complete version history with all details.

---

## 🤝 Contributing

### Code Standards
- **Language:** C++ (Arduino framework)
- **Style:** K&R with 2-space indents
- **File Size:** Keep files under 300 lines
- **Naming:** `snake_case` for functions, `UPPER_CASE` for constants
- **Comments:** Doxygen-style for public APIs

### Semantic Versioning
Follow SemVer (MAJOR.MINOR.PATCH):
- **MAJOR:** Breaking changes (incompatible API, config format)
- **MINOR:** New features (backward compatible)
- **PATCH:** Bug fixes (backward compatible)

### Adding Features
1. **Plan:** Consider which layer the feature belongs to
2. **Implement:** Create new .cpp/.h files in appropriate layer
3. **Test:** Verify on hardware
4. **Document:** Update CHANGELOG.md and this README
5. **Version:** Bump version in `include/constants.h`
6. **Commit:** Use descriptive commit message with `git tag`

### Pull Request Process
1. Fork the repository
2. Create feature branch (`git checkout -b feature/my-feature`)
3. Make changes (follow code standards)
4. Test on hardware
5. Commit with descriptive message
6. Push to your fork
7. Open Pull Request with description

---

## 📞 Support & Issues

### Getting Help
- **GitHub Issues:** [Report bugs & request features](https://github.com/Jangreenlarsen/Modbus_server_slave_ESP32/issues)
- **Documentation:** See `docs/` folder for detailed guides
- **Serial Debug:** Use `show debug` and `set debug <flag> on` for diagnostics

### Reporting Bugs
Please include:
- Firmware version (`show version`)
- Build number
- Hardware configuration (board, RS-485 module)
- Steps to reproduce
- Serial console output
- Expected vs actual behavior

### Feature Requests
Please describe:
- Use case & motivation
- Proposed implementation (if applicable)
- Impact on existing features

---

## 📄 License

**Copyright © 2025 Jan Green Larsen**

[Specify your license here - options: MIT, GPL-3.0, Apache-2.0, proprietary, etc.]

---

## 🙏 Acknowledgments

- **Original Architecture:** Mega2560 v3.6.5 (reference implementation)
- **ESP32 Platform:** Espressif Systems
- **Modbus Protocol:** Modicon/Schneider Electric
- **IEC 61131-3 ST:** International Electrotechnical Commission
- **PlatformIO:** Build system & toolchain
- **Arduino Framework:** ESP32 Arduino Core
- **Community:** Stack Overflow, ESP32 forums

---

**Maintained by:** Jan Green Larsen
**Last Updated:** 2025-12-24
**Repository:** https://github.com/Jangreenlarsen/Modbus_server_slave_ESP32

---

## 📚 Additional Documentation

### 🎯 Quick Start Guides
- **[docs/API_HELLO_WORLD_GUIDE.md](docs/API_HELLO_WORLD_GUIDE.md)** - ⭐ **API Hello World** - ST Logic + GPIO via REST API (komplet gennemgang)
- **[docs/SETUP_GUIDE.md](docs/SETUP_GUIDE.md)** - Initial setup and configuration
- **[docs/GPIO2_ST_QUICK_START.md](docs/GPIO2_ST_QUICK_START.md)** - Quick start: GPIO2 + ST Logic integration
- **[docs/COUNTER_COMPARE_QUICK_START.md](docs/COUNTER_COMPARE_QUICK_START.md)** - Quick start: Counter compare feature

### 📖 Core Documentation (Root)
- **[MODBUS_REGISTER_MAP.md](MODBUS_REGISTER_MAP.md)** - ⭐ **COMPLETE Modbus register reference**
  - All registers: Fixed (ST Logic) + Dynamic (Counters/Timers/GPIO)
  - IR 200-293: ST Logic status & performance statistics
  - HR 200-237: ST Logic control & interval
  - Multi-register byte order (DINT/DWORD/REAL: LSW first, MSW second)
  - Address collision avoidance guide
  - Python pymodbus examples

- **[CHANGELOG.md](CHANGELOG.md)** - Complete version history
  - v5.0.0: IEC 61131-3 type system (INT 16-bit, DINT 32-bit)
  - v4.1.0: Performance monitoring & Modbus integration
  - v4.0.0-v4.0.2: Persistent registers & watchdog
  - v3.0.0-v3.3.0: Telnet & Wi-Fi features

### 🔧 Feature Guides (docs/)
- **[docs/FEATURE_GUIDE.md](docs/FEATURE_GUIDE.md)** - Feature-by-feature documentation
- **[docs/ESP32_Module_Architecture.md](docs/ESP32_Module_Architecture.md)** - Architecture deep-dive
- **[docs/GPIO_MAPPING_GUIDE.md](docs/GPIO_MAPPING_GUIDE.md)** - GPIO configuration guide

### 📊 ST Logic Programming
- **[docs/README_ST_LOGIC.md](docs/README_ST_LOGIC.md)** - ⭐ **ST Logic programming guide** (Start here!)
  - Data types: BOOL, INT (16-bit), DINT (32-bit), DWORD, REAL
  - Variable binding examples
  - Built-in functions reference

- **[docs/ST_USAGE_GUIDE.md](docs/ST_USAGE_GUIDE.md)** - ST Logic usage guide
  - Quick start (4 steps)
  - Syntax examples
  - Best practices

- **[docs/ST_IEC61131_COMPLIANCE.md](docs/ST_IEC61131_COMPLIANCE.md)** - IEC 61131-3 compliance reference
  - Standard compliance matrix
  - Data type specifications
  - Operator reference

- **[ST_MONITORING.md](ST_MONITORING.md)** - ⭐ **ST Logic performance tuning guide**
  - CLI commands: show logic stats, show logic X timing
  - Performance monitoring workflow
  - Optimization strategies
  - Modbus register access examples

- **[TIMING_ANALYSIS.md](TIMING_ANALYSIS.md)** - ST Logic timing deep dive
  - Fixed rate scheduler analysis
  - Execution interval control
  - Jitter analysis and recommendations

- **[ST_COMPLETE_TEST_PLAN.md](ST_COMPLETE_TEST_PLAN.md)** - Complete ST Logic test suite
  - 59 test cases covering all features
  - Copy/paste ready CLI commands
  - DINT/DWORD/REAL multi-register tests

- **[docs/ST_FUNCTION_TESTS.md](docs/ST_FUNCTION_TESTS.md)** - ST builtin function tests
- **[docs/ST_FUNCTIONS_TODO.md](docs/ST_FUNCTIONS_TODO.md)** - Future ST function ideas

### ⚙️ Counter & Timer Guides
- **[docs/COUNTER_COMPARE_REFERENCE.md](docs/COUNTER_COMPARE_REFERENCE.md)** - Counter compare feature reference
- **[docs/COUNTER_CONFIG_TEMPLATES.md](docs/COUNTER_CONFIG_TEMPLATES.md)** - Counter configuration templates

### 🐛 Development & Debugging
- **[CLAUDE.md](CLAUDE.md)** - ⭐ **Developer navigation hub** (Danish)
  - Quick links by task
  - Documentation modules overview
  - 30-second project summary

- **[CLAUDE_INDEX.md](CLAUDE_INDEX.md)** - Main entry point for developers
- **[CLAUDE_SETUP.md](CLAUDE_SETUP.md)** - Security rules & working directory
- **[CLAUDE_WORKFLOW.md](CLAUDE_WORKFLOW.md)** - Code modification guidelines
- **[CLAUDE_ARCH.md](CLAUDE_ARCH.md)** - Layered architecture & file reference

- **[BUGS.md](BUGS.md)** - Complete bug tracking (125+ bugs documented)
  - All fixes with code examples
  - Root cause analysis
  - Test verification

- **[BUGS_INDEX.md](BUGS_INDEX.md)** - ⭐ **Quick bug lookup** (Read this first!)
  - Status summary (all critical bugs fixed)
  - Quick lookup by category
  - 10 seconds to find relevant bugs

### 🔬 Analysis & Architecture
- **[ST_MODULE_ANALYSIS.md](ST_MODULE_ANALYSIS.md)** - ST Logic module analysis
- **[ST_SYNTAX_ANALYSIS.md](ST_SYNTAX_ANALYSIS.md)** - ST syntax analysis
- **[ST_TYPE_REFACTOR_PLAN.md](ST_TYPE_REFACTOR_PLAN.md)** - Type system refactor plan
- **[docs/LOGIC_BLOCK_HLD.md](docs/LOGIC_BLOCK_HLD.md)** - Logic block high-level design
- **[docs/ESP32_Migration_Analysis.md](docs/ESP32_Migration_Analysis.md)** - ESP32 migration analysis
- **[docs/W5500_Ethernet_Integration_Plan.md](docs/W5500_Ethernet_Integration_Plan.md)** - Ethernet integration plan

### 🧪 Testing
- **[docs/TEST_GPIO2_ST_LOGIC.md](docs/TEST_GPIO2_ST_LOGIC.md)** - GPIO2 + ST Logic tests
