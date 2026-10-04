// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_sw.cpp
 * @brief Software polling mode counter (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters.cpp
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Polling discrete inputs for edge detection
 * - Counting edges (all edges - no prescaler skipping here)
 * - Debounce filtering
 * - Overflow detection
 */

#include "counter_sw.h"
#include "counter_config.h"
#include "registers.h"
#include "constants.h"
#include "types.h"
#include <string.h>

/* ============================================================================
 * SW MODE RUNTIME STATE (per counter)
 * Uses CounterSWState from types.h
 * ============================================================================ */

static CounterSWState sw_state[COUNTER_COUNT] = {0};

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_sw_init(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterSWState* state = &sw_state[id - 1];
  state->counter_value = 0;
  state->last_level = 0;
  // BUG FIX 1.7: Initialize debounce_timer to current time to prevent initial false window
  state->debounce_timer = registers_get_millis();
  state->is_counting = 0;
  state->overflow_flag = 0;  // BUG FIX 1.1: Initialize overflow flag

  // Get config to initialize last_level
  CounterConfig cfg;
  if (counter_config_get(id, &cfg)) {
    // Read initial level from discrete input
    if (cfg.input_dis < (DISCRETE_INPUTS_SIZE * 8)) {
      state->last_level = registers_get_discrete_input(cfg.input_dis) ? 1 : 0;
    }

    // Set start value
    state->counter_value = cfg.start_value;
  }
}

/* ============================================================================
 * MAIN LOOP - POLLING & EDGE DETECTION
 * ============================================================================ */

void counter_sw_loop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  if (!cfg.enabled || cfg.hw_mode != COUNTER_HW_SW) {
    return;
  }

  CounterSWState* state = &sw_state[id - 1];

  // BUG FIX 2.1: Check if counting is enabled (start/stop control)
  if (!state->is_counting) {
    return;  // Counter stopped, skip counting
  }

  // Read current level from discrete input
  uint8_t current_level = (cfg.input_dis < (DISCRETE_INPUTS_SIZE * 8)) ?
    registers_get_discrete_input(cfg.input_dis) ? 1 : 0 : 0;

  // BUG FIX 1.7: Check debounce_enabled before applying debounce
  uint32_t now_ms = registers_get_millis();

  if (cfg.debounce_enabled) {
    // Debounce: only count if enough time has passed
    uint32_t debounce_ms = cfg.debounce_ms > 0 ? cfg.debounce_ms : 10;  // Default 10ms

    if (now_ms - state->debounce_timer < debounce_ms) {
      return;  // Still in debounce window
    }
  }

  // Edge detection based on mode
  uint8_t edge_detected = 0;

  if (cfg.edge_type == COUNTER_EDGE_RISING && state->last_level == 0 && current_level == 1) {
    edge_detected = 1;
  } else if (cfg.edge_type == COUNTER_EDGE_FALLING && state->last_level == 1 && current_level == 0) {
    edge_detected = 1;
  } else if (cfg.edge_type == COUNTER_EDGE_BOTH && state->last_level != current_level) {
    edge_detected = 1;
  }

  // Update last level for next iteration
  state->last_level = current_level;

  // Count the edge
  if (edge_detected) {
    // BUG FIX 1.5: Implement direction (UP/DOWN)
    if (cfg.direction == COUNTER_DIR_UP) {
      state->counter_value++;
    } else {
      // BUG-181 FIX: DOWN counting: decrement with underflow wrap to start_value (not max_val!)
      if (state->counter_value > 0) {
        state->counter_value--;
      } else {
        // Underflow: wrap to start_value (consistent with UP mode wrapping)
        state->counter_value = cfg.start_value;
        state->overflow_flag = 1;  // Set overflow flag on underflow wrap
      }
    }

    // BUG FIX 1.7: Only update debounce timer if debounce is enabled
    if (cfg.debounce_enabled) {
      state->debounce_timer = now_ms;  // Reset debounce timer
    }

    // Check for overflow based on bit width (UP counting only)
    if (cfg.direction == COUNTER_DIR_UP) {
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

      // BUG-180 FIX: Preserve overflow counts when wrapping to start_value
      if (state->counter_value > max_val) {
        uint64_t overflow_amt = state->counter_value - max_val - 1;
        state->counter_value = (cfg.start_value + overflow_amt) & max_val;
        state->overflow_flag = 1;  // BUG FIX 1.1: Set overflow flag
      }
    }
  }
}

/* ============================================================================
 * RESET
 * ============================================================================ */

void counter_sw_reset(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;

  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return;

  CounterSWState* state = &sw_state[id - 1];
  state->counter_value = cfg.start_value;
  state->debounce_timer = 0;
}

/* ============================================================================
 * VALUE ACCESS
 * ============================================================================ */

uint64_t counter_sw_get_value(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  return sw_state[id - 1].counter_value;
}

void counter_sw_set_value(uint8_t id, uint64_t value) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].counter_value = value;
}

uint8_t counter_sw_get_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return 0;
  // BUG FIX 1.1: Return actual overflow flag
  return sw_state[id - 1].overflow_flag;
}

void counter_sw_clear_overflow(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  // BUG FIX 1.1: Clear overflow flag
  sw_state[id - 1].overflow_flag = 0;
}

/* ============================================================================
 * START/STOP CONTROL (BUG FIX 2.1: Control register bits)
 * ============================================================================ */

void counter_sw_start(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].is_counting = 1;
}

void counter_sw_stop(uint8_t id) {
  if (id < 1 || id > COUNTER_COUNT) return;
  sw_state[id - 1].is_counting = 0;
}
