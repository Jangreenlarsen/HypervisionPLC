// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_engine.cpp
 * @brief Counter orchestration and state machine (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters.cpp (complete orchestration)
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Initialize all counter modes (SW/ISR/HW)
 * - Dispatch to mode-specific handlers via loop()
 * - Write counter values to Modbus registers with prescaler division
 * - Handle control register commands (reset, start, stop)
 * - Update frequency and overflow registers
 *
 * KEY: UNIFIED PRESCALER STRATEGY
 * - Mode files count ALL edges (no skipping)
 * - This file divides by prescaler at output only
 * - value register = counterValue × scale
 * - raw register = counterValue / prescaler
 * - frequency register = measured Hz (no prescaler)
 *
 * Context: This is the "orchestrator" that ties all modes together
 */

#include "counter_engine.h"
#include "counter_config.h"
#include "counter_sw.h"
#include "counter_sw_isr.h"
#include "counter_hw.h"
#include "counter_frequency.h"
#include "registers.h"
#include "constants.h"
#include "debug.h"
#include <string.h>
#include <math.h>
#include <Arduino.h>  // For millis()

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

// COMPARE FEATURE RUNTIME STATE (v2.3+)
typedef struct {
  uint8_t compare_triggered;  // Flag: Compare værdi nået denne iteration
  uint32_t compare_time_ms;   // Timestamp når triggered
  uint64_t last_value;        // Previous counter value (for exact match detection)
} CounterCompareRuntime;

static CounterCompareRuntime counter_compare_state[COUNTER_COUNT];

// ISSUE-1 FIX: Atomic multi-word write protection
// Prevents Modbus master from reading mid-update on 32/64-bit counter values
// Simple spinlock: write lock bit during multi-word register updates
static volatile uint8_t counter_write_lock[COUNTER_COUNT] = {0, 0, 0, 0};

// Forward declaration for compare check function
static void counter_engine_check_compare(uint8_t id, uint64_t counter_value);

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_engine_init(void) {
  // Initialize configuration system
  counter_config_init();

  // BUG FIX 1.6: Don't init counters here with default config
  // Init will be called by counter_engine_configure() when config is applied
  // This ensures start_value and other parameters are read from actual config,
  // not from defaults

  // Initialize frequency tracking for all counters (safe to do with defaults)
  for (uint8_t id = 1; id <= 4; id++) {
    counter_frequency_init(id);
  }

  // COMPARE FEATURE: Initialize runtime state (v2.3+)
  memset(counter_compare_state, 0, sizeof(counter_compare_state));
}

/* ============================================================================
 * MAIN LOOP - DISPATCH TO MODE HANDLERS
 * ============================================================================ */

void counter_engine_loop(void) {
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (!counter_config_get(id, &cfg)) {
      continue;
    }

    // BUG-023 FIX: Check compare feature even if counter is disabled
    // Compare logic should work independently of enabled flag
    uint64_t counter_value = counter_engine_get_value(id);
    counter_engine_check_compare(id, counter_value);

    // Only run counter if enabled
    if (!cfg.enabled) {
      continue;
    }

    // Handle control register commands first
    counter_engine_handle_control(id);

    // Dispatch to mode-specific handler
    switch (cfg.hw_mode) {
      case COUNTER_HW_SW:
      case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
        // Software polling mode
        counter_sw_loop(id);
        break;

      case COUNTER_HW_SW_ISR:
        // Software ISR mode
        counter_sw_isr_loop(id);
        break;

      case COUNTER_HW_PCNT:
        // Hardware PCNT mode
        counter_hw_loop(id);
        break;

      default:
        // Unknown mode - skip
        continue;
    }

    // Update frequency measurement (works for all modes)
    uint64_t current_value = counter_engine_get_value(id);
    counter_frequency_update(id, current_value);

    // Write to Modbus registers (prescaler division happens here)
    counter_engine_store_value_to_registers(id);
  }
}

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

