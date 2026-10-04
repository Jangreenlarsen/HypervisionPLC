# Logic Block System - High Level Design Document (REVIDERET)

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


**Version:** 0.2 (SEQUENCE-CENTRIC ARCHITECTURE)
**Dato:** 2025-11-30
**Platform:** ESP32-WROOM-32
**Status:** Architecture Update / Brainstorm Feedback

---

## 1. EXECUTIVE SUMMARY

Et modulært logic-system baseret på **sekvenser** som primære eksekverings-enheder. Logic blokke sammensættes ind i sekvenser, og sekvenserne er de aktive programmer som styres fra config-mode.

**Kernebegreber:**
- **Sekvenser** (2-4 stk) - Uafhængige, asynkront kørende automatiserings-programmer
- **Logic Blokke** (16-32 stk) - Komponenter som sammensættes ind i sekvenser
- **Sekvens-state** - Fases, triggers, villkor for sekvensudførelse
- **Config-level Integration** - Sekvenser behandles som moduler på samme niveau som timere/counters
- **Asynkron Eksekvering** - Sekvenser kører parallelt, uden at blokere hinanden

---

## 2. SYSTEM OVERVIEW - SEQUENCE-CENTRIC ARCHITECTURE

```
┌─────────────────────────────────────────────────────────────────┐
│                  LOGIC SEQUENCE ENGINE                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  CONFIG MODULE:                 SEQUENCE RUNTIME:               │
│  ┌────────────────┐             ┌──────────────────────┐        │
│  │ Sequence 1     │             │ Seq1 (Running)       │        │
│  │ - 4 fases      │  ─────→    │ - Current fase: 2    │        │
│  │ - Triggers     │             │ - Elapsed: 1250ms    │        │
│  │ - Variabler    │             │ - Status: EXECUTING  │        │
│  └────────────────┘             └──────────────────────┘        │
│                                                                  │
│  ┌────────────────┐             ┌──────────────────────┐        │
│  │ Sequence 2     │             │ Seq2 (Paused)        │        │
│  │ - 3 faser      │  ─────→    │ - Current fase: 0    │        │
│  │ - Triggers     │             │ - Status: WAITING    │        │
│  │ - Variabler    │             │ - Trigger req: No    │        │
│  └────────────────┘             └──────────────────────┘        │
│                                                                  │
│  CONFIG-LEVEL:                  INPUT/OUTPUT SOURCES:           │
│  ┌────────────────┐             ┌──────────────────────┐        │
│  │ Seq 1 enabled  │             │ Reg:0-159            │        │
│  │ Seq 2 enabled  │  ─────→    │ Coil:0-255           │        │
│  │ Sequence list  │             │ Input:0-255          │        │
│  │ (like timers)  │             │ Timer:1-4 overflow   │        │
│  └────────────────┘             │ Counter:1-4 value    │        │
│                                 └──────────────────────┘        │
│  EXECUTION: Non-blocking, async per sequence                   │
│  Each sequence independent, own state machine                  │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### **Key Architectural Change:**
- **OLD:** 16 independent blocks evaluated each loop
- **NEW:** 2-4 sequences, each containing multiple phases/blocks, running asynchronously
- **Integration:** Sequences appear in `config-mode` as first-class modules (like timers/counters)

---

## 3. USE CASES - SEQUENCE-BASED

### Use Case 1: Simple Threshold Alarm Sequence
```
Scenario: Alarm hvis temperatur > 60°C i mere end 2 sekunder

SEQUENCE 1 (Temp Alarm):
  Fase 0 (IDLE):
    - Trigger: Reg:100 > 600 (temperature check)
    - Block: THRESHOLD input:Reg:100 output:Var:10
    - Next: Fase 1 when Var:10 = 1, else stay Fase 0

  Fase 1 (ALARM_CONFIRMED):
    - Actions on entry: Set Coil:200 = 1
    - Trigger: Timer elapsed 2000ms
    - Next: Fase 2

  Fase 2 (ALARM_ACTIVE):
    - Block: Write Coil:201 = 1
    - Trigger: Reg:100 <= 550 (hysteresis reset)
    - Next: Fase 0

Result: Sequence runs async, maintains state between phases
```

### Use Case 2: Motor Interlocking Sequence
```
Scenario: Start motor kun hvis:
  - Pressure > 100 bar (Reg:110)
  - AND Door closed (Coil:50)
  - AND Emergency stop NOT active (Coil:60)

SEQUENCE 2 (Motor Control):
  Fase 0 (IDLE):
    - Block 1: THRESHOLD input:Reg:110 threshold:1000 output:Var:20
    - Block 2: NOT input:Coil:60 output:Var:21
    - Block 3: AND inputs:[Coil:50, Var:20, Var:21] output:Var:22
    - Trigger: Var:22 = 1 (all conditions met)
    - Next: Fase 1

  Fase 1 (MOTOR_START):
    - Actions on entry: Set Coil:202 = 1 (motor_start)
    - Block: LATCH set:Coil:202 reset:Coil:60 output:Coil:203
    - Trigger: Manual stop or e-stop
    - Next: Fase 0

