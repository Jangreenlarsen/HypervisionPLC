# Modbus Register Map - ESP32 Modbus RTU Server

**Version:** v4.7.2
**Dato:** 2026-01-04
**Hardware:** ESP32-WROOM-32

---

## 📋 Overview

Dette dokument beskriver **ALLE** Modbus registre, coils og discrete inputs som ESP32 Modbus RTU Serveren bruger.

**Register Typer:**
- **Holding Registers (HR):** Read-Write, 16-bit, FC03/FC06/FC10
- **Input Registers (IR):** Read-Only, 16-bit, FC04
- **Coils (C):** Read-Write, 1-bit, FC01/FC05/FC0F
- **Discrete Inputs (DI):** Read-Only, 1-bit, FC02

**⚠️ VIGTIGT - CLI Kommandoer (v4.7.2+):**
- Læs **Holding Registers (HR):** `read holding-reg <addr> <count>` (eller: `read reg`)
- Læs **Input Registers (IR):** `read i-reg <addr> <count>` ⚠️ **Ikke "read reg"!**
- Skriv **Holding Registers:** `set holding-reg STATIC/DYNAMIC ...` (eller: `set reg`)
- **ST Logic EXPORT variable VALUES** ligger i **IR 220-251** (ikke HR!) → Brug `read i-reg 220 32`
- **v5.1.0:** Kun variabler markeret med `EXPORT` keyword i ST program er synlige i IR 220-251

**Addressing:**
- Alle adresser er 0-baserede (Modbus standard)
- 32-bit værdier bruger 2 på hinanden følgende registre (LSW first, MSW second)
  - DINT/DWORD/REAL: Register N = LSW (Least Significant Word), Register N+1 = MSW (Most Significant Word)
  - Eksempel: Value 100000 (0x000186A0) → HR100=0x86A0 (34464), HR101=0x0001 (1)

**Modbus Function Codes:**
- **FC03** (Read Holding Regs): Læs HR (bruges sjældent for ST vars)
- **FC04** (Read Input Regs): ✅ **Læs IR** ← Brug denne til ST Logic OUTPUT!
- **FC06** (Write Single HR): ✅ **Skriv til HR** ← Brug denne til ST Logic INPUT!
- **FC16** (Write Multiple HRs): ✅ **Skriv til HR** ← Brug denne til multi-register (DINT/REAL)!

**⚠️ VIGTIGT:** Du kan **IKKE** skrive til Input Registers (IR)! De er read-only og opdateres kun af systemet.

---

## 🔄 ST Logic Variable Bindings - Praktisk Guide

### Binding Modes

| Mode | Modbus → ST | ST → Modbus | CLI Kommando | Brug Til |
|------|-------------|-------------|--------------|----------|
| **INPUT** | ✅ HR (FC06/FC16) | ✅ IR 220-251 (auto EXPORT) | `bind var hr:100 input` | Setpoints, commands fra SCADA |
| **OUTPUT** | - | ✅ IR 220-251 (auto EXPORT)<br>+ IR custom | `bind var ir:300 output` | Status, målinger til SCADA |
| **BOTH** | ✅ HR (FC06/FC16) | ✅ IR 220-251 (auto EXPORT)<br>+ IR custom | `bind var both hr:100 ir:300` | Begge retninger |

**Note (v5.1.0):** IR 220-251 automatic mapping requires `EXPORT` keyword in ST program variable declaration.

### Praktisk Eksempel: Temperature Controller

**ST Program:**
```st
PROGRAM TempControl
VAR
  setpoint: REAL;        # Ønsket temperatur (fra SCADA)
  actual: REAL;          # Målt temperatur (til SCADA)
  control: REAL;         # PID output (begge veje)
END_VAR

bind setpoint hr:100 input         # SCADA → ST
bind actual ir:300 output          # ST → SCADA (+ auto IR 221)
bind control both hr:102 ir:302    # Begge veje (+ auto IR 222)
END_PROGRAM
```

**Modbus Access (Python):**
```python
from pymodbus.client import ModbusSerialClient

client = ModbusSerialClient(port='COM3', baudrate=115200, slave=1)

# === SKRIV til ST Program (INPUT) ===
# Sæt setpoint = 25.5°C via HR 100-101 (REAL)
client.write_registers(100, float_to_regs(25.5))        # FC16

# Sæt control output = 40% via HR 102-103 (REAL)
client.write_registers(102, float_to_regs(40.0))        # FC16

# === LÆS fra ST Program (OUTPUT) ===
# Læs setpoint fra IR 220 (automatisk mapping)
result = client.read_input_registers(220, 2)            # FC04
setpoint_readback = regs_to_float(result.registers)

# Læs actual fra IR 221 (automatisk) ELLER IR 300 (manual)
result = client.read_input_registers(221, 2)            # FC04
actual1 = regs_to_float(result.registers)
result = client.read_input_registers(300, 2)            # FC04
actual2 = regs_to_float(result.registers)
# actual1 == actual2 (identiske!)

# Læs control fra IR 222 (automatisk) ELLER IR 302 (manual)
result = client.read_input_registers(222, 2)            # FC04
control1 = regs_to_float(result.registers)
result = client.read_input_registers(302, 2)            # FC04
control2 = regs_to_float(result.registers)
# control1 == control2 (identiske!)
```

**CLI Kommandoer (ESP32 Terminal):**
```bash
# Skriv til ST Program (INPUT)
write h-reg 100 value real 25.5          # Sæt setpoint
write h-reg 102 value real 40.0          # Sæt control output

# Læs fra ST Program (OUTPUT)
read i-reg 220 real                # Læs setpoint (auto IR)
read i-reg 221 real                # Læs actual (auto IR)
read i-reg 300 real                # Læs actual (manual IR)
read i-reg 222 real                # Læs control (auto IR)
read i-reg 302 real                # Læs control (manual IR)
```

**Data Flow Diagram:**
```
┌─────────────────────────────────────────────────────────┐
│  setpoint (Variable #0 i Logic1)                        │
├─────────────────────────────────────────────────────────┤
│  IR 220 (auto) ←─ ST Program ─→ HR 100 (input)         │
│      ↑ FC04                          ↓ FC06/FC16        │
│    SCADA læser                    SCADA skriver         │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  actual (Variable #1 i Logic1)                          │
├─────────────────────────────────────────────────────────┤
│  IR 221 (auto) ←─ ST Program ─→ IR 300 (output)        │
│      ↑ FC04                          ↑ FC04             │
│    SCADA læser                    SCADA læser           │
│  (identiske værdier på begge!)                          │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  control (Variable #2 i Logic1)                         │
├─────────────────────────────────────────────────────────┤
│  IR 222 (auto) ←─ ST Program ─→ HR 102 (input)         │
│      ↑ FC04          ↕            ↓ FC06/FC16           │
│  IR 302 (output) ←───┘         SCADA skriver            │
│      ↑ FC04                                             │
│    SCADA læser (3 steder total!)                        │
└─────────────────────────────────────────────────────────┘
```

