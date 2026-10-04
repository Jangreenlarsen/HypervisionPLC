// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_latch.cpp
 * @brief ST Bistable Latch Implementation (SR, RS)
 *
 * Implements IEC 61131-3 standard bistable latches.
 */

#include "st_builtin_latch.h"

/* ============================================================================
 * SR - Set-Reset Latch (Reset priority)
 * ============================================================================ */

st_value_t st_builtin_sr(st_value_t S1, st_value_t R, st_latch_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot maintain state
  }

  // Extract input values
  bool set_input = S1.bool_val;
  bool reset_input = R.bool_val;

  // SR Logic: Reset has priority
  // Truth table:
  //   R=1          → Q=0 (reset, regardless of S)
  //   R=0 and S=1  → Q=1 (set)
  //   R=0 and S=0  → Q holds previous state
  if (reset_input) {
    // Reset has priority - force output to FALSE
    instance->Q = false;
  } else if (set_input) {
    // Set input active (and reset not active) - set output to TRUE
    instance->Q = true;
  }
  // Else: Both inputs FALSE - hold previous state (no action)

  // Return current output state
  result.bool_val = instance->Q;
  return result;
}

/* ============================================================================
 * RS - Reset-Set Latch (Set priority)
 * ============================================================================ */

st_value_t st_builtin_rs(st_value_t S, st_value_t R1, st_latch_instance_t* instance) {
  st_value_t result;
  result.bool_val = false;

  if (!instance) {
    return result;  // No storage - cannot maintain state
  }

  // Extract input values
  bool set_input = S.bool_val;
  bool reset_input = R1.bool_val;

  // RS Logic: Set has priority
  // Truth table:
  //   S=1          → Q=1 (set, regardless of R)
  //   S=0 and R=1  → Q=0 (reset)
  //   S=0 and R=0  → Q holds previous state
  if (set_input) {
    // Set has priority - force output to TRUE
    instance->Q = true;
  } else if (reset_input) {
    // Reset input active (and set not active) - reset output to FALSE
    instance->Q = false;
  }
  // Else: Both inputs FALSE - hold previous state (no action)

  // Return current output state
  result.bool_val = instance->Q;
  return result;
}
