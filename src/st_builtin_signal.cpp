// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_signal.cpp
 * @brief ST Signal Processing Function Implementation
 *
 * Implements signal processing and conditioning functions.
 */

#include "st_builtin_signal.h"
#include <Arduino.h>
#include <math.h>

/* ============================================================================
 * SCALE - Linear Scaling/Mapping
 * ============================================================================ */

st_value_t st_builtin_scale(st_value_t in, st_value_t in_min, st_value_t in_max,
                             st_value_t out_min, st_value_t out_max) {
  st_value_t result;
  result.real_val = 0.0f;

  // Extract values
  float in_val = in.real_val;
  float in_min_val = in_min.real_val;
  float in_max_val = in_max.real_val;
  float out_min_val = out_min.real_val;
  float out_max_val = out_max.real_val;

  // BUG-161 FIX: Avoid divide-by-zero, return 0.0 instead of arbitrary out_min
  if (in_max_val == in_min_val) {
    result.real_val = 0.0f;  // Consistent with other math functions (SQRT, LN, etc.)
    return result;
  }

  // Clamp input to input range
  if (in_val < in_min_val) {
    in_val = in_min_val;
  }
  if (in_val > in_max_val) {
    in_val = in_max_val;
  }

  // Linear scaling: OUT = (IN - IN_MIN) / (IN_MAX - IN_MIN) * (OUT_MAX - OUT_MIN) + OUT_MIN
  float span_in = in_max_val - in_min_val;
  float span_out = out_max_val - out_min_val;
  result.real_val = (in_val - in_min_val) * span_out / span_in + out_min_val;

  return result;
}

/* ============================================================================
 * HYSTERESIS - Schmitt Trigger
 * ============================================================================ */

st_value_t st_builtin_hysteresis(st_value_t in, st_value_t high, st_value_t low,
                                  st_hysteresis_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot maintain state
  }

  // Extract input values
  float in_val = in.real_val;
  float high_val = high.real_val;
  float low_val = low.real_val;

  // BUG-176 FIX: Validate that high threshold is greater than low threshold
  if (high_val <= low_val) {
    // Invalid thresholds → reset to OFF state and return false
    instance->Q = false;
    result.bool_val = false;
    return result;
  }

  // Hysteresis logic
  if (in_val > high_val) {
    // Above upper threshold - switch ON
    instance->Q = true;
  } else if (in_val < low_val) {
    // Below lower threshold - switch OFF
    instance->Q = false;
  }
  // Else: in dead zone (low ≤ in ≤ high) - hold previous state

  // Return current output state
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * BLINK - Periodic Pulse Generator
 * ============================================================================ */

st_value_t st_builtin_blink(st_value_t enable, st_value_t on_time, st_value_t off_time,
                             st_blink_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot maintain state
  }

  // Extract input values
  bool enable_val = enable.bool_val;

  // BUG-165 FIX: Validate time values are non-negative before casting to unsigned
  if (on_time.int_val < 0 || off_time.int_val < 0) {
    // Invalid time → disable blinking and return false
    instance->Q = false;
    instance->state = 0;  // IDLE
    result.bool_val = false;
    return result;
  }

  uint32_t on_time_ms = (uint32_t)on_time.int_val;
  uint32_t off_time_ms = (uint32_t)off_time.int_val;

  // Get current timestamp
  uint32_t now = millis();

  // BUG-170 NOTE: millis() wraparound handling
  // millis() wraps around after ~49.7 days (2^32 milliseconds).
  // The expression (now - instance->timer) uses unsigned arithmetic which correctly
  // handles wraparound due to two's complement modulo arithmetic (per C standard).
  // Example: If timer=4294967290 and now=5 (after wrap), then:
  //   now - timer = 5 - 4294967290 = 15 (via uint32_t wraparound)
  // This is mathematically correct and no special handling is needed.

  // State machine
  if (!enable_val) {
    // Disabled - reset to IDLE
    instance->Q = false;
    instance->state = 0;  // IDLE
    instance->timer = now;
  } else {
    if (instance->state == 0) {
      // IDLE → Start blinking
      instance->Q = true;
      instance->timer = now;
      instance->state = 1;  // ON_PHASE
    } else if (instance->state == 1) {
      // ON_PHASE - check if ON duration expired
      if ((now - instance->timer) >= on_time_ms) {
        instance->Q = false;
        instance->timer = now;
        instance->state = 2;  // OFF_PHASE
      }
    } else if (instance->state == 2) {
      // OFF_PHASE - check if OFF duration expired
      if ((now - instance->timer) >= off_time_ms) {
        instance->Q = true;
        instance->timer = now;
        instance->state = 1;  // ON_PHASE
      }
    }
  }

  // Return current output state
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * FILTER - First-Order Low-Pass Filter
 * ============================================================================ */

st_value_t st_builtin_filter(st_value_t in, st_value_t time_constant,
                              st_filter_instance_t* instance, uint32_t cycle_time_ms) {
  st_value_t result;
  result.real_val = 0.0f;

  if (!instance) {
    return result;  // No storage - cannot maintain state
  }

  // Extract input values
  float in_val = in.real_val;
  float tau = (float)time_constant.int_val;  // Time constant in ms

  // Avoid negative or zero time constant
  if (tau <= 0.0f) {
    // No filtering - pass through
    instance->out_prev = in_val;
    result.real_val = in_val;
    return result;
  }

  // BUG-153 FIX: Use actual cycle time from engine state
  float DT = (float)cycle_time_ms;  // milliseconds

  // BUG-175 FIX: Avoid invalid cycle time (defensive check)
  // This should never happen in normal operation since cycle_time_ms is set by engine,
  // but provides safe fallback if stateful storage is not properly initialized.
  if (DT <= 0.0f) {
    DT = 10.0f;  // Fallback to 10ms default (approximate typical ST execution interval)
  }

  // Calculate smoothing factor: α = DT / (τ + DT)
  float alpha = DT / (tau + DT);

  // Exponential moving average: OUT = OUT_prev + α * (IN - OUT_prev)
  float out_val = instance->out_prev + alpha * (in_val - instance->out_prev);

  // Update state
  instance->out_prev = out_val;

  // Return filtered output
  result.real_val = out_val;
  return result;
}