---

## 🎯 Register Allocation Guide

### Safe Register Ranges (Avoiding Conflicts)

For at undgå konflikter mellem system funktioner og bruger data, skal du bruge disse safe ranges:

#### 📊 Holding Registers (HR) - Safe Ranges

**Total size: 256 registers (HR 0-255)**

| Range | Status | Anbefalet Brug | Konflikt Med |
|-------|--------|----------------|-------------|
| **HR 0-17** | ⚠️ **RESERVED** | Analog I/O (FEAT-034/035/036/037, ES32D26): Vi1-4 raw+value (HR0-7), Ii1-4 raw+value (HR8-15), AO1-2 setpoint (HR16-17) | Analog driver |
| **HR 18-19** | ✅ **SAFE** | General purpose data, test variables | Ingen |
| **HR 20-89** | ✅ **SAFE** | ST Logic test variables, temporary data | Ingen |
| **HR 90-99** | ✅ **SAFE** | Reserved for future use | Ingen |
| **HR 100-179** | ⚠️ **RESERVED** | Counter default allocation (4 counters × 20 registers) | Counter engine |
| **HR 180-199** | ✅ **SAFE** | Custom data, persistent registers | Ingen |
| **HR 200-203** | 🔒 **SYSTEM** | ST Logic control registers (4 programs) | ST Logic engine |
| **HR 204-235** | 🔒 **SYSTEM** | ST Logic variable inputs (32 registers) | ST Logic engine |
| **HR 236-237** | 🔒 **SYSTEM** | ST Logic execution interval (2 registers) | ST Logic engine |
| **HR 238-255** | ✅ **SAFE** | Custom data (18 registers available) | Ingen |
| **HR 256+** | ❌ **N/A** | Does not exist (HOLDING_REGS_SIZE = 256) | - |

#### 🎚️ Coils (C) - Safe Ranges

| Range | Status | Anbefalet Brug | Konflikt Med |
|-------|--------|----------------|-------------|
| **C 0-49** | ✅ **SAFE** | GPIO outputs, timer outputs, ST Logic outputs | Ingen (med omtanke) |
| **C 50-255** | ✅ **SAFE** | Custom control bits, alarms, status flags | Ingen |

#### 📥 Discrete Inputs (DI) - Safe Ranges

| Range | Status | Anbefalet Brug | Konflikt Med |
|-------|--------|----------------|-------------|
| **DI 0-49** | ✅ **SAFE** | GPIO inputs, sensor inputs | Ingen (med omtanke) |
| **DI 50-255** | ✅ **SAFE** | Custom sensor inputs, status bits | Ingen |

#### 🔢 Input Registers (IR) - Safe Ranges

**Total size: 256 registers (IR 0-255)**

| Range | Status | Anbefalet Brug | Konflikt Med |
|-------|--------|----------------|-------------|
| **IR 0-199** | ✅ **SAFE** | Read-only sensor data, calculated values | Ingen |
| **IR 200-203** | 🔒 **SYSTEM** | ST Logic status registers (4 programs) | ST Logic engine |
| **IR 204-207** | 🔒 **SYSTEM** | ST Logic execution count (4 programs) | ST Logic engine |
| **IR 208-211** | 🔒 **SYSTEM** | ST Logic error count (4 programs) | ST Logic engine |
| **IR 212-215** | 🔒 **SYSTEM** | ST Logic error codes (4 programs) | ST Logic engine |
| **IR 216-219** | 🔒 **SYSTEM** | ST Logic variable count (4 programs) | ST Logic engine |
| **IR 220-251** | 🔒 **SYSTEM** | ST Logic EXPORT variables (dynamic pool, 32 regs) | ST Logic engine |
|                |               | **v5.1.0:** Dynamic allocation based on EXPORT keyword | |
|                |               | - Only variables marked EXPORT are visible | |
|                |               | - Flexible allocation across 4 programs | |
|                |               | - Type-aware: REAL/DINT/DWORD use 2 regs, INT/BOOL use 1 reg | |
|                |               | **Note:** Pool shared across all programs (max 32 regs total) | |
| **IR 252-259** | 🔒 **SYSTEM** | ST Logic min exec time (4 programs × 2 regs) | ST Logic engine |
| **IR 260-267** | 🔒 **SYSTEM** | ST Logic max exec time (4 programs × 2 regs) | ST Logic engine |
| **IR 268-275** | 🔒 **SYSTEM** | ST Logic avg exec time (4 programs × 2 regs) | ST Logic engine |
| **IR 276-283** | 🔒 **SYSTEM** | ST Logic overrun count (4 programs × 2 regs) | ST Logic engine |
| **IR 284-285** | 🔒 **SYSTEM** | ST Logic global cycle min time (2 regs) | ST Logic engine |
| **IR 286-287** | 🔒 **SYSTEM** | ST Logic global cycle max time (2 regs) | ST Logic engine |
| **IR 288-289** | 🔒 **SYSTEM** | ST Logic global cycle overrun (2 regs) | ST Logic engine |
| **IR 290-291** | 🔒 **SYSTEM** | ST Logic total cycles (2 regs) | ST Logic engine |
| **IR 292-293** | 🔒 **SYSTEM** | ST Logic execution interval (read-only, 2 regs) | ST Logic engine |
| **IR 294-255** | ❌ **N/A** | Does not exist (IR ends at 293) | - |
| **IR 256+** | ❌ **N/A** | Does not exist (INPUT_REGS_SIZE = 256) | - |

---

### 💡 Best Practices

#### 1. **Test og Development**
- **HR 20-89:** Brug dette område til ST Logic tests og temporary variables
- **C 0-20:** Brug til test outputs/inputs
- Eksempel:
  ```bash
  set logic 1 bind test_input reg:20 input
  set logic 1 bind test_output reg:25 output
  ```

#### 1a. **Analog I/O (FEAT-034/035/036/037, ES32D26)**
Faste registre (ikke bruger-omkonfigurerbare i v1), allokeret via register-allokatoren når kanalen er `enabled`:

| Kanal | Raw (mV) | Kalibreret værdi (×100) |
|-------|----------|--------------------------|
| Vi1 (0-10V) | HR 0 | HR 1 |
| Vi2 (0-10V) | HR 2 | HR 3 |
| Vi3 (0-10V) | HR 4 | HR 5 |
| Vi4 (0-10V) | HR 6 | HR 7 |
| Ii1 (4-20mA) | HR 8 | HR 9 |
| Ii2 (4-20mA) | HR 10 | HR 11 |
| Ii3 (4-20mA) | HR 12 | HR 13 |
| Ii4 (4-20mA) | HR 14 | HR 15 |

