# ESP32 Projekt: Modular Arkitektur

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


## Problemanalyse fra Mega2560 projekt

### Nuværende problemer (Mega2560)
- **modbus_core.cpp** (500+ linjer): Frame RX, parsing, CRC, TX blandet
- **modbus_counters.cpp** (400+ linjer): SW polling, HW mode, prescaler, frequency i samme fil
- **modbus_counters_sw_int.cpp**: ISR handlers blandet med config logic
- **modbus_counters_hw.cpp**: Timer5 ISR, external clock logic, prescaler
- **cli_shell.cpp** (600+ linjer): Alle CLI commands i én fil uden struktur
- **config_store.cpp**: EEPROM + config application blandet

**Konsekvens:** Debug af ét område påvirker alt andet → bugs introduceres

---

## Løsning: Funktions-baseret modularisering

Vi deler projektet op så hver .cpp/.h fil har **EN klar ansvar**.

### Princip:
- Hver modul: 100-250 linjer (bevidst, manageable)
- Klare interface via .h filer
- Minimal cross-dependency
- Testbar i isolation

---

## Foreslået filstruktur (ESP32 projekt)

```
src/
├── main.cpp                          (Only: setup/loop, subsystem calls)
│
├── [LAYER 0: Hardware Abstraction]
├── gpio_driver.cpp / .h              (GPIO init, read/write, interrupt setup)
├── uart_driver.cpp / .h              (UART0/UART1 init, RX/TX, ISR)
├── pcnt_driver.cpp / .h              (PCNT unit config, counting, ISR)
├── nvs_driver.cpp / .h               (NVS read/write, EEPROM emulation)
│
├── [LAYER 1: Protocol Core]
├── modbus_frame.cpp / .h             (Frame struct, CRC16, validation ONLY)
├── modbus_parser.cpp / .h            (Parse frame → function code, data)
├── modbus_serializer.cpp / .h        (Build response frame, CRC)
│
├── [LAYER 2: Function Code Handlers]
├── modbus_fc_read.cpp / .h           (FC01-04 read implementations)
├── modbus_fc_write.cpp / .h          (FC05-06, 0F-10 write implementations)
├── modbus_fc_dispatch.cpp / .h       (Router: FC → handler)
│
├── [LAYER 3: Modbus Server Runtime]
├── modbus_rx.cpp / .h                (Serial RX, frame detection, timeout)
├── modbus_tx.cpp / .h                (RS-485 DIR control, serial TX)
├── modbus_server.cpp / .h            (Main Modbus state machine)
│
├── [LAYER 4: Register/Coil Storage]
├── registers.cpp / .h                (Holding/input register array, access functions)
├── coils.cpp / .h                    (Coil/discrete input array, access functions)
├── gpio_mapping.cpp / .h             (GPIO ↔ coil/input bindings)
│
├── [LAYER 5: Feature Engines]
├── counter_config.cpp / .h           (Counter struct, defaults, validation)
├── counter_sw.cpp / .h               (SW polling mode ONLY)
├── counter_sw_isr.cpp / .h           (SW-ISR mode ONLY)
├── counter_hw.cpp / .h               (HW PCNT mode ONLY)
├── counter_frequency.cpp / .h        (Frequency measurement logic)
├── counter_engine.cpp / .h           (Counter state machine, prescaler division)
│
├── timer_config.cpp / .h             (Timer struct, defaults, validation)
├── timer_engine.cpp / .h             (Timer state machines, mode 1-4)
│
├── [LAYER 6: Persistence]
├── config_struct.cpp / .h            (PersistConfig struct definition ONLY)
├── config_load.cpp / .h              (Load from NVS, migration, validation)
├── config_save.cpp / .h              (Save to NVS, CRC)
├── config_apply.cpp / .h             (Apply config to engines)
│
├── [LAYER 7: CLI & User Interface]
├── cli_parser.cpp / .h               (Tokenize, command dispatch)
├── cli_commands.cpp / .h             (All 'set' command implementations)
├── cli_show.cpp / .h                 (All 'show' command implementations)
├── cli_history.cpp / .h              (Command history, arrow keys)
├── cli_shell.cpp / .h                (Serial I/O, state machine)
│
├── [LAYER 8: System]
├── heartbeat.cpp / .h                (LED blink, watchdog)
├── version.cpp / .h                  (Version string, changelog)
└── debug.cpp / .h                    (Debug output, printf helpers)

include/
├── constants.h                       (All #define, enums - SINGLE FILE)
├── types.h                           (All struct definitions - SINGLE FILE)
└── config.h                          (Build configuration)
```

