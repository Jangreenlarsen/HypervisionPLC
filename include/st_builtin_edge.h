// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_edge.h
 * @brief ST Edge Detection Functions (R_TRIG, F_TRIG)
 *
 * IEC 61131-3 standard edge detection function blocks.
 *
 * Functions:
 * - R_TRIG(CLK) : BOOL - Rising edge detector (FALSE→TRUE)
 * - F_TRIG(CLK) : BOOL - Falling edge detector (TRUE→FALSE)
 *
 * Usage:
 *   VAR
 *     button_count : INT := 0;
 *   END_VAR
 *
 *   IF R_TRIG(button_input) THEN
 *     button_count := button_count + 1;  (* Count button presses *)
 *   END_IF;
 *
 * Note: These are stateful functions - require instance storage.
 */

#ifndef ST_BUILTIN_EDGE_H
#define ST_BUILTIN_EDGE_H

#include "st_types.h"
#include "st_stateful.h"

/* ============================================================================
 * EDGE DETECTION FUNCTIONS
 * ============================================================================ */

/**
 * @brief Rising edge detector (R_TRIG)
 *
 * Detects 0→1 transition on input signal.
 * Returns TRUE for exactly ONE cycle when input goes from FALSE to TRUE.
 *
 * @param CLK Current input signal
 * @param instance Pointer to edge detector instance storage
 * @return TRUE on rising edge (0→1), FALSE otherwise
 *
 * @example
 *   IF R_TRIG(button) THEN
 *     count := count + 1;  (* Increment on button press *)
 *   END_IF;
 */
st_value_t st_builtin_r_trig(st_value_t CLK, st_edge_instance_t* instance);

/**
 * @brief Falling edge detector (F_TRIG)
 *
 * Detects 1→0 transition on input signal.
 * Returns TRUE for exactly ONE cycle when input goes from TRUE to FALSE.
 *
 * @param CLK Current input signal
 * @param instance Pointer to edge detector instance storage
 * @return TRUE on falling edge (1→0), FALSE otherwise
 *
 * @example
 *   IF F_TRIG(sensor) THEN
 *     log_event();  (* Log when sensor deactivates *)
 *   END_IF;
 */
st_value_t st_builtin_f_trig(st_value_t CLK, st_edge_instance_t* instance);

#endif // ST_BUILTIN_EDGE_H