| Kanal | Setpoint (×100) |
|-------|-------------------|
| AO1 | HR 16 |
| AO2 | HR 17 |

"Kalibreret værdi"/"Setpoint" er ×100 fixed-point (fx 1000 = 10,00V eller 20,00mA). Vi1 (GPIO14) og Vi3 (GPIO27) er ADC2 — registrene beholder deres sidste gyldige værdi mens WiFi er tilsluttet (kendt ESP32 HW-begrænsning). Se `show analog` / `docs/manual/04_Web_Dashboard_og_Monitor.md` for kalibrering (scale/offset).

#### 2. **Counter Configuration**
Counter default allocation bruger **HR 100-179** (4 counters × 20 registers):
- Counter 1: HR 100-119
- Counter 2: HR 120-139
- Counter 3: HR 140-159
- Counter 4: HR 160-179

**⚠️ VIGTIGT:** Hvis du IKKE bruger counters, er HR 100-179 safe for andre formål.

**Bedste praksis:**
- Hvis du bruger counters: Undgå HR 100-179 helt
- Hvis du IKKE bruger counters: Du kan bruge HR 100-179 frit
- Brug altid `show counters` for at tjekke om counters er konfigureret

#### 3. **ST Logic Variable Bindings**
ST Logic bruger **IR/HR 200-293** til system registers:
- IR 200-293: Status, statistics, performance (READ-ONLY) - 94 registers
- HR 200-237: Control, interval settings (READ-WRITE) - 38 registers
- **HR 238-255: SAFE for bruger data** - 18 ledige registers! ✅

**⚠️ UNDGÅ:**
```bash
# FORKERT - Kolliderer med ST Logic system registers!
set logic 1 bind my_var reg:200 input
set logic 1 bind temp reg:220 output  # IR220-251 er ST Logic variable values!
```

**✅ BRUG:**
```bash
# KORREKT - Safe ranges
set logic 1 bind my_var reg:20 input      # HR 20-89 safe zone
set logic 1 bind result reg:25 output
set logic 1 bind large_data reg:240 input  # HR 238-255 også safe!
```

#### 4. **Multi-Register Typer (DINT/DWORD/REAL)**
32-bit typer kræver 2 på hinanden følgende registers:
- **DINT:** 2 registers (eksempel: HR 40-41)
- **DWORD:** 2 registers (eksempel: HR 42-43)
- **REAL:** 2 registers (eksempel: HR 60-61)

**Byte order:** LSW first, MSW second (little-endian)

**Eksempel:**
```bash
# DINT variable "large_value" bruger HR 40-41
set logic 1 bind large_value reg:40 input

# Skriv DINT værdi (CLI håndterer automatisk LSW/MSW split)
write h-reg 40 value dint 100000
# Resultat: HR40=34464 (LSW), HR41=1 (MSW)
```

**⚠️ VIGTIGT:** Sørg for at HR N+1 også er i safe range!

#### 5. **Persistent Registers (NEW: v4.7.1 - `set reg STATIC` Multi-Type)**
Persistent registers kan gemmes til NVS og genindlæses ved boot med `set reg STATIC`.

**Understøttede typer (FEAT-001):**
- `uint` - 16-bit unsigned (0-65535)
- `int` - 16-bit signed (-32768 to 32767)
- `dint` - 32-bit signed (2 registers)
- `dword` - 32-bit unsigned (2 registers)
- `real` - 32-bit IEEE-754 float (2 registers)

**Safe ranges for STATIC registers:**
- **HR 0-99:** General purpose (100 registers)
- **HR 180-199:** Custom data (20 registers)
- **HR 238-255:** Custom data (18 registers) - NYT! ✅
- **Undgå:** HR 100-179 (counters), HR 200-237 (ST Logic)

**Eksempler:**
```bash
# 16-bit typer (1 register)
set reg STATIC 80 Value uint 1234
set reg STATIC 81 Value int -500

# 32-bit typer (2 registers) - NYT i v4.7.1!
set reg STATIC 240 Value dint 1000000     # Bruger HR 240-241
set reg STATIC 242 Value real 3.14159     # Bruger HR 242-243

# Gem til NVS
config save

# Efter reboot: Værdier genopstår automatisk! ✅
```

#### 6. **Collision Detection**
Systemet tjekker automatisk for register konflikter ved configuration.

**Hvis du får fejl:**
```
ERROR: Register 100 already allocated (Counter 1: value_reg)
```

**Løsning:**
1. Brug `show counters` for at se hvilke registers er i brug
2. Vælg et register fra safe range (HR 20-89 eller HR 294+)
3. Opdater din configuration

#### 7. **Documentation i Kode**
Når du bruger registers i ST Logic, dokumentér altid:
```structured-text
VAR
  setpoint: INT;      (* HR 20 - Temperature setpoint *)
  actual: INT;        (* HR 21 - Current temperature *)
  output: INT;        (* HR 25 - Heater control value *)
  alarm: BOOL;        (* Coil 10 - Temperature alarm *)
END_VAR
```

---

### 📋 Quick Reference: Anbefalede Ranges

**For almindelig brug:**
- **Test variables:** HR 20-89
- **Production data:** HR 294-499
- **Control bits:** Coils 0-49
- **Sensor inputs:** DI 0-49
- **Calculated values:** IR 0-199

**Undgå altid:**
- ❌ HR 200-293 (ST Logic system)
- ⚠️ HR 100-179 (Counter default - tjek først med `show counters`)

---

## 🔧 Register Kategorier

### 1. Fixed Register Mappings
Disse registre har **faste adresser** og håndteres automatisk af systemet:
- **ST Logic Status & Control** (IR/HR 200-293)

### 2. Dynamic Register Mappings
Disse registre har **bruger-definerede adresser** konfigureret via CLI:
- **Counter Registers** (index, raw, freq, overflow, ctrl)
- **Timer Registers** (ctrl/output)
- **Persistent Registers** (bruger-defineret data storage)

### 3. Dynamic Coil/Discrete Input Mappings
Disse bits har **bruger-definerede adresser** konfigureret via CLI:
- **GPIO Mappings** (pins → coils/discrete inputs)
- **Timer Outputs** (timer state → coils)

---

## 📊 Fixed Register Map: ST Logic (IR/HR 200-293)

### Input Registers (Read-Only)

#### **IR 200-203: Program Status**
| Register | Program | Bits | Beskrivelse |
|----------|---------|------|-------------|
| **200** | Logic1 | [15:0] | Status flags |
| **201** | Logic2 | [15:0] | Status flags |
| **202** | Logic3 | [15:0] | Status flags |
| **203** | Logic4 | [15:0] | Status flags |

