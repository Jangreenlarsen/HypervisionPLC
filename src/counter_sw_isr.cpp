// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_sw_isr.cpp
 * @brief Software ISR (interrupt) mode counter (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters.cpp
 * Adapted to: ESP32 GPIO interrupt handling
 *
 * Responsibility:
 * - GPIO interrupt attachment and handling
 * - ISR-based edge counting (INT0-INT5)
 * - Debounce in main loop
 */

#include "counter_sw_isr.h"
#include "counter_config.h"
#include "gpio_driver.h"
#include "registers.h"
#include "constants.h"
#include "types.h"
#include <Arduino.h>
#include <string.h>

/* ============================================================================
 * ISR MODE RUNTIME STATE (per counter)
 * Minimal state for ISR-based counting
 *
 * BUG FIX 3.1: ISR-accessed variables MUST be volatile to prevent compiler
 * optimizations that cache values in registers. Without volatile, the compiler
 * may not reload values from memory, causing race conditions between ISR and
 * main loop contexts.
 * ============================================================================ */

static volatile CounterSWState isr_state[COUNTER_COUNT] = {0};  // BUG FIX 3.1: volatile for ISR access
static uint8_t isr_gpio_pins[COUNTER_COUNT] = {0};
static volatile unsigned long last_interrupt_time[COUNTER_COUNT] = {0};
static const unsigned long DEBOUNCE_MICROS = 50;  // 50 microseconds debounce
static volatile uint8_t isr_direction[COUNTER_COUNT] = {COUNTER_DIR_UP, COUNTER_DIR_UP, COUNTER_DIR_UP, COUNTER_DIR_UP};  // BUG FIX 1.5 + 3.1
static volatile uint64_t isr_max_val[COUNTER_COUNT] = {0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL};  // BUG-036 FIX: max value for overflow wrapping
static volatile uint64_t isr_start_val[COUNTER_COUNT] = {0, 0, 0, 0};  // BUG-181 FIX: start_value for underflow wrapping

/* ============================================================================
 * ISR HANDLERS (IRAM_ATTR for fast interrupt handling with debounce)
 * ============================================================================ */

void IRAM_ATTR counter_isr_0() {
  // BUG FIX 2.1: Check if counting is enabled
  if (!isr_state[0].is_counting) return;

  unsigned long interrupt_time = micros();
  if (interrupt_time - last_interrupt_time[0] > DEBOUNCE_MICROS) {
    // BUG FIX 1.5 + BUG-181: Implement direction with proper underflow wrapping
    if (isr_direction[0] == COUNTER_DIR_UP) {
      isr_state[0].counter_value++;
    } else {
      // BUG-181 FIX: DOWN counting wraps to start_value (stored in ISR at attach time)
      if (isr_state[0].counter_value > 0) {
        isr_state[0].counter_value--;
      } else {
        // Underflow: wrap to isr_start_val (not max_val!)
        isr_state[0].counter_value = isr_start_val[0];
        isr_state[0].overflow_flag = 1;  // Set overflow flag on underflow wrap
      }
    }
    last_interrupt_time[0] = interrupt_time;
  }
}

void IRAM_ATTR counter_isr_1() {
  // BUG FIX 2.1: Check if counting is enabled
  if (!isr_state[1].is_counting) return;

  unsigned long interrupt_time = micros();
  if (interrupt_time - last_interrupt_time[1] > DEBOUNCE_MICROS) {
    // BUG FIX 1.5 + BUG-181: Implement direction with proper underflow wrapping
    if (isr_direction[1] == COUNTER_DIR_UP) {
      isr_state[1].counter_value++;
    } else {
      // BUG-181 FIX: DOWN counting wraps to start_value (not max_val!)
      if (isr_state[1].counter_value > 0) {
        isr_state[1].counter_value--;
      } else {
        // Underflow: wrap to isr_start_val (consistent with UP mode)
        isr_state[1].counter_value = isr_start_val[1];
        isr_state[1].overflow_flag = 1;  // Set overflow flag on underflow wrap
      }
    }
    last_interrupt_time[1] = interrupt_time;
  }
}

void IRAM_ATTR counter_isr_2() {
  // BUG FIX 2.1: Check if counting is enabled
  if (!isr_state[2].is_counting) return;

  unsigned long interrupt_time = micros();
  if (interrupt_time - last_interrupt_time[2] > DEBOUNCE_MICROS) {
    // BUG FIX 1.5 + BUG-181: Implement direction with proper underflow wrapping
    if (isr_direction[2] == COUNTER_DIR_UP) {
      isr_state[2].counter_value++;
    } else {
      // BUG-181 FIX: DOWN counting wraps to start_value (not max_val!)
      if (isr_state[2].counter_value > 0) {
        isr_state[2].counter_value--;
      } else {
        // Underflow: wrap to isr_start_val (consistent with UP mode)
        isr_state[2].counter_value = isr_start_val[2];
        isr_state[2].overflow_flag = 1;  // Set overflow flag on underflow wrap
      }
    }
    last_interrupt_time[2] = interrupt_time;
  }
}

void IRAM_ATTR counter_isr_3() {
  // BUG FIX 2.1: Check if counting is enabled
  if (!isr_state[3].is_counting) return;

  unsigned long interrupt_time = micros();
  if (interrupt_time - last_interrupt_time[3] > DEBOUNCE_MICROS) {
    // BUG FIX 1.5 + BUG-181: Implement direction with proper underflow wrapping
    if (isr_direction[3] == COUNTER_DIR_UP) {
      isr_state[3].counter_value++;
    } else {
      // BUG-181 FIX: DOWN counting wraps to start_value (not max_val!)
      if (isr_state[3].counter_value > 0) {
        isr_state[3].counter_value--;
      } else {
        // Underflow: wrap to isr_start_val (consistent with UP mode)
        isr_state[3].counter_value = isr_start_val[3];
        isr_state[3].overflow_flag = 1;  // Set overflow flag on underflow wrap
      }
    }
    last_interrupt_time[3] = interrupt_time;
  }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_sw_isr_init(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  volatile CounterSWState* state = &isr_state[id - 1];  // BUG FIX 3.1: volatile pointer
  state->counter_value = 0;
  state->last_level = 0;
  state->debounce_timer = 0;
  state->is_counting = 0;
  state->overflow_flag = 0;  // BUG FIX 1.2: Initialize overflow flag

  // Get config to set start value
  CounterConfig cfg;
  if (counter_config_get(id, &cfg)) {
    state->counter_value = cfg.start_value;
  }
}

/* ============================================================================
 * MAIN LOOP - DEBOUNCE HANDLING ONLY
 * ISR increments counter in interrupt context
 * ============================================================================ */

void counter_sw_isr_loop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  if (!cfg.enabled || cfg.hw_mode != COUNTER_HW_SW_ISR) {
    return;
  }

  volatile CounterSWState* state = &isr_state[id - 1];  // BUG FIX 3.1: volatile pointer

  // Check for overflow/underflow based on bit width
  uint64_t max_val = 0xFFFFFFFFFFFFFFFFULL;
  switch (cfg.bit_width) {
    case 8:
      max_val = 0xFFULL;
      break;
    case 16:
      max_val = 0xFFFFULL;
      break;
    case 32:
      max_val = 0xFFFFFFFFULL;
      break;
  }

  // BUG FIX 1.5: Handle overflow (UP mode wraps at max_val)
  // BUG-036 FIX: Underflow (DOWN mode) now wraps in ISR, no check needed here
  // BUG-180 FIX: Preserve overflow counts when wrapping to start_value
  if (cfg.direction == COUNTER_DIR_UP && state->counter_value > max_val) {
    uint64_t overflow_amt = state->counter_value - max_val - 1;
    state->counter_value = (cfg.start_value + overflow_amt) & max_val;
    state->overflow_flag = 1;  // BUG FIX 1.2: Set overflow flag
  }
}

/* ============================================================================
 * INTERRUPT ATTACHMENT
 * ============================================================================ */

void counter_sw_isr_attach(uint8_t id, uint8_t gpio_pin) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  // Detach any existing interrupt first
  if (isr_gpio_pins[id - 1] > 0) {
    detachInterrupt(digitalPinToInterrupt(isr_gpio_pins[id - 1]));
  }

  // Configure GPIO as input
  pinMode(gpio_pin, INPUT);

  // BUG FIX 1.5: Store direction for ISR use
  isr_direction[id - 1] = cfg.direction;

  // BUG-036 FIX: Store max_val for ISR overflow wrapping
  switch (cfg.bit_width) {
    case 8:
      isr_max_val[id - 1] = 0xFFULL;
      break;
    case 16:
      isr_max_val[id - 1] = 0xFFFFULL;
      break;
    case 32:
      isr_max_val[id - 1] = 0xFFFFFFFFULL;
      break;
    default:
      isr_max_val[id - 1] = 0xFFFFFFFFFFFFFFFFULL;  // 64-bit
      break;
  }

  // BUG-181 FIX: Store start_value for ISR underflow wrapping in DOWN mode
  isr_start_val[id - 1] = cfg.start_value;

  // Map edge type to Arduino interrupt mode
  int mode = RISING;  // Default
  if (cfg.edge_type == COUNTER_EDGE_FALLING) {
    mode = FALLING;
  } else if (cfg.edge_type == COUNTER_EDGE_BOTH) {
    mode = CHANGE;  // Both edges
  } else {
    mode = RISING;
  }

  // Attach the correct ISR based on counter ID
  void (*isr_handler)() = NULL;
  switch (id) {
    case 1: isr_handler = counter_isr_0; break;
    case 2: isr_handler = counter_isr_1; break;
    case 3: isr_handler = counter_isr_2; break;
    case 4: isr_handler = counter_isr_3; break;
  }

  if (isr_handler != NULL) {
    attachInterrupt(digitalPinToInterrupt(gpio_pin), isr_handler, mode);
    isr_gpio_pins[id - 1] = gpio_pin;
    isr_state[id - 1].is_counting = 1;
  }
}