---

## Design Patterns & Interfaces

### Pattern 1: Hardware Drivers (Abstraction Layer 0)

**gpio_driver.h:**
```c
#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

typedef int gpio_pin_t;
typedef enum { GPIO_INPUT, GPIO_OUTPUT } gpio_mode_t;
typedef enum { GPIO_RISING, GPIO_FALLING, GPIO_BOTH } gpio_edge_t;
typedef void (*gpio_isr_handler_t)(void* arg);

void gpio_init(gpio_pin_t pin, gpio_mode_t mode);
void gpio_set(gpio_pin_t pin, int level);
int gpio_get(gpio_pin_t pin);
void gpio_interrupt_attach(gpio_pin_t pin, gpio_edge_t edge, gpio_isr_handler_t handler);
void gpio_interrupt_detach(gpio_pin_t pin);

#endif
```

**Benefit:** If we later port to another platform (RISC-V, STM32), just replace gpio_driver.cpp

---

### Pattern 2: Protocol Layers (OSI-style separation)

**modbus_frame.h:**
```c
#define MODBUS_FRAME_MAX_SIZE 256

typedef struct {
  uint8_t slave_id;
  uint8_t function_code;
  uint8_t data[MODBUS_FRAME_MAX_SIZE - 4];  // payload
  uint16_t data_len;
  uint16_t crc;
} ModbusFrame;

// ONLY CRC and frame struct helpers
uint16_t modbus_crc16(const uint8_t* data, uint16_t len);
bool modbus_frame_validate(const ModbusFrame* frame);
void modbus_crc_set(ModbusFrame* frame);
```

**modbus_parser.h:**
```c
typedef struct {
  uint16_t starting_address;
  uint16_t quantity;
} ReadRequest;

// ONLY parsing logic
bool modbus_parse_read_request(const ModbusFrame* frame, ReadRequest* req);
// ... other parsers
```

**modbus_serializer.h:**
```c
// ONLY serialization logic
bool modbus_serialize_read_response(ModbusFrame* frame, const uint16_t* registers, uint16_t count);
// ... other serializers
```

**Benefit:** Can unit-test frame parsing without full Modbus server running

---

### Pattern 3: Feature Engines (Clear ownership)

**counter_config.h:**
```c
typedef enum { COUNTER_SW, COUNTER_SW_ISR, COUNTER_HW } counter_mode_t;

typedef struct {
  uint8_t enabled;
  counter_mode_t mode;
  uint8_t edge_type;
  uint16_t prescaler;
  uint8_t bit_width;
  int8_t direction;
  float scale;
  uint16_t index_reg;
  uint16_t raw_reg;
  uint16_t freq_reg;
  uint16_t overload_reg;
  uint16_t ctrl_reg;
  // ... mode-specific fields
} CounterConfig;

// ONLY validation and defaults
CounterConfig counter_config_defaults(void);
bool counter_config_validate(const CounterConfig* cfg);
```

**counter_sw.cpp/h:**
```c
typedef struct {
  uint32_t counter_value;
  uint32_t last_input_level;
} CounterSWState;

void counter_sw_init(uint8_t counter_id, const CounterConfig* cfg);
void counter_sw_loop(uint8_t counter_id);  // Called every loop()
void counter_sw_reset(uint8_t counter_id);
uint32_t counter_sw_get_value(uint8_t counter_id);
```

