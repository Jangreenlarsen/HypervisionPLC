/**
 * @file st_builtin_timers.cpp
 * @brief ST Timer Implementation (TON, TOF, TP)
 *
 * Implements IEC 61131-3 standard timers with millisecond precision.
 */

#include "st_builtin_timers.h"

/* ============================================================================
 * TON - On-Delay Timer
 * ============================================================================ */

st_value_t st_builtin_ton(st_value_t IN, st_value_t PT, st_timer_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot run timer
  }

  bool current_IN = IN.bool_val;
  // v4.7+: PT validation - negative values → 0 (prevent huge unsigned conversion)
  // FEAT-121: Use dint_val for TIME support (32-bit range, up to ~24.8 days)
  uint32_t preset_time = (PT.dint_val < 0) ? 0 : (uint32_t)PT.dint_val;
  uint32_t now = millis();

  // Detect rising edge on IN
  bool rising_edge = (current_IN && !instance->last_IN);

  if (rising_edge) {
    // Start timer on rising edge
    instance->start_time = now;
    instance->running = true;
    instance->ET = 0;
    instance->Q = false;
  }

  if (current_IN) {
    // Input is HIGH
    if (instance->running) {
      // Update elapsed time
      instance->ET = now - instance->start_time;

      // Check if preset time expired
      if (instance->ET >= preset_time) {
        instance->Q = true;
        instance->running = false;  // Timer done
      }
    }
  } else {
    // Input is LOW - reset timer immediately
    instance->Q = false;
    instance->ET = 0;
    instance->running = false;
  }

  // Update last state
  instance->last_IN = current_IN;

  // Return output
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * TOF - Off-Delay Timer
 * ============================================================================ */

st_value_t st_builtin_tof(st_value_t IN, st_value_t PT, st_timer_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot run timer
  }

  bool current_IN = IN.bool_val;
  // v4.7+: PT validation - negative values → 0 (prevent huge unsigned conversion)
  // FEAT-121: Use dint_val for TIME support (32-bit range, up to ~24.8 days)
  uint32_t preset_time = (PT.dint_val < 0) ? 0 : (uint32_t)PT.dint_val;
  uint32_t now = millis();

  // Detect falling edge on IN
  bool falling_edge = (!current_IN && instance->last_IN);

  if (falling_edge) {
    // Start timer on falling edge
    instance->start_time = now;
    instance->running = true;
    instance->ET = 0;
    instance->Q = true;  // Output stays TRUE during delay
  }

  if (!current_IN) {
    // Input is LOW
    if (instance->running) {
      // Update elapsed time
      instance->ET = now - instance->start_time;

      // Check if preset time expired
      if (instance->ET >= preset_time) {
        instance->Q = false;
        instance->running = false;  // Timer done
      }
    }
  } else {
    // Input is HIGH - output follows input immediately
    instance->Q = true;
    instance->ET = 0;
    instance->running = false;
  }

  // Update last state
  instance->last_IN = current_IN;

  // Return output
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * TP - Pulse Timer
 * ============================================================================ */

st_value_t st_builtin_tp(st_value_t IN, st_value_t PT, st_timer_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot run timer
  }

  bool current_IN = IN.bool_val;
  // v4.7+: PT validation - negative values → 0 (prevent huge unsigned conversion)
  // FEAT-121: Use dint_val for TIME support (32-bit range, up to ~24.8 days)
  uint32_t preset_time = (PT.dint_val < 0) ? 0 : (uint32_t)PT.dint_val;
  uint32_t now = millis();

  // Detect rising edge on IN
  bool rising_edge = (current_IN && !instance->last_IN);

  if (rising_edge && !instance->running) {
    // Start pulse on rising edge (only if not already running)
    instance->start_time = now;
    instance->running = true;
    instance->ET = 0;
    instance->Q = true;
  }

  if (instance->running) {
    // Update elapsed time
    instance->ET = now - instance->start_time;

    // Check if pulse duration expired
    if (instance->ET >= preset_time) {
      instance->Q = false;
      instance->running = false;  // Pulse complete
      instance->ET = preset_time;  // Clamp ET to PT
    }
  }

  // Update last state
  instance->last_IN = current_IN;

  // Return output
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

uint32_t st_timer_get_et(st_timer_instance_t* instance) {
  if (!instance) return 0;
  return instance->ET;
}