Result: Interlocking logic protected by sequence phases
```

### Use Case 3: Data Processing Pipeline Sequence
```
Scenario: Process sensor value through 3 stages in phases

SEQUENCE 3 (Sensor Processing):
  Fase 0 (ACQUIRE):
    - Block 1: RATELIMIT input:Coil:60 debounce:50ms output:Var:30
    - Trigger: Elapsed 50ms
    - Next: Fase 1

  Fase 1 (ANALYZE):
    - Block 2: COMPARE input1:Var:30 input2:Var:10 operator:> output:Var:31
    - Block 3: ARITHMETIC input1:Reg:100 input2:Var:30 operator:* output:Reg:101
    - Trigger: Always (no wait)
    - Next: Fase 2

  Fase 2 (OUTPUT):
    - Actions on entry: Write Reg:102 = Var:31
    - Trigger: Manual restart request
    - Next: Fase 0

Result: Pipeline maintains state per phase, processes sequentially
```

### Use Case 4: Sequential Pump Control
```
Scenario: Start pump sequence with delays and state holding

SEQUENCE 4 (Pump Control):
  Fase 0 (STANDBY):
    - Block: AND inputs:[Coil:10, Coil:20] output:Var:40
    - Trigger: Var:40 = 1 (start + water available)
    - Next: Fase 1

  Fase 1 (PRE_PRESSURIZE):
    - Actions on entry: Set Coil:300 = 1 (pressurizing indicator)
    - Trigger: Timer elapsed 500ms
    - Next: Fase 2

  Fase 2 (PUMP_ACTIVE):
    - Actions on entry: Set Coil:202 = 1 (pump on)
    - Block: LATCH set:Coil:202 reset:Coil:11 output:Var:41
    - Trigger: Coil:11 = 1 (stop request)
    - Next: Fase 0

Result: Sequence maintains pump state across phases, stops on request
```

### Use Case 5: Conditional Multi-Phase Processing ✨ **UPDATED**
```
Scenario: Read 3 sensor registers, process through phases, trigger actions if threshold

SEQUENCE 5 (Advanced Processing):
  Fase 0 (CALCULATE):
    - Block 1: ARITHMETIC inputs:[Reg:100, Const:10] operator:* output:Var:50
    - Block 2: ARITHMETIC inputs:[Var:50, Reg:101] operator:+ output:Var:51
    - Trigger: Always (continuous calculation)
    - Next: Fase 1

  Fase 1 (COMPARE):
    - Block 3: COMPARE inputs:[Var:51, Const:500] operator:> output:Var:52
    - Trigger: Var:52 = 1 (threshold exceeded)
    - Next: Fase 2

  Fase 2 (EXECUTE_ACTIONS):
    - Actions on entry:
      1. Set Coil:200 = 1 (Alarm)
      2. Set Reg:102 = 1000 (Log)
      3. Start Timer:1
    - Trigger: Timeout 5000ms
    - Next: Fase 0

Result: Sequence continuously monitors, executes actions on condition
Key Feature: Phases can trigger Modbus commands, not just update variables
```

---

### Use Case 6: Complex Multi-Phase State Machine ✨ **UPDATED**
```
Scenario: Multi-phase process with timer/counter triggers

SEQUENCE 6 (Industrial Process):
  Fase 0 (IDLE):
    - On Entry: Set Coil:100 = 0
    - Trigger: Coil:50 = 1 (start_request)
    - Next: Fase 1

  Fase 1 (PRESSURIZING):
    - On Entry:
      - Set Coil:101 = 1 (pump on)
      - Timer internal countdown: 5000ms
    - Block: Monitor Coil:50 for stop
    - Trigger: Timer:1 overflow OR Coil:50 = 0
    - Next: Fase 2

  Fase 2 (RUNNING):
    - On Entry:
      - Set Coil:102 = 1 (process active)
      - Reset Counter:1 internally
    - Block: Compare Counter:1 against 1000
    - Trigger: Counter:1 reaches threshold
    - Next: Fase 3

  Fase 3 (COOLING):
    - On Entry:
      - Set Coil:101 = 0 (pump off)
      - Timer internal countdown: 10000ms
    - Trigger: Timer:2 overflow
    - Next: Fase 0

  Emergency Exit: Coil:51 (e-stop) → Fase 0 immediately from ANY phase

Result:
  - Complete state machine in single sequence
  - Phases maintain state between transitions
  - Timers/counters trigger phase transitions
  - Each phase executes on-entry actions
  - Emergency conditions override normal flow
