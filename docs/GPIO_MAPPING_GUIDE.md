# Complete GPIO Mapping Guide

**ESP32 Modbus RTU Server v2.1.0** - GPIO Control via Modbus Registers and ST Logic

---

## 📋 Table of Contents

1. [Overview](#overview)
2. [Hardware Pins](#hardware-pins)
3. [Mapping Types](#mapping-types)
   - [Standard GPIO Mapping](#standard-gpio-mapping)
   - [Virtual GPIO Mapping](#virtual-gpio-mapping)
4. [GPIO2 Special Mode](#gpio2-special-mode)
5. [CLI Commands Reference](#cli-commands-reference)
6. [Examples](#examples)
7. [Troubleshooting](#troubleshooting)

---

## Overview

GPIO mapping allows you to connect physical GPIO pins or virtual registers to Modbus coils and discrete inputs. There are two mapping systems:

1. **GPIO Mapping** - Direct connection of physical GPIO pins to Modbus registers
2. **ST Logic Binding** - Connection of ST program variables to Modbus registers (via unified mapping)

### Key Features

- ✅ Control physical GPIO pins via Modbus registers
- ✅ Read GPIO pin states via discrete inputs
- ✅ Virtual GPIO (pins 100+) for register-based control
- ✅ ST Logic variable binding to GPIO
- ✅ Persistent storage to NVS
- ✅ Up to 32 simultaneous mappings

---

## Hardware Pins

### ESP32-WROOM-32 Pin Mapping

```
GPIO Pin  │ Function          │ Notes
──────────┼──────────────────┼─────────────────────────────
GPIO2     │ User/Heartbeat    │ Special mode: set gpio 2 enable/disable
GPIO3     │ General purpose   │ Available for user control
GPIO4     │ UART1 RX (Modbus) │ Reserved for Modbus RTU
GPIO5     │ UART1 TX (Modbus) │ Reserved for Modbus RTU
GPIO12    │ General purpose   │ Available for user control
GPIO15    │ RS-485 DIR        │ Reserved (RS-485 direction control)
GPIO16    │ General purpose   │ Available for user control
GPIO17    │ General purpose   │ Available for user control
GPIO18    │ General purpose   │ Available for user control
GPIO19    │ General purpose   │ Available for user control
GPIO21    │ General purpose   │ Available (future I2C SDA)
GPIO22    │ General purpose   │ Available (future I2C SCL)
GPIO23    │ General purpose   │ Available for user control
GPIO25-27 │ General purpose   │ Available for user control
GPIO32-39 │ General purpose   │ Available for user control
```

### Virtual GPIO Pins

Virtual GPIO pins (100+) are implemented as register-based controls:

```
Virtual Pin │ Source              │ Example
────────────┼────────────────────┼──────────────────────
101         │ Coil 1              │ set gpio 101 static map input:5
102         │ Coil 2              │
103         │ Coil 3              │
...         │ ...                 │
```

---

## Mapping Types

### Standard GPIO Mapping

**Maps physical GPIO pins to Modbus registers**

#### Input Mapping (GPIO pin → Discrete Input)

Read the state of a physical GPIO pin and expose it as a discrete input:

```bash
set gpio <pin> static map input:<discrete_input_idx>

# Example: Read GPIO 23 as Discrete Input #5
set gpio 23 static map input:5
```

**When to use:**
- Reading button states
- Monitoring digital sensors
- Detecting low/high signal conditions

**Data flow:**
```
GPIO Pin 23 (HIGH/LOW)
         ↓
      GPIO Driver
         ↓
   Discrete Input #5 (1/0)
         ↓
    Modbus Master reads DI#5
```

#### Output Mapping (Coil → GPIO pin)

Write the state of a Modbus coil to a physical GPIO pin:

```bash
set gpio <pin> static map coil:<coil_idx>

# Example: Write Coil #0 to GPIO 2
set gpio 2 static map coil:0

# Example: Write Coil #12 to GPIO 12
set gpio 12 static map coil:112
```

**When to use:**
- Controlling LEDs
- Driving relays
- Switching digital outputs

**Data flow:**
```
Modbus Master writes Coil #0
         ↓
   Coil Array (updated)
         ↓
   GPIO Mapping detects change
         ↓
      GPIO Driver
         ↓
    GPIO Pin 2 (HIGH/LOW)
         ↓
    Physical LED/Relay
```

### Virtual GPIO Mapping

**Register-based GPIO for control without physical pins**

Virtual GPIO pins (100+) act as virtual GPIO by reading/writing to Modbus coils.

#### Virtual GPIO Input (Coil → Virtual Input)

```bash
set gpio 101 static map input:5   # Virtual GPIO 101 reads Coil 1 as DI#5
set gpio 102 static map input:6   # Virtual GPIO 102 reads Coil 2 as DI#6
```

**Data flow:**
```
Modbus Coil 1
     ↓
Virtual GPIO 101
     ↓
Discrete Input #5
```

**Use case:** Mirror coil states as discrete inputs for SCADA systems that only understand DI, not coils.

#### Virtual GPIO Output (Virtual Output → Coil)

```bash
set gpio 200 static map coil:5   # Virtual GPIO 200 writes to Coil 5
```

**Note:** Virtual GPIO numbers for output are arbitrary (>= 100). The actual "GPIO pin" is a register offset.

---

## GPIO2 Special Mode

**GPIO 2 is reserved for heartbeat LED by default. It must be explicitly enabled for user control.**

### Enable GPIO2 User Mode

```bash
# Activate user mode (disables heartbeat)
set gpio 2 enable

# Save to persistent storage
save

# (Optional) Map to Coil #0 if using ST Logic or Modbus control
set gpio 2 static map coil:0
```

### Disable GPIO2 User Mode

```bash
# Return to heartbeat mode
set gpio 2 disable

# Save to persistent storage
save
```

### GPIO2 Status

```bash
# Check current status
show gpio

# Output should show:
# GPIO 2 Status: USER MODE (if enabled)
# GPIO 2 Status: HEARTBEAT MODE (if disabled)
```

### GPIO2 with ST Logic

Complete workflow for controlling GPIO2 LED via ST Logic:

```bash
# 1. Enable user mode
set gpio 2 enable
save

# 2. Map GPIO2 to Coil#0 (so ST programs can control it)
set gpio 2 static map coil:0

# 3. Upload ST program
set logic 1 upload "VAR led: BOOL; END_VAR led := TRUE;"

# 4. Bind ST variable to coil
set logic 1 bind led coil:0

# 5. Enable program
set logic 1 enabled:true

# LED now responds to ST logic changes
```

---

## CLI Commands Reference

### Check Current Mappings

```bash
# Show all GPIO configuration
show gpio

# Output example:
# === GPIO Configuration ===
# GPIO 2 Status: USER MODE (frigivet til bruger)
#   Kommandoer: 'set gpio 2 enable' / 'set gpio 2 disable'
#
# User configured (2 mappings):
#   GPIO 23 - input:45  [fjern: 'no set gpio 23']
#   GPIO 2  - coil:0    [fjern: 'no set gpio 2']
```

### Add GPIO Mapping

```bash
# GPIO → Discrete Input
set gpio <pin> static map input:<discrete_input_idx>

# Coil → GPIO Output
set gpio <pin> static map coil:<coil_idx>
```

### Remove GPIO Mapping

```bash
# Remove any mapping from a GPIO pin
no set gpio <pin>

# Example:
no set gpio 23
no set gpio 2
```

### GPIO2 Control

```bash
# Enable GPIO2 user mode
set gpio 2 enable

# Disable GPIO2 user mode (restore heartbeat)
set gpio 2 disable
```

### Persist Changes

```bash
# Save all GPIO configurations to NVS
save
```

---

## Examples

### Example 1: Control LED via Modbus Coil

**Setup:**
- GPIO 12 connected to LED (with resistor)
- Control via Modbus Coil #112

**Commands:**
```bash
# Map coil to GPIO
set gpio 12 static map coil:112

# Save config
save
```

**Operation:**
```bash
# Turn LED ON (via Modbus master or CLI)
set coil 112 1

# Turn LED OFF
set coil 112 0
```

### Example 2: Read Button via GPIO

**Setup:**
- GPIO 23 connected to button
- Read via Discrete Input #45

**Commands:**
```bash
# Map GPIO to discrete input
set gpio 23 static map input:45

# Save config
save
```

**Operation:**
```bash
# Read button state
show discrete_input 45

# Modbus master can read via FC02 (Read Discrete Inputs)
```

### Example 3: GPIO2 LED Control via ST Logic

**Complete workflow:**

```bash
# 1. Enable GPIO2
set gpio 2 enable
save

# 2. Map GPIO2 to Coil#0
set gpio 2 static map coil:0

# 3. Create ST program
set logic 1 upload "VAR counter: INT; led: BOOL; END_VAR IF counter > 50 THEN led := TRUE; ELSE led := FALSE; END_IF;"

# 4. Bind variables
set logic 1 bind counter reg:10
set logic 1 bind led coil:0

# 5. Enable program
set logic 1 enabled:true

# 6. Test - LED turns ON when counter > 50
set holding_register 10 75
show logic 1   # Verify

# 7. Test - LED turns OFF when counter <= 50
set holding_register 10 25
```

**Data flow:**
```
HR#10 (input: 75)
    ↓
ST Variable: counter
    ↓
IF counter > 50 THEN led := TRUE
    ↓
ST Variable: led (TRUE)
    ↓
Coil#0 (set to 1)
    ↓
GPIO Mapping: Coil#0 → GPIO 2
    ↓
GPIO Pin 2 (HIGH)
    ↓
Physical LED (ON) 🔵
```

### Example 4: Multiple GPIO Mappings

**Setup multiple pins for different purposes:**

```bash
# Input mappings (GPIO → Discrete Inputs)
set gpio 23 static map input:45   # Button 1
set gpio 18 static map input:46   # Button 2
set gpio 19 static map input:47   # Button 3

# Output mappings (Coils → GPIO)
set gpio 12 static map coil:110   # LED 1
set gpio 16 static map coil:111   # LED 2
set gpio 17 static map coil:112   # LED 3

# Save all mappings
save
```

### Example 5: Virtual GPIO for SCADA Compatibility

**Mirror Coils as Discrete Inputs for legacy SCADA:**

```bash
# Virtual GPIO 101 reads Coil 1 as Discrete Input
set gpio 101 static map input:50

# Virtual GPIO 102 reads Coil 2 as Discrete Input
set gpio 102 static map input:51

# SCADA can now read DI#50 instead of Coil#1
save
```

---

## Mapping Limitations

### Maximum Mappings

- **Total mappings:** 32 per configuration
- **Virtual GPIO pins:** 100-199 (for distinction from physical pins 0-99)

### Pin Availability

Pins reserved for Modbus/System:
- GPIO 4, 5: UART1 (Modbus RTU)
- GPIO 15: RS-485 direction control
- GPIO 0, 2: Strapping pins (boot mode) - use with caution

### Coil/Register Ranges

- **Coils:** 0-255 (256 coils available)
- **Discrete Inputs:** 0-255 (256 inputs available)
- **Holding Registers:** 0-159 (160 registers)
- **Input Registers:** 0-159 (160 registers)

---

## Persistence

### Automatic Saving

Some operations automatically save mappings to NVS:
- ST Logic variable bindings (`set logic <id> bind ...`)
- GPIO2 mode changes (`set gpio 2 enable/disable`)

### Manual Saving

Explicit GPIO mappings require manual save:

```bash
set gpio 23 static map input:45
set gpio 12 static map coil:112

# Must run save to persist
save
```

### Reload on Boot

All saved mappings reload automatically on ESP32 restart:
```bash
# After reboot, all GPIO mappings are active
show gpio
```

---

## Troubleshooting

### Problem: GPIO Mapping Not Working

**Symptom:** Coil changes don't affect GPIO pin, or GPIO reads return nothing

**Cause:** Mapping not saved to NVS

**Solution:**
```bash
# Verify mapping is configured
show gpio

# If mapping is missing, reconfigure it
set gpio 12 static map coil:110

# Save explicitly
save

# Verify
show gpio   # Should show the mapping now
```

### Problem: GPIO2 LED Not Responding to ST Logic

**Symptom:** ST program changes led variable, but GPIO2 doesn't toggle

**Cause:** Missing explicit GPIO2-to-Coil mapping, or GPIO2 still in heartbeat mode

**Solution:**
```bash
# 1. Check if GPIO2 is in user mode
show gpio

# 2. If not, enable it
set gpio 2 enable
save

# 3. Add explicit mapping
set gpio 2 static map coil:0

# 4. Verify binding in ST program
show logic 1
# Should show: led → Coil#0 (output)
```

### Problem: Button Input Not Detected

**Symptom:** GPIO pin state changes, but discrete input stays constant

**Cause:** GPIO not mapped, or input pin has debouncing delay

**Solution:**
```bash
# Check mapping
show gpio

# If missing, add it
set gpio 23 static map input:45

# Save
save

# Wait a moment for debouncing, then check
show discrete_input 45
```

### Problem: Multiple Mappings Conflict

**Symptom:** Two GPIO pins map to same coil, causing unexpected behavior

**Cause:** Configuration conflict

**Solution:**
```bash
# List all current mappings
show gpio

# Remove conflicting mappings
no set gpio <conflicting_pin>

# Reconfigure correctly
set gpio 12 static map coil:110
set gpio 16 static map coil:111

# Save
save
```

### Problem: Virtual GPIO Not Working

**Symptom:** Virtual GPIO (pin >= 100) doesn't respond

**Cause:** Virtual GPIO feature not properly configured, or incorrect source

**Solution:**
```bash
# Virtual GPIO 101 maps to Coil 1 (NOT Coil 101)
set gpio 101 static map input:50

# Verify that Coil 1 is being updated
set coil 1 1

# Check if DI#50 becomes 1
show discrete_input 50
```

---

## Architecture Notes

### GPIO Mapping Implementation

```c
// In gpio_mapping.cpp:
void gpio_mapping_update(void) {
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping* map = &g_persist_config.var_maps[i];

    if (map->source_type == MAPPING_SOURCE_GPIO) {
      if (map->gpio_pin >= 100) {
        // Virtual GPIO: read from coil
        uint16_t virtual_coil = map->gpio_pin - 100;
        int level = registers_get_coil(virtual_coil) ? 1 : 0;
      } else {
        // Physical GPIO: read/write pin
        if (map->is_input) {
          int level = gpio_driver_read(map->gpio_pin);
          registers_set_discrete_input(map->input_reg, level);
        } else {
          int state = registers_get_coil(map->output_reg);  // BUG-011: renamed from coil_reg
          gpio_driver_write(map->gpio_pin, state);
        }
      }
    }
  }
}
```

### Update Cycle

GPIO mappings are processed every 10ms in the main loop:

```
Main Loop (every 10ms)
  ├─ Phase 1: Read all GPIO inputs
  ├─ Phase 2: Execute Modbus server
  ├─ Phase 3: Update GPIO outputs
  ├─ Phase 4: Execute ST Logic (if enabled)
  └─ Phase 5: Update coil/register mappings
```

This ensures responsive control with minimal latency (~5-10ms).

---

## Summary Table

| Task | Command | Notes |
|------|---------|-------|
| Read GPIO via DI | `set gpio <pin> static map input:<idx>` | GPIO → Discrete Input |
| Control GPIO via Coil | `set gpio <pin> static map coil:<idx>` | Coil → GPIO |
| Control GPIO2 LED | `set gpio 2 enable` + `set gpio 2 static map coil:0` | Must map to coil |
| Remove mapping | `no set gpio <pin>` | Unmaps pin |
| Save mappings | `save` | Persist to NVS |
| Show all mappings | `show gpio` | List current config |
| Virtual GPIO input | `set gpio 101 static map input:50` | Coil 1 → DI#50 |

---

**Last Updated:** 2025-12-02

**Document Status:** ✅ Comprehensive (all features documented)
