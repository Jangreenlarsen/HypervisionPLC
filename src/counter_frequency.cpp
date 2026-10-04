// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_frequency.cpp
 * @brief Frequency measurement for counters (LAYER 5)
 *
 * Ported from: Mega2560 v3.6.5 modbus_counters.cpp (lines ~383-677)
 * Adapted to: ESP32 modular architecture
 *
 * Responsibility:
 * - Frequency measurement via delta counting
 * - Timing window validation (1-2 second windows)
 * - Overflow/underflow detection
 * - Result validation and clamping
 *
 * Algorithm:
 * 1. Sample counter value every ~1 second
 * 2. Calculate: Hz = (delta_count) / (delta_time_seconds)
 * 3. Validate delta against max 100 kHz threshold
 * 4. Detect overflow/underflow wrap-around
 * 5. Clamp result to 0-20000 Hz range
 * 6. Reset on timeout (5+ sec) or counter reset
 *
 * Works identically for all counter modes (SW/ISR/HW)
 */

#include "counter_frequency.h"
#include "counter_config.h"  // BUG FIX 1.8: Need config for bit_width
#include "registers.h"
#include "constants.h"
#include <string.h>

/* ============================================================================
 * FREQUENCY MEASUREMENT STATE (per counter)
 * ============================================================================ */

typedef struct {
  uint16_t current_hz;         // Last measured frequency
  uint64_t last_count;         // Count at last measurement
  uint32_t last_measure_ms;    // Timestamp of last measurement (0 = not started)
  uint8_t window_valid;        // Timing window is valid for calculation
} FrequencyState;

static FrequencyState freq_state[4] = {0};

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void counter_frequency_init(uint8_t id) {
  if (id < 1 || id > 4) return;

  FrequencyState* state = &freq_state[id - 1];
  state->current_hz = 0;
  state->last_count = 0;
  state->last_measure_ms = 0;
  state->window_valid = 0;
}

/* ============================================================================
 * FREQUENCY UPDATE (called from counter_engine loop)
 * ============================================================================ */

uint16_t counter_frequency_update(uint8_t id, uint64_t current_value) {
  if (id < 1 || id > 4) return 0;

  FrequencyState* state = &freq_state[id - 1];
  uint32_t now_ms = registers_get_millis();

  // BUG FIX 1.8: Get config to determine bit_width for wrap-around calculation
  CounterConfig cfg;
  if (!counter_config_get(id, &cfg)) return state->current_hz;

  // First-time initialization
  if (state->last_measure_ms == 0) {
    state->last_measure_ms = now_ms;
    state->last_count = current_value;
    state->current_hz = 0;
    state->window_valid = 0;
    return 0;
  }

  // Calculate time delta
  uint32_t delta_time_ms = now_ms - state->last_measure_ms;

  // Check timing window (1000-2000 ms is valid for ~1 Hz resolution)
  if (delta_time_ms >= 1000 && delta_time_ms <= 2000) {
    state->window_valid = 1;

    // Calculate count delta (handle wrap-around)
    uint64_t delta_count = 0;
    uint8_t valid_delta = 1;

    // BUG-184 FIX: Direction-aware frequency calculation
    // UP counting: current_value >= last_count is normal
    // DOWN counting: current_value <= last_count is normal
    uint64_t max_val = 0xFFFFFFFFFFFFFFFFULL;  // Default 64-bit
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
      // 64-bit: use default
    }

    if (cfg.direction == COUNTER_DIR_DOWN) {
      // DOWN counting: value decreases over time
      if (current_value <= state->last_count) {
        // Normal: count decreased
        delta_count = state->last_count - current_value;
      } else {
        // Underflow wrap-around: counter wrapped from 0 to start_value
        // Example: last=5, current=995, start=1000 → wrapped, delta = 5 + (1000 - 995) = 10
        delta_count = state->last_count + (cfg.start_value - current_value) + 1;

        // Sanity check: if delta is unreasonably large (>50% of start_value), skip
        if (cfg.start_value > 0 && delta_count > cfg.start_value / 2) {
          valid_delta = 0;
        }
      }
    } else {
      // UP counting: value increases over time (original logic)
      if (current_value >= state->last_count) {
        // Normal: count increased
        delta_count = current_value - state->last_count;
      } else {
        // Overflow wrap-around
        delta_count = (max_val - state->last_count) + current_value + 1;

        // Sanity check: if delta is unreasonably large (>50% of max), skip
        if (delta_count > max_val / 2) {
          valid_delta = 0;
        }
      }
    }

    // Validate delta against max 100 kHz threshold (1000 counts/10ms)
    if (valid_delta && delta_count <= 100000UL) {
      // BUG FIX 1.8: Calculate frequency with 64-bit math to prevent overflow
      // Hz = (delta_count * 1000) / delta_time_ms
      uint64_t freq_calc_64 = (delta_count * 1000ULL) / delta_time_ms;
      uint32_t freq_calc = (uint32_t)freq_calc_64;

      // Clamp to reasonable range (0-20000 Hz)
      if (freq_calc > 20000UL) {
        freq_calc = 20000UL;
      }

      state->current_hz = (uint16_t)freq_calc;
    }
    // else: keep last valid value

    // Update for next measurement
    state->last_count = current_value;
    state->last_measure_ms = now_ms;

  } else if (delta_time_ms > 5000) {
    // Timeout: reset tracking if no update for 5+ seconds
    state->last_measure_ms = now_ms;
    state->last_count = current_value;
    state->current_hz = 0;
    state->window_valid = 0;
  }

  return state->current_hz;
}

/* ============================================================================
 * FREQUENCY ACCESS
 * ============================================================================ */

uint16_t counter_frequency_get(uint8_t id) {
  if (id < 1 || id > 4) return 0;
  return freq_state[id - 1].current_hz;
}

void counter_frequency_reset(uint8_t id) {
  if (id < 1 || id > 4) return;

  FrequencyState* state = &freq_state[id - 1];
  state->current_hz = 0;
  state->last_count = 0;
  state->last_measure_ms = 0;
  state->window_valid = 0;
}

bool counter_frequency_is_valid(uint8_t id, uint16_t* out_hz, uint32_t* out_window) {
  if (id < 1 || id > 4) return false;

  FrequencyState* state = &freq_state[id - 1];

  if (out_hz != NULL) {
    *out_hz = state->current_hz;
  }

  if (out_window != NULL) {
    uint32_t now_ms = registers_get_millis();
    if (state->last_measure_ms > 0) {
      *out_window = now_ms - state->last_measure_ms;
    } else {
      *out_window = 0;
    }
  }

  return state->window_valid;
}