```

**Key Features of Sequence-based State Machines:**
- Fases = states with on-entry actions
- Triggers = phase transition conditions (timers, counters, conditions)
- Non-blocking execution (sequence waits, doesn't pause other sequences)
- Emergency exit capabilities per sequence

---

## SEQUENCE-BASED STATE MACHINE CONCEPT

A sequence IS a state machine. Each sequence has 2-8 phases (fases) with:

```c
typedef struct {
  uint8_t id;                      // Sequence 1-4
  uint8_t enabled;                 // 0=disabled, 1=enabled
  char name[32];                   // e.g., "PumpControl", "AlarmSequence"

  uint8_t current_phase;           // Which phase is active (0-7)
  uint8_t phase_count;             // Number of phases (2-8)

  struct {
    char name[32];                 // e.g., "IDLE", "PRESSURIZING"

    // Blocks to execute in this phase
    struct {
      uint8_t block_id;            // Reference to logic block (1-16)
      uint8_t enabled;             // Execute this block?
    } blocks[4];                   // Up to 4 blocks per phase
    uint8_t block_count;

    // On-entry actions (executed once when entering phase)
    struct {
      uint8_t type;                // WRITE_COIL, WRITE_REG, START_TIMER, etc
      uint16_t target_id;          // Coil, Reg, Timer ID
      uint16_t value;              // Value to set
    } actions[8];                  // Up to 8 actions per phase
    uint8_t action_count;

    // Transition trigger to next phase
    struct {
      uint8_t type;                // CONDITION, TIMER, COUNTER, IMMEDIATE
      uint8_t condition_type;      // IF_EQUALS, IF_GREATER, etc
      uint16_t source_id;          // Var, Coil, Reg, Timer ID
      uint16_t threshold;          // Compare value
      uint32_t timeout_ms;         // If timeout-based transition
    } trigger;

    uint8_t next_phase;            // Which phase to go to

    // Emergency exit (can interrupt any phase)
    struct {
      uint8_t enabled;
      uint8_t source_type;         // COIL, VAR, REG
      uint16_t source_id;
      uint16_t exit_value;
      uint8_t goto_phase;          // Phase to jump to on emergency
    } emergency_exit;

  } phases[8];                     // Max 8 phases per sequence

  // Runtime state
  uint32_t phase_start_time;       // When current phase started
  uint32_t phase_elapsed_ms;       // How long in current phase
  uint32_t seq_var[8];             // Local sequence variables

} LogicSequence;

typedef struct {
  LogicSequence sequences[4];      // Up to 4 sequences
  uint32_t global_var[32];         // Shared variables (all sequences)

  // Time sources
  uint32_t time_source;            // Current millis()
  uint8_t tick_1s;
  uint8_t tick_100ms;

} SequenceEngine;
```

**Key Differences from Block-centric:**
- **Sequence = complete program** (all blocks within sequence share local state)
- **Phases = sequential steps** within a sequence
- **Non-blocking execution** (each sequence runs async in own state)
- **On-entry actions** = phase activation side effects
- **Emergency exit** = interrupt capability per sequence

---

## 4. LOGIC BLOCK TYPES (Used within Sequences)

Blocks are components that execute within sequence phases. A phase contains 1-4 blocks and a sequence contains 2-8 phases.

### **Core Blocks (MUST HAVE)**

Used in phase execution for data processing and logic:

| Type | Inputs | Function | Use Case |
|------|--------|----------|----------|
| **AND** | 2-4 | Output = 1 if ALL inputs = 1 | Safety interlocks |
| **OR** | 2-4 | Output = 1 if ANY input = 1 | Alarm combining |
| **NOT** | 1 | Output = NOT input | Negation logic |
| **THRESHOLD** | 1 | Trigger if value > threshold | Alarm levels |
| **COMPARE** | 2 | Compare: ==, !=, <, >, <=, >= | Value comparison |
| **DELAY** | 1 | Delay output by N ms | Phase delays (within timeout) |
| **LATCH** | 2 (set, reset) | Set/reset bistable | State holding within sequence |
| **ARITHMETIC** | 2 | +, -, *, /, min, max, abs | Data processing |

### **Advanced Blocks (PHASE 2)**

| Type | Function | Capability |
|------|----------|-----------|
| **RATELIMIT** | Limit change rate (debounce) | Noise filtering |
| **HYSTERESIS** | Threshold with dead-band | Multi-level alarms |
| **XOR** | Exclusive OR | Logic combination |
| **MUX** | Multiplexer (select input 0-3) | Input selection |
| **LUT** | Lookup table | Complex mappings |

**Note:** SEQUENCE block no longer needed - sequences ARE the programs now. On-entry actions handle command execution.

---

## 5. INPUT/OUTPUT SOURCES (Sequence-level)

### **Input Sources (What can trigger phases or feed blocks?)**
```
REG:<0-159>              - Holding register value
COIL:<0-255>             - Coil state
INPUT:<0-255>            - Discrete input
VAR:<0-31>               - Global variable (all sequences)
SEQVAR:<0-7>             - Sequence-local variable (per sequence)
TIMER:<1-4>              - Timer status (overflow, elapsed)
COUNTER:<1-4>            - Counter value/threshold
BLOCK_OUTPUT:<1-16>      - Output from block within phase
CONST:<value>            - Constant (hardcoded)
```

### **Phase Trigger Types**
```
CONDITION     - Compare input against value (==, !=, <, >, <=, >=)
TIMER         - Wait for phase timeout (internal timer)
COUNTER       - Compare counter value
IMMEDIATE     - Move to next phase without waiting
```

### **On-Entry Action Types (Phase activation)**
```
WRITE_COIL    - Set coil to value
WRITE_REG     - Set register to value
SET_VAR       - Set global variable
SET_SEQVAR    - Set sequence-local variable
START_TIMER   - Start a timer (1-4)
STOP_TIMER    - Stop a timer
RESET_COUNTER - Reset a counter (1-4)
```

### **Block Output Destinations**
```
SEQVAR:<0-7>         - Local sequence variable
VAR:<0-31>           - Global variable
COIL:<0-255>         - Coil (for visualization only)
REG:<0-159>          - Register (for logging)
```

---

## 6. DATA STRUCTURES (Sequence-Centric)

### **Logic Block Configuration (Used within phases)**
```c
typedef struct {
  uint8_t id;                          // 1-16 (global block ID)
  uint8_t enabled;                     // 0=disabled, 1=enabled
  BlockType type;                      // AND, OR, THRESHOLD, etc

  // Input configuration (up to 4 inputs)
  struct {
    uint8_t source_type;               // REG|COIL|INPUT|VAR|SEQVAR|TIMER|COUNTER|CONST
    uint16_t source_id;                // Address or ID
    uint16_t const_value;              // If CONST type
  } inputs[4];
  uint8_t input_count;

  // Output configuration (up to 2 outputs)
  struct {
    uint8_t dest_type;                 // SEQVAR|VAR|COIL|REG
    uint16_t dest_id;
  } outputs[2];
  uint8_t output_count;

  // Block-specific parameters
  union {
    struct { uint16_t threshold; uint16_t hysteresis; uint8_t logic; } threshold;
    struct { uint32_t delay_ms; uint32_t start_time; } delay;
    struct { uint8_t operator; } compare;
    struct { uint8_t operator; } arithmetic;
    struct { uint8_t logic; } latch;
    struct { uint32_t debounce_ms; uint32_t last_change_time; uint32_t last_value; } ratelimit;
  } params;

  // Runtime
  uint32_t last_output;
  uint32_t state;

} LogicBlock;

