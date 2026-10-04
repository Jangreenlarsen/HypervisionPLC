// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_edge.cpp
 * @brief ST Edge Detection Implementation
 *
 * Implements R_TRIG and F_TRIG edge detectors per IEC 61131-3.
 */

#include "st_builtin_edge.h"

/* ============================================================================
 * R_TRIG - Rising Edge Detector
 * ============================================================================ */

st_value_t st_builtin_r_trig(st_value_t CLK, st_edge_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    // No instance storage - cannot detect edge
    return result;
  }

  // Detect rising edge: current=TRUE AND last=FALSE
  bool current = CLK.bool_val;
  bool rising_edge = (current && !instance->last_state);

  // Update instance state for next cycle
  instance->last_state = current;

  // Return TRUE only on rising edge
  result.bool_val = rising_edge;
  return result;
}

/* ============================================================================
 * F_TRIG - Falling Edge Detector
 * ============================================================================ */

st_value_t st_builtin_f_trig(st_value_t CLK, st_edge_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    // No instance storage - cannot detect edge
    return result;
  }

  // Detect falling edge: current=FALSE AND last=TRUE
  bool current = CLK.bool_val;
  bool falling_edge = (!current && instance->last_state);

  // Update instance state for next cycle
  instance->last_state = current;

  // Return TRUE only on falling edge
  result.bool_val = falling_edge;
  return result;
}
