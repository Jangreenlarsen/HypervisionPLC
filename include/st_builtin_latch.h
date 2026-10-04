// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_latch.h
 * @brief ST Bistable Latch Functions (SR, RS)
 *
 * IEC 61131-3 standard bistable function blocks.
 *
 * Functions:
 * - SR(S1, R) : BOOL - Set-Reset Latch (Reset priority)
 * - RS(S, R1) : BOOL - Reset-Set Latch (Set priority)
 *
 * Both latches maintain their output state when both inputs are FALSE.
 *
 * Usage:
 *   VAR
 *     alarm_active : BOOL;
 *     sensor_trigger : BOOL;
 *     reset_button : BOOL;
 *   END_VAR
 *
 *   (* SR latch - alarm activates on sensor, deactivates on reset *)
 *   alarm_active := SR(sensor_trigger, reset_button);
 *
 * Note: SR and RS differ only in priority when both inputs are TRUE.
 */

#ifndef ST_BUILTIN_LATCH_H
#define ST_BUILTIN_LATCH_H

#include "st_types.h"
#include "st_stateful.h"

/* ============================================================================
 * LATCH FUNCTIONS
 * ============================================================================ */

/**
 * @brief Set-Reset Latch (SR)
 *
 * Bistable latch with Reset priority.
 *
 * Truth table:
 *   S1  R  |  Q
 *   -------+----
 *   0   0  | Hold (previous state)
 *   0   1  | 0 (reset)
 *   1   0  | 1 (set)
 *   1   1  | 0 (reset priority!)
 *
 * IEC 61131-3 Standard Function Block.
 *
 * @param S1 Set input (BOOL) - Sets output to TRUE
 * @param R Reset input (BOOL) - Resets output to FALSE (priority)
 * @param instance Pointer to latch instance storage
 * @return Q output (BOOL) - Current latch state
 *
 * @example
 *   alarm := SR(sensor_triggered, reset_button);
 *   (* Alarm sets on sensor trigger, resets on button press *)
 */
st_value_t st_builtin_sr(st_value_t S1, st_value_t R, st_latch_instance_t* instance);

/**
 * @brief Reset-Set Latch (RS)
 *
 * Bistable latch with Set priority.
 *
 * Truth table:
 *   S   R1 |  Q
 *   -------+----
 *   0   0  | Hold (previous state)
 *   0   1  | 0 (reset)
 *   1   0  | 1 (set)
 *   1   1  | 1 (set priority!)
 *
 * IEC 61131-3 Standard Function Block.
 *
 * @param S Set input (BOOL) - Sets output to TRUE (priority)
 * @param R1 Reset input (BOOL) - Resets output to FALSE
 * @param instance Pointer to latch instance storage
 * @return Q output (BOOL) - Current latch state
 *
 * @example
 *   motor_running := RS(start_button, stop_button);
 *   (* Motor starts on start button, stops on stop button *)
 *   (* If both pressed simultaneously, start has priority *)
 */
st_value_t st_builtin_rs(st_value_t S, st_value_t R1, st_latch_instance_t* instance);

#endif // ST_BUILTIN_LATCH_H