**Status Bits:**
- Bit 0: `ST_LOGIC_STATUS_ENABLED` (0x0001) - Program enabled
- Bit 1: `ST_LOGIC_STATUS_COMPILED` (0x0002) - Program compiled successfully
- Bit 2: `ST_LOGIC_STATUS_RUNNING` (0x0004) - Currently executing
- Bit 3: `ST_LOGIC_STATUS_ERROR` (0x0008) - Has error

**Eksempel:**
```
IR 200 = 0x0003  →  Logic1 is enabled + compiled (bits 0+1 set)
IR 201 = 0x0009  →  Logic2 has error + is enabled (bits 0+3 set)
```

---

#### **IR 204-207: Execution Count**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **204** | Logic1 | 16-bit | Number of times executed (wraps at 65535) |
| **205** | Logic2 | 16-bit | Number of times executed |
| **206** | Logic3 | 16-bit | Number of times executed |
| **207** | Logic4 | 16-bit | Number of times executed |

**Note:** Wraps at 65535 → 0. Se også IR 290-291 for total cycles.

---

#### **IR 208-211: Error Count**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **208** | Logic1 | 16-bit | Number of execution errors (wraps at 65535) |
| **209** | Logic2 | 16-bit | Number of execution errors |
| **210** | Logic3 | 16-bit | Number of execution errors |
| **211** | Logic4 | 16-bit | Number of execution errors |

**Note:** Nulstilles ved `set logic stats reset X` eller Modbus control bit 2.

---

#### **IR 212-215: Last Error Code**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **212** | Logic1 | 16-bit | 0 = no error, 1+ = error present |
| **213** | Logic2 | 16-bit | Error code |
| **214** | Logic3 | 16-bit | Error code |
| **215** | Logic4 | 16-bit | Error code |

**Error Codes:**
- `0` = No error
- `1` = Error present (se error string via CLI `show logic X`)

---

#### **IR 216-219: Variable Binding Count**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **216** | Logic1 | 16-bit | Number of variable bindings for this program |
| **217** | Logic2 | 16-bit | Variable binding count |
| **218** | Logic3 | 16-bit | Variable binding count |
| **219** | Logic4 | 16-bit | Variable binding count |

**Note:** Cached værdi, opdateres automatisk ved bind/unbind operationer.

---

#### **IR 220-251: Variable Values (Dynamic Pool Allocation - v5.1.0)**

**🎯 NY FEATURE (v5.1.0): EXPORT Keyword & Dynamic Pool**

IR 220-251 (32 registers total) er nu en **dynamisk pool** som deles fleksibelt mellem de 4 ST Logic programmer baseret på EXPORT keyword.

**Variable Visibility Control:**
```st
VAR
  temp : INT EXPORT;        (* ✅ Synlig i IR 220-251 - bruger 1 register *)
  setpoint : REAL EXPORT;   (* ✅ Synlig i IR 220-251 - bruger 2 registre *)
  internal : INT;           (* ❌ IKKE synlig i IR - kun intern brug *)
END_VAR
```

**Pool Allocation Eksempel:**
```
Logic1: 3 EXPORT vars (5 registers)  → IR 220-224 (5 regs)
Logic2: 2 EXPORT vars (2 registers)  → IR 225-226 (2 regs)
Logic3: 6 EXPORT vars (10 registers) → IR 227-236 (10 regs)
Logic4: 4 EXPORT vars (6 registers)  → IR 237-242 (6 regs)
Total: 23/32 registers used, 9 free
```

**Type-Aware Register Allocation:**
| Type | Registers Used | Format |
|------|----------------|--------|
| **INT** | 1 register | 16-bit signed integer |
| **BOOL** | 1 register | 0x0000 (false) eller 0x0001 (true) |
| **REAL** | 2 registers | 32-bit float (LSW first, MSW second) |
| **DINT** | 2 registers | 32-bit signed integer (LSW first, MSW second) |
| **DWORD** | 2 registers | 32-bit unsigned (LSW first, MSW second) |

**Variable Mapping (EXPORT Only):**
- Kun variabler markeret med **EXPORT** keyword vises i IR 220-251
- Variabler uden EXPORT er program-internal (ingen Modbus access)
- IR allocation er **contiguous** per program (ingen huller)
- Automatic reallocation ved program compile/delete/reload

**🔄 AUTOMATISK IR MAPPING vs MANUAL BINDINGS (v5.1.0):**

IR 220-251 opdateres **ALTID automatisk** for EXPORT variabler - **uanset binding mode!**

| Binding Mode | Automatisk IR 220-251 | Manual Bindings | Total Modbus Access |
|--------------|----------------------|-----------------|---------------------|
| **Ingen (kun EXPORT)** | ✅ Ja (read-only) | - | **1 sted** (IR 220-251) |
| **INPUT + EXPORT** | ✅ Ja (read-only) | HR (input) | **2 steder** (IR + HR) |
| **OUTPUT + EXPORT** | ✅ Ja (read-only) | IR custom (output) | **2 steder** (IR auto + IR custom) |
| **BOTH + EXPORT** | ✅ Ja (read-only) | HR (input) + IR custom (output) | **3 steder** (IR auto + HR + IR custom) |

**Eksempel - EXPORT + BOTH Binding:**
```st
VAR
  temp : INT EXPORT;      (* Automatisk IR 220-251 + bindings *)
  internal : INT;         (* Kun intern - ingen IR mapping *)
END_VAR
```
```bash
bind temp both hr:100 ir:300    # temp tilgængelig 3 steder:
                                # - IR 220-251 (automatisk EXPORT, read-only)
                                # - HR 100 (INPUT: SCADA → ST)
                                # - IR 300 (OUTPUT: ST → SCADA)
```

**Eksempel - EXPORT Uden Bindings:**
```st
VAR
  sensor : INT EXPORT;    (* Kun automatisk IR 220-251 *)
END_VAR
```
```bash
# Ingen bind kommando nødvendig!
# sensor er automatisk tilgængelig i IR 220-251 (read-only)
read i-reg 220 1    # Læs sensor værdi
```

**⚠️ ANBEFALING:** Brug IKKE IR 220-251 i manual bindings! De er allerede automatisk mappet.

```bash
bind temp both hr:100 ir:220  # ❌ DÅRLIGT - IR 220 allerede automatisk via EXPORT
bind temp both hr:100 ir:300  # ✅ GODT - Brug IR uden for 220-251 for OUTPUT binding
```

**Pool Statistics & Queries:**
- IR pool allocation kan ses med CLI: `show logic <id>` (viser ir_pool_offset og ir_pool_size)
- Total pool usage: Sum af alle programs ir_pool_size
- Free space: 32 - total used

---

### Performance Statistics (v4.1.0)