### **LogicSequence Configuration**
```c
// (Already defined in section 3 - SEQUENCE-BASED STATE MACHINE CONCEPT)
// Copied here for reference:
//
// LogicSequence: id, enabled, name, current_phase, phase_count
//   ├─ phases[0-7]:
//   │  ├─ name
//   │  ├─ blocks[0-3] (references to LogicBlock IDs)
//   │  ├─ actions[0-7] (WRITE_COIL, WRITE_REG, START_TIMER, etc)
//   │  ├─ trigger (CONDITION, TIMER, COUNTER, IMMEDIATE)
//   │  ├─ next_phase
//   │  └─ emergency_exit
//   ├─ phase_start_time
//   ├─ phase_elapsed_ms
//   └─ seq_var[0-7] (local sequence variables)
```

### **Sequence Engine (Main Runtime)**
```c
typedef struct {
  LogicBlock blocks[16];               // All blocks 1-16 (used by all sequences)
  LogicSequence sequences[4];          // Sequences 1-4

  uint32_t global_var[32];             // Shared variables across all sequences
  uint8_t var_changed[32];             // Change flags

  // Time sources
  uint32_t time_source;                // Current millis()
  uint8_t tick_1s;
  uint8_t tick_100ms;

  // Runtime state
  uint8_t active_sequence_count;       // How many sequences enabled

} SequenceEngine;
```

**Key Differences from Block-Centric:**
- Sequences are PRIMARY execution units (not blocks)
- Blocks are subordinate to sequences
- Each sequence has local variables + emergency exit
- Phases enable on-entry actions
- Sequences run asynchronously

---

## 7. GLOBAL CLI ARCHITECTURE (4 MODES - With SEQ Mode)

### **CLI Mode Overview**