void counter_sw_isr_detach(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  if (isr_gpio_pins[id - 1] > 0) {
    detachInterrupt(digitalPinToInterrupt(isr_gpio_pins[id - 1]));
    isr_gpio_pins[id - 1] = 0;
  }

  isr_state[id - 1].is_counting = 0;
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void counter_sw_isr_reset(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  volatile CounterSWState* state = &isr_state[id - 1];  // BUG FIX 3.1: volatile pointer
  state->counter_value = cfg.start_value;
  state->debounce_timer = 0;
}

/* ============================================================================
 * VALUE ACCESS
 * ============================================================================ */

uint64_t counter_sw_isr_get_value(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  // BUG-034 FIX: Use volatile pointer to ensure fresh read from memory
  // Without this, compiler may cache value in register and miss ISR updates
  volatile CounterSWState* state = &isr_state[id - 1];
  return state->counter_value;
}

void counter_sw_isr_set_value(uint8_t id, uint64_t value) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG-034 FIX: Use volatile pointer for consistent access
  volatile CounterSWState* state = &isr_state[id - 1];
  state->counter_value = value;
}

uint8_t counter_sw_isr_get_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  // BUG-034 FIX: Use volatile pointer for consistent access
  volatile CounterSWState* state = &isr_state[id - 1];
  return state->overflow_flag;
}

void counter_sw_isr_clear_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG-034 FIX: Use volatile pointer for consistent access
  volatile CounterSWState* state = &isr_state[id - 1];
  state->overflow_flag = 0;
}

/* ============================================================================
 * START/STOP CONTROL (BUG FIX 2.1: Control register bits)
 * ============================================================================ */

void counter_sw_isr_start(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG-034 FIX: Use volatile pointer for consistent access
  volatile CounterSWState* state = &isr_state[id - 1];
  state->is_counting = 1;
}

void counter_sw_isr_stop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG-034 FIX: Use volatile pointer for consistent access
  volatile CounterSWState* state = &isr_state[id - 1];
  state->is_counting = 0;
}