#### **IR 252-259: Min Execution Time (µs)**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **252-253** | Logic1 | 32-bit | Minimum execution time (microseconds) |
| **254-255** | Logic2 | 32-bit | Minimum execution time |
| **256-257** | Logic3 | 32-bit | Minimum execution time |
| **258-259** | Logic4 | 32-bit | Minimum execution time |

**32-bit Format:** `VALUE = (IR[N] << 16) | IR[N+1]`

**Eksempel:**
```python
# Læs Logic1 min time (IR 252-253)
high = read_input_register(252)  # High word (bits 31-16)
low = read_input_register(253)   # Low word (bits 15-0)
min_us = (high << 16) | low       # Combine to 32-bit
print(f"Logic1 min execution: {min_us} µs")
```

---

#### **IR 260-267: Max Execution Time (µs)**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **260-261** | Logic1 | 32-bit | Maximum execution time (microseconds) |
| **262-263** | Logic2 | 32-bit | Maximum execution time |
| **264-265** | Logic3 | 32-bit | Maximum execution time |
| **266-267** | Logic4 | 32-bit | Maximum execution time |

**Same 32-bit format as Min Execution Time.**

---

#### **IR 268-275: Avg Execution Time (µs, calculated)**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **268-269** | Logic1 | 32-bit | Average execution time (calculated: total_us / exec_count) |
| **270-271** | Logic2 | 32-bit | Average execution time |
| **272-273** | Logic3 | 32-bit | Average execution time |
| **274-275** | Logic4 | 32-bit | Average execution time |

**Calculation:** `avg = total_execution_us / execution_count`

**Note:** Returns 0 if execution_count = 0.

---

#### **IR 276-283: Overrun Count**
| Register | Program | Type | Beskrivelse |
|----------|---------|------|-------------|
| **276-277** | Logic1 | 32-bit | Number of times execution > target interval |
| **278-279** | Logic2 | 32-bit | Overrun count |
| **280-281** | Logic3 | 32-bit | Overrun count |
| **282-283** | Logic4 | 32-bit | Overrun count |

**Definition:** Overrun = execution time > `execution_interval_ms` (default 10ms)

**Eksempel:**
- Hvis Logic1 execution = 12ms og interval = 10ms → overrun count++
- Overrun rate % = (overrun_count / execution_count) × 100

---

### Global Cycle Statistics

#### **IR 284-285: Global Cycle Min Time (ms)**
| Register | Type | Beskrivelse |
|----------|------|-------------|
| **284-285** | 32-bit | Minimum total cycle time for all 4 programs (milliseconds) |

**Cycle Time Definition:** Tid for at køre Logic1 + Logic2 + Logic3 + Logic4 sequentially.

---

#### **IR 286-287: Global Cycle Max Time (ms)**
| Register | Type | Beskrivelse |
|----------|------|-------------|
| **286-287** | 32-bit | Maximum total cycle time for all 4 programs (milliseconds) |

---

#### **IR 288-289: Global Cycle Overrun Count**
| Register | Type | Beskrivelse |
|----------|------|-------------|
| **288-289** | 32-bit | Number of times total cycle > target interval |

**Definition:** Global overrun = (Logic1 + Logic2 + Logic3 + Logic4 execution time) > interval

---

#### **IR 290-291: Total Cycles Executed**
| Register | Type | Beskrivelse |
|----------|------|-------------|
| **290-291** | 32-bit | Total number of ST Logic cycles executed since boot/reset |

**Note:** Increments hver gang scheduler kører alle enabled programs.

---

#### **IR 292-293: Execution Interval (Read-Only Copy)**
| Register | Type | Beskrivelse |
|----------|------|-------------|
| **292-293** | 32-bit | Current execution interval in milliseconds (read-only) |

**Default:** 10ms

**Note:** For at ÆNDRE interval, brug HR 236-237 eller CLI `set logic interval:X`.

---

## 🎛️ Holding Registers (ST Logic Control)

### **HR 200-203: Program Control**
| Register | Program | Bits | Access | Beskrivelse |
|----------|---------|------|--------|-------------|
| **200** | Logic1 | [15:0] | R/W | Control flags |
| **201** | Logic2 | [15:0] | R/W | Control flags |
| **202** | Logic3 | [15:0] | R/W | Control flags |
| **203** | Logic4 | [15:0] | R/W | Control flags |

**Control Bits:**
- Bit 0: `ST_LOGIC_CONTROL_ENABLE` (0x0001) - Enable/disable program
  - Write 1 → enable program
  - Write 0 → disable program
- Bit 1: `ST_LOGIC_CONTROL_START` (0x0002) - Start/stop (future use)
- Bit 2: `ST_LOGIC_CONTROL_RESET_ERROR` (0x0004) - Reset error flag
  - Write 1 → clear error counter + error string
  - **Auto-clears after processing** (read-back = 0)

**Eksempel:**
```python
# Enable Logic1
write_register(200, 0x0001)  # Set bit 0

# Reset error for Logic2
write_register(201, 0x0004)  # Set bit 2 (auto-clears)

# Disable Logic3
write_register(202, 0x0000)  # Clear bit 0
```

---

### **HR 204-235: Variable Input - Direct Write to ST Logic Variables (v4.2.0)**
| Register Range | Program | Variables | Access | Beskrivelse |
|----------------|---------|-----------|--------|-------------|
| **204-211** | Logic1 | Var[0]-Var[7] | W | Direct write to Logic1 variables (bytecode) |
| **212-219** | Logic2 | Var[0]-Var[7] | W | Direct write to Logic2 variables (bytecode) |
| **220-227** | Logic3 | Var[0]-Var[7] | W | Direct write to Logic3 variables (bytecode) |
| **228-235** | Logic4 | Var[0]-Var[7] | W | Direct write to Logic4 variables (bytecode) |

**Status:** Implementeret i v4.2.0 - DIREKTE mapping uden variable binding lookup.

**Funktionalitet:**
- Modbus master skriver direkte til ST Logic variabel bytecode
- **Deterministisk mapping:** `prog_id = (addr - 204) / 8`, `var_index = (addr - 204) % 8`
- **Type-aware konvertering:**
  - `ST_TYPE_BOOL`: Value ≠ 0 konverteres til bool `true`/`false`
  - `ST_TYPE_REAL`: 16-bit integer castes til float
  - `ST_TYPE_INT/DWORD`: 16-bit signed integer
- **Bounds checking:** Writes ignoreres hvis program ikke compiled eller var_index >= var_count
- **Debug output:** `[ST_VAR_INPUT] Logic# var[#] = value (type=#)` hvis debug enabled

**Eksempler:**
```
# Skriv Logic1 var[0] (address 204) værdi 42
Write HR 204 = 42

# Skriv Logic2 var[3] (address 215) værdi 1 (BOOL)
Write HR 215 = 1 (true)

# Skriv Logic4 var[7] (address 235) værdi 255
Write HR 235 = 255
```