**counter_hw.cpp/h:**
```c
typedef struct {
  pcnt_unit_t pcnt_unit;
  uint32_t last_count;
  uint32_t prescaler_accumulator;
} CounterHWState;

void counter_hw_init(uint8_t counter_id, const CounterConfig* cfg);
void counter_hw_loop(uint8_t counter_id);  // Frequency measurement only
void counter_hw_reset(uint8_t counter_id);
uint32_t counter_hw_get_value(uint8_t counter_id);
void counter_hw_isr(pcnt_unit_t unit, pcnt_channel_t channel);
```

**counter_engine.cpp/h:**
```c
// HIGH-LEVEL orchestration
void counter_engine_init(const PersistConfig* config);
void counter_engine_loop(void);  // Call every main loop iteration
void counter_engine_set_mode(uint8_t counter_id, counter_mode_t mode);
uint32_t counter_engine_get_value(uint8_t counter_id);  // Route to right backend
```

**Benefit:**
- Can disable SW mode without touching HW mode code
- Can rewrite PCNT implementation without affecting prescaler logic
- Easy to test each mode in isolation

---

### Pattern 4: CLI Command Organization

**cli_commands.h:**
```c
typedef struct {
  const char* keyword;
  int (*handler)(int argc, char* argv[]);
  const char* help_text;
} CLICommand;

// Handlers return: 0=success, <0=error
int cli_cmd_set_counter(int argc, char* argv[]);
int cli_cmd_set_timer(int argc, char* argv[]);
int cli_cmd_set_network(int argc, char* argv[]);
// ... one function per major 'set' command
```

**cli_show.h:**
```c
int cli_cmd_show_config(void);
int cli_cmd_show_counters(void);
int cli_cmd_show_timers(void);
// ... one function per 'show' command
```

**cli_parser.cpp:**
```c
// Tokenize input, dispatch to command handler
void cli_parser_execute(const char* input);
```

**Benefit:** Adding new CLI command = add one function, no spaghetti logic

---

### Pattern 5: Configuration (Single Source of Truth)

**config_struct.h:**
```c
typedef struct {
  uint8_t slave_id;
  uint32_t baudrate;
  CounterConfig counters[4];
  TimerConfig timers[4];
  // ... all persistent config
  uint16_t crc;
  uint8_t schema_version;
} PersistConfig;
```

**config_load.cpp:**
```c
// ONLY reading from NVS
// Migration logic (v1→v2, etc.)
bool config_load_from_nvs(PersistConfig* cfg);
```

**config_save.cpp:**
```c
// ONLY writing to NVS
// CRC calculation
bool config_save_to_nvs(const PersistConfig* cfg);
```

**config_apply.cpp:**
```c
// ONLY applying config to engines
// Initialize counters, timers, GPIO mappings based on config
bool config_apply_to_system(const PersistConfig* cfg);
```

**Benefit:** If NVS crashes, only config_load.cpp to debug. If counter won't start, only config_apply.cpp to check.

---

## Dependency Graph (clean → dirty)

```
Clean (no dependencies):
  constants.h
  types.h

↓ (depend on above)

Hardware Drivers:
  gpio_driver.c
  uart_driver.c
  pcnt_driver.c
  nvs_driver.c

↓

Protocol Layers:
  modbus_frame.c
  modbus_parser.c
  modbus_serializer.c

↓

Function Handlers:
  modbus_fc_read.c
  modbus_fc_write.c
  modbus_fc_dispatch.c

↓

Modbus Server:
  modbus_rx.c (uses uart_driver)
  modbus_tx.c (uses gpio_driver, uart_driver)
  modbus_server.c (uses parser, handlers, registers)

↓

Storage:
  registers.c
  coils.c
  gpio_mapping.c

↓

Feature Engines:
  counter_config.c
  counter_sw.c (uses gpio_driver)
  counter_sw_isr.c (uses gpio_driver)
  counter_hw.c (uses pcnt_driver)
  counter_frequency.c
  counter_engine.c (orchest sw/hw modes)

  timer_config.c
  timer_engine.c

↓

Config System:
  config_struct.c (just struct definition)
  config_load.c (uses nvs_driver)
  config_save.c (uses nvs_driver)
  config_apply.c (uses counter_engine, timer_engine, gpio_mapping)

↓

CLI:
  cli_parser.c
  cli_commands.c (uses config_apply, counter_engine)
  cli_show.c (uses counter_engine, timer_engine)
  cli_history.c
  cli_shell.c (orchestrator)

↓

Main:
  heartbeat.c (LED)
  version.c (strings)
  main.c (calls everything)
```