bool counter_engine_configure(uint8_t id, const CounterConfig* cfg) {
  if (cfg == NULL) return false;

  // ISSUE-2 FIX: Stop counter before reconfiguration
  // If counter is already running and we're changing config, stop it first
  // to prevent race conditions between old and new mode
  {
    CounterConfig old_cfg;
    if (counter_config_get(id, &old_cfg) && old_cfg.enabled) {
      // Stop old mode before switching
      switch (old_cfg.hw_mode) {
        case COUNTER_HW_SW:
        case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
          counter_sw_stop(id);
          break;
        case COUNTER_HW_SW_ISR:
          counter_sw_isr_detach(id);
          break;
        case COUNTER_HW_PCNT:
          counter_hw_stop(id);
          break;
        default:
          break;
      }
      debug_print("  ISSUE-2 FIX: Stopped old counter mode before reconfig\n");
    }
  }

  // Validate and set configuration
  if (!counter_config_set(id, cfg)) {
    return false;
  }

  // Initialize the chosen mode
  switch (cfg->hw_mode) {
    case COUNTER_HW_SW:
    case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
      counter_sw_init(id);
      break;

    case COUNTER_HW_SW_ISR:
      counter_sw_isr_init(id);
      // Attach interrupt if pin configured
      if (cfg->interrupt_pin > 0) {
        counter_sw_isr_attach(id, cfg->interrupt_pin);
      }
      break;

    case COUNTER_HW_PCNT:
      counter_hw_init(id);
      // BUG FIX 1.9: Configure PCNT with GPIO from config (not hardcoded)
      debug_print("  DEBUG: Counter ");
      debug_print_uint(id);
      debug_print(" PCNT mode, hw_gpio = ");
      debug_print_uint(cfg->hw_gpio);
      debug_print(", enabled = ");
      debug_print_uint(cfg->enabled);
      debug_println("");

      if (cfg->enabled && cfg->hw_gpio > 0) {
        counter_hw_configure(id, cfg->hw_gpio);
      } else {
        debug_println("  WARNING: PCNT not configured (hw_gpio = 0 or not enabled)");
      }
      break;

    default:
      return false;
  }

  // Reset frequency tracking
  counter_frequency_reset(id);

  return true;
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void counter_engine_reset(uint8_t id) {
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  // Reset based on mode
  switch (cfg.hw_mode) {
    case COUNTER_HW_SW:
    case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
      counter_sw_reset(id);
      // BUG-035 FIX: Clear internal overflow flag
      counter_sw_clear_overflow(id);
      break;
    case COUNTER_HW_SW_ISR:
      counter_sw_isr_reset(id);
      // BUG-035 FIX: Clear internal overflow flag
      counter_sw_isr_clear_overflow(id);
      break;
    case COUNTER_HW_PCNT:
      counter_hw_reset(id);
      // BUG-035 FIX: Clear internal overflow flag
      counter_hw_clear_overflow(id);
      break;
    default:
      break;
  }

  // Reset frequency tracking
  counter_frequency_reset(id);

  // Clear overflow flag in ctrl_reg bit 3
  if (cfg.ctrl_reg < HOLDING_REGS_SIZE) {
    uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
    ctrl_val &= ~(1 << 3);  // Clear bit 3 (overflow flag)
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val);
  }

  // BUG-035 FIX: Reset compare runtime state (last_value) to prevent false trigger
  if (id >= 1 && id <= 4) {
    counter_compare_state[id - 1].last_value = cfg.start_value;
    counter_compare_state[id - 1].compare_triggered = 0;
  }
}

void counter_engine_reset_all(void) {
  for (uint8_t id = 1; id <= 4; id++) {
    counter_engine_reset(id);
  }
}

/* ============================================================================
 * CONTROL REGISTER HANDLING (reset, start, stop bits)
 * ============================================================================ */