**Alternativ:** Variable bindings via CLI (`set logic X bind var_name reg:Y`) for permanent mapping med CLI kontrol.

---

### **HR 236-237: Execution Interval Control (v4.1.0)**
| Register | Type | Access | Beskrivelse |
|----------|------|--------|-------------|
| **236-237** | 32-bit | R/W | Global execution interval in milliseconds |

**Allowed Values:** 10, 20, 25, 50, 75, 100 (ms only)

**Validation:**
- Ugyldige værdier afvises automatisk
- Register nulstilles til nuværende værdi hvis invalid
- Debug output viser fejl

**Eksempel:**
```python
# Sæt interval til 50ms
write_register(236, 0)   # High word = 0
write_register(237, 50)  # Low word = 50

# Læs nuværende interval
high = read_holding_register(236)
low = read_holding_register(237)
interval = (high << 16) | low
print(f"Interval: {interval} ms")
```

**CLI Alternativ:**
```bash
set logic interval:50
save
```

---

## 🔢 Dynamic Register Mappings

Disse registre har **INGEN faste adresser**. Brugeren konfigurerer dem via CLI kommandoer.

### Counter Registers

Hver counter kan bruge op til **4 registre** (alle bruger-definerede adresser):

| Register Type | CLI Parameter | Type | Beskrivelse | Eksempel |
|---------------|---------------|------|-------------|----------|
| **value_reg** | `value-reg:X` | 16/32/64-bit | Scaled counter value = value × scale_factor | `value-reg:100` |
| **raw_reg** | `raw-reg:Y` | 16/32/64-bit | Prescaled value = value / prescaler | `raw-reg:104` |
| **freq_reg** | `freq-reg:Z` | 16-bit | Measured frequency in Hz (0-20000 Hz) | `freq-reg:108` |
| **ctrl_reg** | `ctrl-reg:V` | 16-bit | Control/status register (bitfield) | `ctrl-reg:110` |

**Multi-Register Support:**
- 8-bit counters: 1 register (value_reg uses HR X only)
- 16-bit counters: 1 register (value_reg uses HR X only)
- 32-bit counters: 2 registers (value_reg uses HR X-X+1, LSW first)
- 64-bit counters: 4 registers (value_reg uses HR X-X+3, LSW first)

**Note:** raw_reg følger samme multi-register layout som value_reg baseret på bit_width.

---

### Control Register Detaljeret Specifikation (ctrl_reg)

**ctrl_reg** er en 16-bit bitfield der indeholder både **kommandoer** (write) og **status** (read).

#### Bit Layout (alle 16 bits)

| Bit | Navn | Type | Beskrivelse | Detaljer |
|-----|------|------|-------------|----------|
| **0** | `RESET_CMD` | W | Reset command | Write 1 → nulstil counter til start_value. Auto-clears. |
| **1** | `START_CMD` | W | Start command | Write 1 → start counting (one-shot). Auto-clears. |
| **2** | `RUNNING_STATUS` | R | Running flag | Read-only: 1 = counting aktiv, 0 = stoppet |
| **3** | `OVERFLOW_FLAG` | R | Overflow detected | Read-only: 1 = overflow (værdi > max), 0 = normal |
| **4** | `COMPARE_MATCH` | R | Compare triggered | Read-only: 1 = værdi ≥ threshold, 0 = below |
| **5** | Reserved | - | Fremtidig brug | Altid 0 |
| **6** | Reserved | - | Fremtidig brug | Altid 0 |
| **7** | `DIRECTION_IND` | R | Direction | Read-only: 0 = counting up, 1 = counting down |
| **8-15** | Reserved | - | Fremtidig brug | Altid 0 |

#### Bit 0: RESET_CMD (Write-Only Command)
- **Funktionalitet:**
  - Write `1` → Reset counter værdi til `start_value`
  - Clearer overflow flag (bit 3) og compare match flag (bit 4)
  - Auto-clears: Næste read viser bit 0 = 0
- **Eksempel:**
  ```python
  write_register(110, 0x0001)  # Reset Counter1
  ```

#### Bit 1: START_CMD (Write-Only Command)
- **Funktionalitet:**
  - Write `1` → Start counting (enable counter)
  - Mode-afhængig: SW polling, ISR attach, eller HW PCNT start
  - Auto-clears: Næste read viser bit 1 = 0
- **Eksempel:**
  ```python
  write_register(110, 0x0002)  # Start Counter1
  ```

#### Bit 2: RUNNING_STATUS (Read-Only Status)
- **Funktionalitet:**
  - `1` = Counter aktiv (tæller edges/pulses)
  - `0` = Counter stoppet
- **Eksempel:**
  ```python
  ctrl = read_register(110)
  running = (ctrl & 0x0004) != 0
  ```

#### Bit 3: OVERFLOW_FLAG (Read-Only Status)
- **Funktionalitet:**
  - `1` = Counter værdi > max for bit_width
  - `0` = Normal drift
  - Overflow limits: 8-bit=255, 16-bit=65535, 32-bit=2^32-1, 64-bit=2^64-1
  - Clears kun via RESET_CMD (bit 0)
- **Eksempel:**
  ```python
  ctrl = read_register(110)
  if (ctrl & 0x0008):
      print("Overflow!")
      write_register(110, 0x0001)  # Reset
  ```

#### Bit 4: COMPARE_MATCH (Read-Only Status)
- **Funktionalitet:**
  - `1` = Counter værdi ≥ compare_value
  - `0` = Counter værdi < compare_value
  - Kun aktiv hvis compare_enabled=true
  - Clears via RESET_CMD eller reset_on_read
- **Eksempel:**
  ```python
  ctrl = read_register(110)
  if (ctrl & 0x0010):
      print("Compare threshold reached!")
  ```

#### Bit 7: DIRECTION_IND (Read-Only Status)
- **Funktionalitet:**
  - `0` = Counting UP (incrementing)
  - `1` = Counting DOWN (decrementing)
- **Eksempel:**
  ```python
  ctrl = read_register(110)
  direction = "DOWN" if (ctrl & 0x0080) else "UP"
  ```

#### Typiske Ctrl Reg Værdier

| Hex | Dec | Bits | Betydning |
|-----|-----|------|-----------|
| `0x0000` | 0 | None | Counter stopped, no flags |
| `0x0004` | 4 | Bit 2 | Running (normal) |
| `0x000C` | 12 | Bit 2+3 | Running + overflow |
| `0x0014` | 20 | Bit 2+4 | Running + compare match |
| `0x001C` | 28 | Bit 2+3+4 | Running + overflow + compare |
| `0x0084` | 132 | Bit 2+7 | Running + counting DOWN |