**Regel:** No circular dependencies. If A depends on B, B cannot depend on A.

---

## Build/Test Strategy

### Compile-time modular testing

```bash
# Test frame parsing WITHOUT hardware
gcc -o test_frame \
  src/modbus_frame.c \
  test/test_frame.c \
  -Iinclude

# Test CLI parsing WITHOUT Modbus server
gcc -o test_cli \
  src/cli_parser.c \
  test/test_cli.c \
  -Iinclude

# Test counter logic WITHOUT hardware (mock pcnt_driver)
gcc -o test_counter \
  src/counter_config.c \
  src/counter_engine.c \
  test/mock_pcnt_driver.c \
  test/test_counter.c \
  -Iinclude
```

---

## Migration from Mega2560

| File (Mega) | ESP32 Module(s) | Approach |
|---|---|---|
| modbus_core.cpp (500 lines) | modbus_frame, modbus_parser, modbus_serializer, modbus_rx, modbus_tx, modbus_server | **Split into 6 focused files** |
| modbus_fc.cpp (300 lines) | modbus_fc_read, modbus_fc_write, modbus_fc_dispatch | **Split by function code family** |
| modbus_counters.cpp (400 lines) | counter_config, counter_engine, counter_frequency | **Extract orchestration** |
| modbus_counters_hw.cpp (250 lines) | counter_hw, pcnt_driver | **New pcnt_driver layer** |
| modbus_counters_sw_int.cpp (200 lines) | counter_sw_isr, gpio_driver | **ISR-specific layer** |
| cli_shell.cpp (600 lines) | cli_parser, cli_commands, cli_show, cli_history, cli_shell | **5 focused files** |
| config_store.cpp (300 lines) | config_struct, config_load, config_save, config_apply, nvs_driver | **Separate concerns** |

---

## File Size Guidelines

```
Target line counts per file:
  - .h file: 50-150 lines (interface only)
  - .cpp file: 100-250 lines (implementation)
  - Some exceptions (cli_commands.cpp might be 300+ due to many commands)

Benefits:
  ✓ Can edit one file without affecting others
  ✓ Easier code review
  ✓ Clear responsibility
  ✓ Can write unit tests per file
  ✓ Debugging easier (grep fewer files)
```

---

## Summary: What changes from Mega2560

| Aspect | Mega2560 | ESP32 | Benefit |
|--------|----------|-------|---------|
| Files | 15 large files (500+ lines each) | 30+ small files (100-250 lines) | Better focus, less conflicts |
| Abstractions | HAL via Arduino libs | Explicit driver layer | Cleaner separation |
| Protocols | Protocol logic mixed with server | Separate frame/parser/serializer/server | Can test independently |
| Features | Engines mixed with config | Config struct separate from engines | Can change config without engine logic |
| CLI | All commands in one file (600 lines) | 4 focused CLI files | Easy to add new commands |
| Config | Load/save/apply mixed | Separate files per concern | Clear migration path |
| Testing | Hard to unit test | Can mock drivers, test in isolation | Higher confidence |

---

## Next Steps

1. Agree on this architecture
2. Create `ESP32_Module_Architecture.md` (this file)
3. Update `CLAUDE.md` with module guidelines
4. Create empty .cpp/.h file structure
5. Start porting modbus_frame.cpp (simplest, no dependencies)

**Question:** Should I create the full file structure as boilerplate now, or start with just the critical modules?