void counter_engine_handle_control(uint8_t id) {
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg) || cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return;
  }

  uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);

  // Bit 0: Reset command
  if (ctrl_val & 0x0001) {
    counter_engine_reset(id);
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0001);
  }

  // BUG FIX 2.1: Bit 1: Start command
  if (ctrl_val & 0x0002) {
    // Start the counter based on mode
    switch (cfg.hw_mode) {
      case COUNTER_HW_SW:
      case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
        counter_sw_start(id);
        break;
      case COUNTER_HW_SW_ISR:
        // BUG FIX 3.2: Reattach interrupt to ensure ISR is active
        if (cfg.interrupt_pin > 0) {
          counter_sw_isr_attach(id, cfg.interrupt_pin);
        }
        break;
      case COUNTER_HW_PCNT:
        counter_hw_start(id);
        break;
    }
    // Clear the start bit after executing
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0002);
  }

  // BUG FIX 2.1: Bit 2: Stop command
  if (ctrl_val & 0x0004) {
    // Stop the counter based on mode
    switch (cfg.hw_mode) {
      case COUNTER_HW_SW:
      case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
        counter_sw_stop(id);
        break;
      case COUNTER_HW_SW_ISR:
        // BUG FIX 3.2: Detach interrupt to prevent ISR calls when stopped
        counter_sw_isr_detach(id);
        break;
      case COUNTER_HW_PCNT:
        counter_hw_stop(id);
        break;
    }
    // Clear the stop bit after executing
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val & ~0x0004);
  }

  // BUG-016 FIX: Bit 7: Running flag (persistent state - doesn't auto-clear)
  // When bit 7 is set, counter should be actively counting
  // When bit 7 is cleared, counter should be stopped
  // This provides a persistent "running" state unlike bit 1/2 which are command bits
  if (ctrl_val & 0x0080) {
    // Running bit is SET - ensure counter is started
    switch (cfg.hw_mode) {
      case COUNTER_HW_SW:
      case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
        counter_sw_start(id);
        break;
      case COUNTER_HW_SW_ISR:
        if (cfg.interrupt_pin > 0) {
          counter_sw_isr_attach(id, cfg.interrupt_pin);
        }
        break;
      case COUNTER_HW_PCNT:
        // BUG-015 FIX: Validate GPIO is configured before starting PCNT
        if (cfg.hw_gpio > 0) {
          counter_hw_start(id);
        } else {
          debug_println("WARNING: Cannot start HW counter - GPIO not configured");
        }
        break;
    }
  } else {
    // Running bit is CLEARED - ensure counter is stopped
    switch (cfg.hw_mode) {
      case COUNTER_HW_SW:
      case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
        counter_sw_stop(id);
        break;
      case COUNTER_HW_SW_ISR:
        counter_sw_isr_detach(id);
        break;
      case COUNTER_HW_PCNT:
        counter_hw_stop(id);
        break;
    }
  }

  // Bit 3: Reset-on-read (sticky bit, remains set until cleared by user)
  // This is handled at Modbus FC level, not here
}

/* ============================================================================
 * REGISTER VALUE STORAGE (with prescaler division)
 * UNIFIED PRESCALER STRATEGY: ALL division happens here
 * ============================================================================ */

void counter_engine_store_value_to_registers(uint8_t id) {
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg) || !cfg.enabled) {
    return;
  }

  // Get current value from appropriate mode
  uint64_t counter_value = counter_engine_get_value(id);

  // FIX P0.7: PCNT hardware counts ONLY the configured edges (rising/falling/both)
  // Previous code incorrectly assumed PCNT always counts both edges and divided by 2
  // PCNT_COUNT_DIS correctly disables edge counting, so no division needed
  // The PCNT driver (pcnt_driver.cpp) configures pos_mode/neg_mode correctly:
  //   - RISING: pos_mode=INC, neg_mode=DIS → counts only rising edges
  //   - FALLING: pos_mode=DIS, neg_mode=INC → counts only falling edges
  //   - BOTH: pos_mode=INC, neg_mode=INC → counts both edges
  // Therefore counter_value is already correct, no adjustment needed

  // PRESCALER DIVISION: raw = counterValue / prescaler
  // (counterValue already includes ALL edges, no skipping)
  uint64_t raw_value = counter_value;
  if (cfg.prescaler > 1) {
    raw_value = counter_value / cfg.prescaler;
  }

  // SCALING: scaled = counterValue × scale
  double scale = (cfg.scale_factor > 0.0f) ? (double)cfg.scale_factor : 1.0;
  double scaled_float = (double)counter_value * scale;

  // Clamp to bit width
  uint8_t bw = cfg.bit_width;
  uint64_t max_val = 0;
  switch (bw) {
    case 8:
      max_val = 0xFFULL;
      break;
    case 16:
      max_val = 0xFFFFULL;
      break;
    case 32:
      max_val = 0xFFFFFFFFULL;
      break;
    case 64:
    default:
      max_val = 0xFFFFFFFFFFFFFFFFULL;
      break;
  }

  if (scaled_float < 0.0) scaled_float = 0.0;
  if (scaled_float > (double)max_val) scaled_float = (double)max_val;

  uint64_t scaled_value = (uint64_t)(scaled_float + 0.5);  // Round to nearest
  scaled_value &= max_val;
  raw_value &= max_val;

  // ISSUE-1 FIX: Atomic protection for multi-word writes
  // Set write lock to prevent Modbus master from reading mid-update
  uint8_t lock_id = id - 1;  // Array is 0-indexed
  counter_write_lock[lock_id] = 1;

  // Write scaled value to value register (multi-word if 32/64 bit)
  if (cfg.value_reg < HOLDING_REGS_SIZE) {
    uint8_t words = (bw <= 16) ? 1 : (bw == 32) ? 2 : 4;
    for (uint8_t w = 0; w < words && cfg.value_reg + w < HOLDING_REGS_SIZE; w++) {
      uint16_t word = (uint16_t)((scaled_value >> (16 * w)) & 0xFFFF);
      registers_set_holding_register(cfg.value_reg + w, word);
    }
  }

  // Write raw (prescaled) value to raw register
  if (cfg.raw_reg < HOLDING_REGS_SIZE) {
    uint8_t words = (bw <= 16) ? 1 : (bw == 32) ? 2 : 4;
    for (uint8_t w = 0; w < words && cfg.raw_reg + w < HOLDING_REGS_SIZE; w++) {
      uint16_t word = (uint16_t)((raw_value >> (16 * w)) & 0xFFFF);
      registers_set_holding_register(cfg.raw_reg + w, word);
    }
  }

  // Release write lock
  counter_write_lock[lock_id] = 0;

  // Write frequency to freq register (no prescaler compensation)
  if (cfg.freq_reg < HOLDING_REGS_SIZE) {
    uint16_t freq_hz = counter_frequency_get(id);
    registers_set_holding_register(cfg.freq_reg, freq_hz);
  }

  // Write overflow flag to ctrl_reg bit 3
  if (cfg.ctrl_reg < HOLDING_REGS_SIZE) {
    uint8_t overflow = counter_engine_get_overflow(id);  // BUG-434: faelles getter

    // Set or clear bit 3 in ctrl_reg
    uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
    if (overflow) {
      ctrl_val |= (1 << 3);  // Set bit 3
    } else {
      ctrl_val &= ~(1 << 3);  // Clear bit 3
    }
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val);
  }

  // BUG-030: Write compare_value to register (for Modbus read/write)
  if (cfg.compare_enabled && cfg.compare_value_reg < HOLDING_REGS_SIZE) {
    uint8_t words = (bw <= 16) ? 1 : (bw == 32) ? 2 : 4;
    for (uint8_t w = 0; w < words && cfg.compare_value_reg + w < HOLDING_REGS_SIZE; w++) {
      uint16_t word = (uint16_t)((cfg.compare_value >> (16 * w)) & 0xFFFF);
      registers_set_holding_register(cfg.compare_value_reg + w, word);
    }
  }

  // COMPARE CHECK (v2.3+)
  // Check if counter value meets compare criteria and update status bit
  counter_engine_check_compare(id, counter_value);
}