---

**CLI Configuration Example:**
```bash
set counter 1 value-reg:100 raw-reg:104 freq-reg:108 ctrl-reg:110
set counter 1 enabled:true prescaler:16 scale:10 resolution:32
save
```

**Modbus Read Example:**
```python
# Læs Counter1 scaled value (32-bit)
low = read_holding_register(100)   # LSW
high = read_holding_register(101)  # MSW
value = (high << 16) | low

# Læs Counter1 frequency
freq_hz = read_holding_register(108)
print(f"Counter1: value={value}, freq={freq_hz} Hz")

# Check status flags
ctrl = read_holding_register(110)
running = (ctrl & 0x0004) != 0
overflow = (ctrl & 0x0008) != 0
print(f"Running: {running}, Overflow: {overflow}")

# Reset counter
write_register(110, 0x0001)
```

**Register Count Per Counter:**
- Minimum: 1 register (value_reg kun, 16-bit)
- Maximum: 11 registers (value_reg 4 + raw_reg 4 + freq_reg 1 + ctrl_reg 1 + compare_value_reg 1, 64-bit mode)
- Typical: 4 registers (value_reg, raw_reg, freq_reg, ctrl_reg)
- Total capacity: Op til 4 counters × ~4 registers = ~16 registre typisk

---

### Timer Registers

Hver timer bruger **1 register** (bruger-defineret adresse):

| Register Type | CLI Parameter | Type | Beskrivelse | Eksempel |
|---------------|---------------|------|-------------|----------|
| **ctrl_reg** | `ctrl-reg:X` | 16-bit | Timer control/status register | `ctrl-reg:50` |

**Control Register Bits (ctrl_reg):**
- Bit 0: Timer enabled (read/write)
- Bit 1: Timer mode (0-4, read-only)
- Bits 2-7: Timer state (mode-dependent, read-only)
- Bit 8: Manual trigger (write 1 to trigger mode 1/2)
- Bit 9: Reset timer (write 1 to reset)

**CLI Configuration Example:**
```bash
set timer 1 ctrl-reg:50
set timer 1 mode:3 enabled:true t1:1000 t2:500  # Astable blink
save
```

**Modbus Control Example:**
```python
# Enable Timer1
ctrl = read_holding_register(50)
ctrl |= 0x0001  # Set bit 0
write_register(50, ctrl)

# Trigger Timer1 (mode 1/2 only)
write_register(50, 0x0100)  # Set bit 8

# Read Timer1 state
ctrl = read_holding_register(50)
enabled = (ctrl & 0x0001) != 0
mode = (ctrl >> 1) & 0x07
print(f"Timer1: enabled={enabled}, mode={mode}")
```

---

### Persistent Registers

**Persistent registers** er bruger-defineret data storage som gemmes til NVS.

| CLI Parameter | Type | Beskrivelse | Eksempel |
|---------------|------|-------------|----------|
| `persist-reg:X=value` | 16-bit | Store arbitrary 16-bit value at address X | `persist-reg:150=4242` |

**Max Count:** 32 persistent registers (configurerbar)

**CLI Configuration:**
```bash
set persist enable:true
set persist group:0 persist-reg:150=4242
set persist group:1 persist-reg:151=1234
save
```

**Features:**
- Values overlever reboot (stored in NVS)
- Read/Write via Modbus FC03/FC06/FC10
- Kan bruges til user settings, calibration data, etc.

**Modbus Example:**
```python
# Læs persistent register 150
value = read_holding_register(150)
print(f"Persist reg 150: {value}")

# Skriv til persistent register 150
write_register(150, 9999)
# Note: Auto-saves til NVS (konfigureret via CLI)
```

---

## 🔘 Coils & Discrete Inputs

**Coils** og **Discrete Inputs** er dynamisk mappede til GPIO pins eller timer outputs.

### GPIO → Coil/Discrete Input Mapping

| CLI Parameter | Type | Direction | Beskrivelse | Eksempel |
|---------------|------|-----------|-------------|----------|
| `coil:X` | Coil | Output | Map GPIO output pin to coil address X | `coil:10` |
| `input-dis:Y` | Discrete Input | Input | Map GPIO input pin to discrete input Y | `input-dis:20` |

**CLI Configuration Example:**
```bash
# Map GPIO25 (output) to Coil #10
set gpio 25 coil:10

# Map GPIO26 (input) to Discrete Input #20
set gpio 26 input-dis:20

save
```

**Modbus Control Example:**
```python
# Set Coil #10 (GPIO25) HIGH
write_coil(10, True)

# Read Discrete Input #20 (GPIO26)
state = read_discrete_input(20)
print(f"GPIO26 state: {state}")
```

**Max Count:**
- Coils: 256 (0-255)
- Discrete Inputs: 256 (0-255)

---

### Timer Output → Coil Mapping

Timer outputs kan også mappes til coils:

```bash
set timer 1 ctrl-reg:50
set timer 1 output-coil:15  # Map timer output to Coil #15
save
```

**Modbus Read:**
```python
# Læs Timer1 output state via Coil #15
state = read_coil(15)
print(f"Timer1 output: {state}")
```

---

## 📋 Register Usage Summary

| Category | Type | Address Range | Count | Fixed/Dynamic |
|----------|------|---------------|-------|---------------|
| **ST Logic Status** | IR | 200-203 | 4 | Fixed |
| **ST Logic Exec Count** | IR | 204-207 | 4 | Fixed |
| **ST Logic Error Count** | IR | 208-211 | 4 | Fixed |
| **ST Logic Error Code** | IR | 212-215 | 4 | Fixed |
| **ST Logic Var Count** | IR | 216-219 | 4 | Fixed |
| **ST Logic Var Values** | IR | 220-251 | 32 | Fixed |
| **ST Logic Min Time** | IR | 252-259 | 8 (4×32-bit) | Fixed |
| **ST Logic Max Time** | IR | 260-267 | 8 (4×32-bit) | Fixed |
| **ST Logic Avg Time** | IR | 268-275 | 8 (4×32-bit) | Fixed |
| **ST Logic Overrun Count** | IR | 276-283 | 8 (4×32-bit) | Fixed |
| **Global Cycle Min** | IR | 284-285 | 2 (32-bit) | Fixed |
| **Global Cycle Max** | IR | 286-287 | 2 (32-bit) | Fixed |
| **Global Cycle Overrun** | IR | 288-289 | 2 (32-bit) | Fixed |
| **Total Cycles** | IR | 290-291 | 2 (32-bit) | Fixed |
| **Exec Interval (RO)** | IR | 292-293 | 2 (32-bit) | Fixed |
| **ST Logic Control** | HR | 200-203 | 4 | Fixed |
| **ST Logic Var Input** | HR | 204-235 | 32 (reserved) | Fixed |
| **Exec Interval (RW)** | HR | 236-237 | 2 (32-bit) | Fixed |
| **Counter Registers** | HR | 0-255 | 0-20 | Dynamic |
| **Timer Registers** | HR | 0-255 | 0-4 | Dynamic |
| **Persistent Registers** | HR | 0-255 | 0-32 | Dynamic |
| **GPIO Coils** | Coil | 0-255 | 0-256 | Dynamic |
| **GPIO Discrete Inputs** | DI | 0-255 | 0-256 | Dynamic |