```
┌─────────────────────────────────────────────────────────────────────┐
│                       GLOBAL CLI SYSTEM                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  MONITORING (>)        CONFIG (.)         SEQ MODE (#)  SYSTEM ($)  │
│  ┌──────────────┐      ┌──────────────┐  ┌──────────┐  ┌────────┐ │
│  │ show config  │      │ set timer    │  │ set seq  │  │ save   │ │
│  │ show timers  │      │ set counter  │  │ set var  │  │ load   │ │
│  │ show counters│      │ set gpio     │  │ show seq │  │ reset  │ │
│  │ show logic   │      │ set register │  │ show vars│  │ reboot │ │
│  │ show seqs    │      │ set baudrate │  │ set phase│  │ version│ │
│  │ read h-reg     │      │ delete timer │  │ delete   │  │ help   │ │
│  │ read coil    │      │ load defaults│  │ verify   │  │ exit   │ │
│  │ monitor      │      └──────────────┘  │ show eval│  └────────┘ │
│  │ config-mode ─┼────────────────────┐   └──────────┘              │
│  │ seq-mode ────┼────────────────────┼─────────┐                  │
│  │ system-mode  │                    │         │                  │
│  └──────────────┘                    │         │                  │
│   ^ prev                             │         │                  │
│   │                                  │         │                  │
│   └──────────────────────────────────┴─────────┘                  │
│                                                                      │
│  Default Mode: MONITORING (>)                                       │
│  Shared: help, exit (to previous mode), version                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**KEY CHANGE:** CONFIG MODE now includes sequence management. Sequences are first-class configuration modules like timers and counters.

---

### **MONITORING MODE (>)** - Default, Read-Only

**Purpose:** View system state, monitor values, diagnostics

```bash
> show config              # Full configuration summary
> show timers              # All timers status
> show counters            # All counters status
> show seqs                # List all sequences (brief)
> show seq 1               # Sequence 1 details (phase, status, vars)
> show seq 1 phase 2       # Sequence 1, Phase 2 details
> read h-reg 0 10            # Read registers 0-10
> read coil 0 16           # Read coils 0-16
> monitor                  # Live update mode (all values)
> help
> config-mode              # Enter config mode
> seq-mode                 # Enter sequence configuration mode
> system-mode              # Enter system mode
> exit                     # Exit app
```

---

### **CONFIG MODE (.)** - Configuration, Hardware + Sequences

**Purpose:** Configure Modbus, timers, counters, GPIO, sequences

```bash
# Hardware Configuration (existing)
. set timer 1 mode 3 on-dur:500 off-dur:500 output-coil:100
. set counter 1 mode:hw input-dis:16 index-reg:0 prescaler:1
. set gpio 140 static map input:10
. set register 0 static value 1000
. set coil 200 static value 1
. set baudrate 115200
. set slave-id 1

# Sequence Configuration (NEW - but managed from SEQ MODE)
. set seq 1 enabled:1              # Enable/disable sequence
. set seq 1 name "PumpControl"     # Name a sequence
. delete seq 1                     # Delete sequence (done in SEQ MODE)

# Shared
. delete timer 1
. delete counter 2
. load defaults                    # Load factory defaults
. help
. exit                             # Back to monitoring (>)
. save                             # Save to system
. load                             # Load from system