/* ============================================================================
 * COMPARE FEATURE (v2.3+)
 * ============================================================================ */

static void counter_engine_check_compare(uint8_t id, uint64_t counter_value) {
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg) || !cfg.compare_enabled) {
    return;  // Compare feature disabled
  }

  if (cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
    return;  // Invalid control register
  }

  // BUG-030: Read compare_value from Modbus register (allows runtime modification)
  uint64_t compare_value = cfg.compare_value;  // Fallback to config value
  if (cfg.compare_value_reg < HOLDING_REGS_SIZE) {
    uint8_t words = (cfg.bit_width <= 16) ? 1 : (cfg.bit_width == 32) ? 2 : 4;
    compare_value = 0;
    for (uint8_t w = 0; w < words && cfg.compare_value_reg + w < HOLDING_REGS_SIZE; w++) {
      uint16_t word = registers_get_holding_register(cfg.compare_value_reg + w);
      compare_value |= ((uint64_t)word) << (16 * w);
    }
  }

  // BUG-040 FIX: Calculate compare source value based on compare_source setting
  // 0 = raw (hardware counter), 1 = prescaled (÷ prescaler), 2 = scaled (× scale)
  uint64_t compare_source_value = counter_value;  // Default: raw

  if (cfg.compare_source == 1) {
    // PRESCALED: Divide by prescaler
    if (cfg.prescaler > 1) {
      compare_source_value = counter_value / cfg.prescaler;
    }
  } else if (cfg.compare_source == 2) {
    // SCALED: Multiply by scale factor
    double scale = (cfg.scale_factor > 0.0f) ? (double)cfg.scale_factor : 1.0;
    double scaled_float = (double)counter_value * scale;

    // Clamp to bit width
    uint64_t max_val = 0xFFFFFFFFFFFFFFFFULL;
    switch (cfg.bit_width) {
      case 8:  max_val = 0xFFULL; break;
      case 16: max_val = 0xFFFFULL; break;
      case 32: max_val = 0xFFFFFFFFULL; break;
      case 64: max_val = 0xFFFFFFFFFFFFFFFFULL; break;
    }

    if (scaled_float < 0.0) scaled_float = 0.0;
    if (scaled_float > (double)max_val) scaled_float = (double)max_val;

    compare_source_value = (uint64_t)(scaled_float + 0.5);  // Round to nearest
    compare_source_value &= max_val;
  }
  // else: compare_source == 0, use raw counter_value (already set)

  // Get runtime state for tracking previous value
  CounterCompareRuntime *runtime = &counter_compare_state[id - 1];

  // BUG-029 FIX: All compare modes should use edge detection (rising edge trigger)
  // Only set bit4 when crossing threshold, not on every iteration while above it
  // This allows reset-on-read to work correctly - bit4 stays cleared until next crossing
  uint8_t compare_hit = 0;

  switch (cfg.compare_mode) {
    case 0:  // ≥ (greater-or-equal) - Rising edge detection
      // Trigger when crossing from below to at-or-above threshold
      compare_hit = (runtime->last_value < compare_value &&
                     compare_source_value >= compare_value) ? 1 : 0;
      break;

    case 1:  // > (greater-than) - Rising edge detection
      // Trigger when crossing from at-or-below to strictly above threshold
      compare_hit = (runtime->last_value <= compare_value &&
                     compare_source_value > compare_value) ? 1 : 0;
      break;

    case 2:  // === (exact match, only on rising edge transition)
      // Only trigger when crossing from below to at-or-above compare value
      compare_hit = (runtime->last_value < compare_value &&
                     compare_source_value >= compare_value) ? 1 : 0;
      break;
  }

  // Update last value for next iteration (used by all modes for edge detection)
  runtime->last_value = compare_source_value;

  // If compare condition met, set bit 4 in control register
  if (compare_hit) {
    uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
    ctrl_val |= (1 << 4);  // Set bit 4 (compare status bit)
    registers_set_holding_register(cfg.ctrl_reg, ctrl_val);

    // Log in runtime state
    runtime->compare_triggered = 1;
    runtime->compare_time_ms = millis();
  }
}