**Total Fixed Register Usage:**
- Input Registers: 94 (IR 200-293)
- Holding Registers: 38 (HR 200-237)

**Available Dynamic Space:**
- HR 0-199, 238-255: 218 registers for counters/timers/persistent
- Coils/DI: 256 each (but overlap med ST Logic addresser skal undgås)

---

## 🚨 Address Collision Avoidance

**VIGTIGT:** Undgå at mappe dynamiske registre til ST Logic addresser!

### Reserved Address Ranges (DO NOT USE for dynamic mappings)

**Input Registers:**
- ❌ **IR 200-293** - Reserved for ST Logic (læs-kun)

**Holding Registers:**
- ❌ **HR 200-237** - Reserved for ST Logic Control

### Safe Address Ranges for Dynamic Mappings

**Holding Registers (Counter/Timer/Persist):**
- ✅ **HR 0-199** - Safe for dynamic mappings (200 registers)
- ✅ **HR 238-255** - Safe for dynamic mappings (18 registers)

**Coils & Discrete Inputs:**
- ✅ **Coil 0-255** - All available (avoid overlap med GPIO pins)
- ✅ **DI 0-255** - All available (avoid overlap med GPIO pins)

**Best Practice:**
```bash
# GODT: Counters i safe zone
set counter 1 index-reg:10 raw-reg:11 freq-reg:12

# DÅRLIGT: Collision med ST Logic!
set counter 1 index-reg:200  # ❌ Overlaps med ST Logic Status!
```

---

## 📖 CLI Commands Reference

### ST Logic Commands
```bash
# Status
show logic stats              # All programs performance stats
show logic 1 timing           # Detailed timing for Logic1
show logic 1                  # Show Logic1 config

# Control
set logic 1 enabled:true      # Enable Logic1
set logic interval:50         # Set execution interval to 50ms
set logic stats reset all     # Reset all statistics
set logic debug:true          # Enable debug output

# Upload program
set logic 1 upload "VAR x:INT; END_VAR x:=x+1;"

# Variable binding
set logic 1 bind x reg:100    # Bind variable x to HR 100
```

### Counter Commands
```bash
# Configuration
set counter 1 index-reg:10 raw-reg:11 freq-reg:12
set counter 1 enabled:true prescaler:16 scale:10
show counter 1

# Reset via CLI
set counter 1 reset           # Reset counter value
```

### Timer Commands
```bash
# Configuration
set timer 1 ctrl-reg:50
set timer 1 mode:3 t1:1000 t2:500  # Astable blink
set timer 1 enabled:true
show timer 1

# Control
set timer 1 trigger           # Manual trigger (mode 1/2)
```

### GPIO Commands
```bash
# Map GPIO pins
set gpio 25 coil:10           # GPIO25 output → Coil #10
set gpio 26 input-dis:20      # GPIO26 input → DI #20
show gpio
```

### Persistent Register Commands
```bash
# Enable persistence
set persist enable:true

# Define persistent registers
set persist group:0 persist-reg:150=4242
set persist group:1 persist-reg:151=1234

# Save to NVS
save
```

---

## 🔧 Python Modbus Client Examples

### Read ST Logic Statistics
```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient('192.168.1.100')

# Read Logic1 status (IR 200)
status = client.read_input_registers(200, 1).registers[0]
enabled = (status & 0x0001) != 0
compiled = (status & 0x0002) != 0
error = (status & 0x0008) != 0
print(f"Logic1: enabled={enabled}, compiled={compiled}, error={error}")

# Read Logic1 avg execution time (IR 268-269, 32-bit)
high = client.read_input_registers(268, 1).registers[0]
low = client.read_input_registers(269, 1).registers[0]
avg_us = (high << 16) | low
print(f"Logic1 avg execution: {avg_us} µs")

# Read execution interval (IR 292-293, 32-bit)
high = client.read_input_registers(292, 1).registers[0]
low = client.read_input_registers(293, 1).registers[0]
interval = (high << 16) | low
print(f"Execution interval: {interval} ms")
```

### Control ST Logic via Modbus
```python
# Enable Logic1 (HR 200, bit 0)
client.write_register(200, 0x0001)

# Reset Logic2 error (HR 201, bit 2)
client.write_register(201, 0x0004)

# Set execution interval to 50ms (HR 236-237, 32-bit)
client.write_register(236, 0)   # High word
client.write_register(237, 50)  # Low word
```

### Read Counter Values
```python
# Antag Counter1: index-reg=10, freq-reg=12
scaled_value = client.read_holding_registers(10, 1).registers[0]
frequency = client.read_holding_registers(12, 1).registers[0]
print(f"Counter1: value={scaled_value}, freq={frequency} Hz")

# Reset Counter1 via ctrl-reg (bit 3)
client.write_register(104, 0x0008)
```

### Control GPIO via Coils
```python
# Set Coil #10 (GPIO25 output) HIGH
client.write_coil(10, True)

# Read Discrete Input #20 (GPIO26 input)
state = client.read_discrete_inputs(20, 1).bits[0]
print(f"GPIO26 state: {state}")
```

---

## 📚 Related Documentation

- `ST_MONITORING.md` - Performance monitoring og tuning guide
- `TIMING_ANALYSIS.md` - ST Logic timing deep dive
- `CLAUDE.md` - Architecture overview (Layer 5: ST Logic Engine)
- `BUGS.md` - Bug tracking (BUG-001 til BUG-007 fixed)

---

## 🔗 Quick Links

**CLI Help:**
```bash
help                    # General help
show logic stats        # Performance statistics
show counter 1          # Counter 1 status
show timer 1            # Timer 1 status
show gpio               # GPIO mappings
```

**Modbus Function Codes:**
- FC01 (0x01): Read Coils
- FC02 (0x02): Read Discrete Inputs
- FC03 (0x03): Read Holding Registers
- FC04 (0x04): Read Input Registers
- FC05 (0x05): Write Single Coil
- FC06 (0x06): Write Single Register
- FC0F (0x0F): Write Multiple Coils
- FC10 (0x10): Write Multiple Registers

---

**Version:** v4.4.3
**Build:** #834
**Author:** Claude Code
**Last Updated:** 2025-12-29