Note: Detailed sequence programming (phases, blocks, triggers)
      is done in SEQ MODE (#), not CONFIG MODE.
```

---

### **SEQ MODE (#)** - Sequence Programming (Phases & Blocks)

**Purpose:** Program sequences, phases, logic blocks, and automation

```bash
# Enter sequence mode from monitoring
> seq-mode
Entering Sequence Programming Mode...

# Create/configure a sequence
# set seq 1 name "PumpControl"      # Name sequence 1
# set seq 1 phase-count 3            # Sequence 1 has 3 phases

# Configure phases within sequence
# set seq 1 phase 0 name "IDLE"
# set seq 1 phase 0 timeout 0        # No timeout (wait for trigger)
# set seq 1 phase 0 trigger coil:50 = 1   # Trigger on Coil 50 = 1
# set seq 1 phase 0 next-phase 1     # Go to phase 1

# Add blocks to phase
# set seq 1 phase 0 block 1          # Add Block 1 to phase 0
# set seq 1 phase 0 block 2          # Add Block 2 to phase 0

# Add on-entry actions to phase
# set seq 1 phase 0 action write-coil 100 1      # Action: Set Coil 100 = 1
# set seq 1 phase 0 action start-timer 1         # Action: Start Timer 1

# Configure sequence blocks
# set block 1 type:and input:coil:50 input2:coil:60 output:seqvar:0
# set block 2 type:compare input:seqvar:0 input2:const:1 operator:== output:seqvar:1

# Set global variables
# set var 10 1000

# Show commands
# show seqs                  # All sequences
# show seq 1                 # Sequence 1 full config
# show seq 1 phase 0         # Phase 0 of sequence 1
# show blocks                # All blocks
# show vars                  # All global variables
# show eval-order            # Block evaluation order

# Management
# delete seq 1               # Delete sequence 1
# delete block 1             # Delete block 1
# verify                     # Check for issues
# help
# exit                       # Back to monitoring (>)
```

**New Concepts:**
- Sequences = complete programs
- Phases = sequential steps within program
- Each phase has blocks + on-entry actions + transition trigger
- Blocks are reusable components (can be in multiple sequences)
- Global variables (VAR) + Sequence variables (SEQVAR)

---

### **SYSTEM MODE ($)** - Persistence, System Operations

**Purpose:** Save/load configuration, system control, metadata

```bash
$ save              # Save entire config (timers + counters + sequences + logic + GPIO)
$ load              # Load config from NVS
$ reset             # Reset config to factory defaults
$ reboot            # Reboot ESP32
$ version           # Show firmware version
$ help
$ exit              # Back to previous mode (usually monitoring)
```

---

### **Mode Transitions (Visual)**

```
Start
  │
  ├─→ MONITORING MODE (>) [Default]
  │   ├─→ "config-mode" → CONFIG MODE (.)
  │   │                    └─→ "exit" → MONITORING (>)
  │   │
  │   ├─→ "seq-mode" → SEQ MODE (#)          ← NEW (was logic-mode)
  │   │                 └─→ "exit" → MONITORING (>)
  │   │
  │   ├─→ "system-mode" → SYSTEM MODE ($)
  │   │                    └─→ "exit" → MONITORING (>)
  │   │
  │   └─→ "exit" → Shutdown
  │
  └─→ (Any mode) "save" or "load" → SYSTEM MODE ($) → (back to previous mode)
```

**Key Changes:**
- LOGIC MODE (#) renamed to SEQ MODE (#)
- CONFIG MODE now aware of sequences
- Sequences persist via save/load/reset in SYSTEM MODE

---

### **CLI Command Distribution (Updated for Sequences)**

| Command | > Monitor | . Config | # SEQ | $ System |
|---------|-----------|----------|-------|----------|
| show config | ✅ | - | - | - |
| show timers | ✅ | - | - | - |
| show counters | ✅ | - | - | - |
| show seqs | ✅ | - | ✅ | - |
| show seq N | ✅ | - | ✅ | - |
| read reg/coil | ✅ | - | - | - |
| monitor | ✅ | - | - | - |
| set timer | - | ✅ | - | - |
| set counter | - | ✅ | - | - |
| set seq | - | ✅ | ✅ | - |
| set gpio | - | ✅ | - | - |
| set register | - | ✅ | - | - |
| set coil | - | ✅ | - | - |
| set baudrate | - | ✅ | - | - |
| set phase | - | - | ✅ | - |
| set block | - | - | ✅ | - |
| set var | - | - | ✅ | - |
| delete seq | - | - | ✅ | - |
| delete timer | - | ✅ | - | - |
| delete block | - | - | ✅ | - |
| verify | - | - | ✅ | - |
| save | - | - | - | ✅ |
| load | - | - | - | ✅ |
| reboot | - | - | - | ✅ |
| version | ✅ | ✅ | ✅ | ✅ |
| help | ✅ | ✅ | ✅ | ✅ |
| exit | ✅ | ✅ | ✅ | ✅ |

---

---

---

## 9. IMPLEMENTATION STRATEGY (Sequence-Centric)

### **Phase 1: Core (MVP)**
- [ ] LogicSequence structure + LogicBlock structure
- [ ] Sequence storage (up to 4 sequences per config)
- [ ] Phase structure with blocks, actions, triggers
- [ ] AND, OR, NOT, THRESHOLD, COMPARE blocks
- [ ] Input source reading (REG, COIL, VAR, SEQVAR, CONST)
- [ ] Output sink writing (SEQVAR, VAR, COIL, REG)
- [ ] Phase transition logic (CONDITION, TIMER, IMMEDIATE triggers)
- [ ] On-entry actions execution (WRITE_COIL, WRITE_REG, START_TIMER)
- [ ] CLI mode infrastructure (> . # $ prompts)
- [ ] SEQ MODE commands (set seq, set phase, set block, show seqs)
- [ ] Main loop integration (evaluate all active sequences)

### **Phase 2: Enhanced**
- [ ] DELAY, LATCH, ARITHMETIC blocks
- [ ] RATELIMIT, HYSTERESIS blocks
- [ ] Emergency exit per sequence
- [ ] Sequence-local variables (SEQVAR)
- [ ] NVS persistence (save/load sequences)
- [ ] Verify command (check sequence integrity, circular deps)
- [ ] CONFIG MODE awareness of sequences

### **Phase 3: Advanced (Future)**
- [ ] Timer integration (sequences can read timer status)
- [ ] Counter integration (sequences can read counter values)
- [ ] Advanced triggers (OR combinations, edge detection)
- [ ] XOR, MUX, LUT blocks
- [ ] Multiple sequence simultaneous execution testing
- [ ] Performance optimization

---

## 10. OPEN QUESTIONS FOR BRAINSTORM (Sequence-Centric)

### **Sequence Architecture**
- [ ] Is 4 sequences + 2-8 phases per sequence sufficient?
- [ ] Should phases evaluate blocks in parallel or sequential order?
- [ ] Can phases transition while previous actions are still executing?
- [ ] Should sequence-local variables (SEQVAR) persist between phase transitions?

### **Phase Triggers**
- [ ] Should triggers support OR logic (multiple conditions)?
- [ ] Do we need edge-detection triggers (rising/falling)?
- [ ] Should timeout per phase be per-phase configurable?
- [ ] Can we have multiple triggers to different next-phases?

### **On-Entry Actions**
- [ ] Max 8 actions per phase - is that enough?
- [ ] Should actions execute atomically or can be interrupted?
- [ ] Do we need "on-exit" actions when leaving phase?
- [ ] Can actions reference block outputs directly?

### **Functionality & Integration**
- [ ] Should sequences be able to trigger other sequences?
- [ ] Can sequences access timer/counter values in blocks?
- [ ] Should we support sequence pause/resume from outside?
- [ ] Do we need sequence debugging/step-through mode?

### **CLI/UX**
- [ ] Is the SEQ MODE syntax intuitive?
- [ ] Should we have sequence templates/examples?
- [ ] Need better visualization of phase flow?
- [ ] Should sequences have names + comments?

### **Performance**
- [ ] How many sequences can run simultaneously?
- [ ] Can all 4 sequences + 8 phases + 4 blocks evaluate in <10ms?
- [ ] Should we limit blocks per phase to 2-3 instead of 4?

### **Emergency Exit**
- [ ] Per-sequence emergency exit - is one enough?
- [ ] Can emergency exit go to any phase or only phase 0?
- [ ] Should emergency override be disableable per sequence?

---

## 11. RESOURCE ESTIMATES (Sequence-Based)

### **Memory (Approximate)**
```
LogicBlock struct:              ~100 bytes each
16 blocks:                      ~1.6 KB

LogicSequence struct:           ~500 bytes each
  ├─ 8 phases × ~120 bytes:     ~960 bytes/seq
  ├─ 8 local vars × 4:          ~32 bytes/seq
4 sequences:                    ~2.0 KB

Global variables (32 × 4):      ~128 bytes
Sequence Engine struct:         ~300 bytes
───────────────────────────────
TOTAL:                          ~4.0 KB (well within 520KB RAM)
```

### **Performance (Per loop iteration)**
```
Evaluate 1 sequence:
  ├─ Read phase trigger inputs:  ~50 μs
  ├─ Execute on-entry actions:   ~50 μs
  ├─ Execute 1-4 blocks:         ~100-200 μs
  ├─ Check phase transition:     ~20 μs
  Total per sequence:            ~220-320 μs

All 4 sequences:                 ~880-1280 μs per loop
@ 1000 Hz loop (1ms):            0.88-1.28% CPU usage
```

**Note:** Non-blocking execution means sequences pause while waiting for triggers, using minimal CPU.

---

## 12. FILE STRUCTURE (Sequence-Based)

```
src/
  # CLI Infrastructure
  cli_shell.cpp/h           - Main CLI shell with mode handling (>, ., #, $)
  cli_monitoring.cpp/h      - Monitoring mode (show seqs, show config, read, monitor)
  cli_config.cpp/h          - Config mode (set timer, counter, gpio, seq enable/name)
  cli_seq.cpp/h             - SEQ mode (set seq, set phase, set block, set var, show)

  # Sequence Engine Core
  sequence_engine.cpp/h     - Main runtime (eval all sequences, manage phases)
  sequence_phase.cpp/h      - Phase execution (blocks, actions, triggers)
  sequence_trigger.cpp/h    - Phase trigger evaluation (CONDITION, TIMER, etc)
  sequence_action.cpp/h     - On-entry action execution

  # Logic Blocks (used within phases)
  logic_block.cpp/h         - Block struct, types, config
  logic_evaluator.cpp/h     - Block evaluation (AND, OR, NOT, THRESHOLD, COMPARE, etc)
  logic_source.cpp/h        - Read inputs (REG, COIL, VAR, SEQVAR, CONST, TIMER, COUNTER)
  logic_sink.cpp/h          - Write outputs (SEQVAR, VAR, COIL, REG)

  # Persistence
  sequence_config.cpp/h     - Load/save sequences to NVS
  sequence_migration.cpp/h  - Schema migration if needed

include/
  sequence_engine.h         - Public API (SequenceEngine, LogicSequence, LogicBlock)
  cli_modes.h               - Mode definitions and state
  types.h                   - (updated with SequenceEngine, LogicSequence types)
  constants.h               - (updated with sequence constants)
```

---

## 13. INTEGRATION POINTS

### **In main loop:**
```cpp
void loop() {
  // ... existing Modbus, timers, counters ...

  // Update time sources for sequence engine
  sequence_engine_update_time();

  // Evaluate all active sequences (non-blocking)
  sequence_engine_evaluate_all();

  // Sequences update coils, registers, start timers as needed

  // ... rest of loop ...
}
```

**Key Point:** Sequences are non-blocking. They update state, execute actions, and transition phases within the loop iteration without blocking. Multiple sequences run concurrently.

### **With existing systems:**
- **Registers:** Sequences read/write holding/input registers
- **Coils:** Sequences read/write coil array
- **Timers:** Sequences can read timer status and start/stop timers (Phase 1)
- **Counters:** Sequences can read counter values (Phase 2)
- **GPIO:** Sequences can use GPIO mappings as inputs (Phase 3)

### **CLI Integration:**
- **cli_shell.cpp:** Dispatches to correct mode handler based on current prompt
- **Monitoring mode (>):** show seqs, show seq N, show seq N phase M
- **Config mode (.):** set seq enabled/name (basic sequence control)
- **SEQ mode (#):** set seq, set phase, set block, set var, delete, show, verify
- **System mode ($):** Save/load/reboot (persists all sequences)

### **Configuration Persistence:**
- Sequences saved in NVS alongside timers, counters, registers
- Load/reset commands include sequence configuration
- Schema versioning for backward compatibility

---

## 14. SUCCESS CRITERIA (Sequence-Centric)

### **Phase 1 (MVP):**
- [ ] CLI mode system working (4 prompts: > . # $)
- [ ] LogicSequence and LogicBlock structures defined in types.h
- [ ] SequenceEngine with 4 sequences max, 2-8 phases per sequence
- [ ] Phase structure with blocks, actions, triggers
- [ ] AND, OR, NOT, THRESHOLD, COMPARE blocks implemented and working
- [ ] Input source reading (REG, COIL, VAR, SEQVAR, CONST)
- [ ] Output sink writing (SEQVAR, VAR, COIL, REG)
- [ ] Phase transition logic (CONDITION, TIMER, IMMEDIATE triggers)
- [ ] On-entry actions execution (WRITE_COIL, WRITE_REG, START_TIMER)
- [ ] SEQ MODE commands (set seq, set phase, set block, show seqs)
- [ ] All sequences + phases + blocks evaluate in < 2ms total per loop
- [ ] Configuration persists across reboot (save/load in system mode)
- [ ] Multiple sequences run asynchronously without blocking each other
- [ ] Emergency exit per sequence working

### **Phase 2:**
- [ ] DELAY, LATCH, ARITHMETIC blocks
- [ ] Sequence-local variables (SEQVAR, 8 per sequence)
- [ ] Global variables (VAR, 32 shared across all sequences)
- [ ] Verify command detects circular dependencies
- [ ] CONFIG MODE aware of sequences (set seq enabled, name)
- [ ] NVS persistence including sequence schema version

### **Phase 3:**
- [ ] Timer/counter integration (sequences can read status)
- [ ] Advanced triggers (OR combinations, edge detection)
- [ ] Multiple simultaneous sequences stress tested
- [ ] Performance benchmark < 1% CPU at 1000Hz loop

---

## 15. NEXT STEPS (Sequence-Centric Implementation)

1. **Update include/types.h:**
   - Add LogicSequence struct (id, enabled, name, current_phase, phases[], seq_var[])
   - Add LogicPhase struct (name, blocks[], actions[], trigger, next_phase)
   - Add LogicBlock struct (id, type, inputs[], outputs[], params)
   - Add LogicAction struct (type, target_id, value)
   - Add LogicTrigger struct (type, condition_type, source_id, threshold, timeout_ms)
   - Add SequenceEngine struct (sequences[], blocks[], global_var[])

2. **Update include/constants.h:**
   - Add SEQUENCE_MAX (4)
   - Add PHASE_MAX (8)
   - Add BLOCKS_PER_PHASE (4)
   - Add ACTIONS_PER_PHASE (8)
   - Add SEQVAR_PER_SEQ (8)
   - Add block type enums
   - Add trigger type enums
   - Add action type enums

3. **Create sequence_engine.h:**
   - Public API for SequenceEngine
   - Functions: init, evaluate_all, update_time, etc

4. **Implement Phase 1 core modules:**
   - src/sequence_engine.cpp/h - Main runtime
   - src/sequence_phase.cpp/h - Phase execution
   - src/sequence_trigger.cpp/h - Trigger evaluation
   - src/sequence_action.cpp/h - Action execution
   - src/logic_evaluator.cpp/h - Block types (AND, OR, NOT, THRESHOLD, COMPARE)
   - src/logic_source.cpp/h - Input reading
   - src/logic_sink.cpp/h - Output writing

5. **Update CLI architecture:**
   - Rename cli_logic.cpp → cli_seq.cpp
   - Update cli_shell.cpp for mode "seq-mode"
   - Update cli_config.cpp for sequence enable/name commands

6. **Integration testing:**
   - Test each use case with real Modbus scenarios
   - Verify async execution (multiple sequences running)
   - Verify persistence (save/load/reset)

7. **Documentation:**
   - Create SEQ MODE user guide with examples
   - Document sequence best practices
   - Add troubleshooting guide

---

## SUMMARY OF CHANGES FROM BLOCK-CENTRIC TO SEQUENCE-CENTRIC

| Aspect | OLD (Block-Centric) | NEW (Sequence-Centric) |
|--------|-------------------|----------------------|
| **Execution Unit** | Individual blocks in topological order | Sequences with phases |
| **State Management** | Blocks process each loop | Sequences maintain phase state |
| **Control Flow** | No sequence, just dependency graph | Explicit phases with triggers |
| **Actions** | Blocks only update variables/coils | Phases have on-entry Modbus actions |
| **Async Capability** | N/A (blocks evaluated same loop) | Full async - sequences wait at triggers |
| **Scope** | Global variables shared | Global + local sequence variables |
| **CLI Mode** | logic-mode (#) for blocks | seq-mode (#) for sequences |
| **Integration** | Logic blocks as separate subsystem | Sequences as first-class config module |

**Result:** Simpler to understand (phases = steps in a program), better control flow (explicit state machine), true async execution (sequences run independently), integrated with config system.

---

**END OF REVISED HLD DOCUMENT (v0.2)**
**Status:** Ready for implementation brainstorm + feedback

---

### **How to Use This Document:**

1. **Read the use cases** - Do they match your needs?
2. **Comment on block types** - Are these enough? Need more?
3. **Flag concerns** - Circular dependencies? Performance?
4. **Suggest improvements** - Better CLI syntax? Different structure?
5. **Approve when ready** - Then we code Phase 1!