/* ============================================================================
 * CONFIGURATION ACCESS
 * ============================================================================ */

bool counter_engine_get_config(uint8_t id, CounterConfig* out) {
  return counter_config_get(id, out);
}

uint8_t counter_engine_get_overflow(uint8_t id) {
  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return 0;
  switch (cfg.hw_mode) {
    case COUNTER_HW_SW:
    case COUNTER_HW_ENCODER: return counter_sw_get_overflow(id) ? 1 : 0;
    case COUNTER_HW_SW_ISR: return counter_sw_isr_get_overflow(id) ? 1 : 0;
    case COUNTER_HW_PCNT:   return counter_hw_get_overflow(id) ? 1 : 0;  // BUG FIX P0.3
    default:                return 0;
  }
}

/* ============================================================================
 * VALUE ACCESS (returns counter value before prescaler/scale)
 * ============================================================================ */

uint64_t counter_engine_get_value(uint8_t id) {
  if (id < 1 || id > 4) return 0;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return 0;

  // Get value from appropriate mode
  switch (cfg.hw_mode) {
    case COUNTER_HW_SW:
    case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
      return counter_sw_get_value(id);

    case COUNTER_HW_SW_ISR:
      return counter_sw_isr_get_value(id);

    case COUNTER_HW_PCNT:
      return counter_hw_get_value(id);

    default:
      return 0;
  }
}

void counter_engine_set_value(uint8_t id, uint64_t value) {
  if (id < 1 || id > 4) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  // Set value in appropriate mode
  switch (cfg.hw_mode) {
    case COUNTER_HW_SW:
    case COUNTER_HW_ENCODER:  // FEAT-470: samme SW-sti
      counter_sw_set_value(id, value);
      break;

    case COUNTER_HW_SW_ISR:
      counter_sw_isr_set_value(id, value);
      break;

    case COUNTER_HW_PCNT:
      counter_hw_set_value(id, value);
      break;

    default:
      break;
  }

  // Reset frequency tracking after manual set
  counter_frequency_reset(id);
}

/* ============================================================================
 * ATOMIC WRITE LOCK ACCESS (ISSUE-1 FIX)
 * ============================================================================ */

uint8_t counter_engine_is_write_locked(uint8_t id) {
  if (id < 1 || id > 4) return 0;
  return counter_write_lock[id - 1];
}

/* ============================================================================
 * INTERNAL HELPERS (for mode-specific overflow access)
 * ============================================================================ */

uint8_t counter_sw_get_overflow(uint8_t id);
uint8_t counter_sw_isr_get_overflow(uint8_t id);
uint8_t counter_hw_get_overflow(uint8_t id);
