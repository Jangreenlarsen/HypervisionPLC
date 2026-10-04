// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_counters.h
 * @brief ST Counter Functions (CTU, CTD, CTUD)
 *
 * IEC 61131-3 standard counter function blocks.
 *
 * Functions:
 * - CTU(CU, RESET, PV) : BOOL - Count Up
 * - CTD(CD, LOAD, PV) : BOOL  - Count Down
 * - CTUD(CU, CD, RESET, LOAD, PV) : BOOL - Count Up/Down
 *
 * All counters track CV (Current Value) and return Q when limit reached.
 *
 * Usage:
 *   VAR
 *     product_count : INT := 0;
 *   END_VAR
 *
 *   IF CTU(sensor, reset_btn, 100) THEN
 *     (* Batch of 100 complete *)
 *   END_IF;
 */

#ifndef ST_BUILTIN_COUNTERS_H
#define ST_BUILTIN_COUNTERS_H

#include "st_types.h"
#include "st_stateful.h"

/* ============================================================================
 * COUNTER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Count Up Counter (CTU)
 *
 * Increments CV on rising edge of CU input.
 * Output Q goes TRUE when CV >= PV.
 * RESET input resets CV to 0.
 *
 * @param CU Count-up input (BOOL) - rising edge increments
 * @param RESET Reset input (BOOL) - TRUE resets counter
 * @param PV Preset value (INT) - count limit
 * @param instance Pointer to counter instance storage
 * @return Q output (BOOL) - TRUE when CV >= PV
 *
 * @example
 *   done := CTU(pulse, reset, 100);  (* Count to 100 *)
 */
st_value_t st_builtin_ctu(st_value_t CU, st_value_t RESET, st_value_t PV, st_counter_instance_t* instance);

/**
 * @brief Count Down Counter (CTD)
 *
 * Decrements CV on rising edge of CD input.
 * Output Q goes TRUE when CV <= 0.
 * LOAD input loads PV into CV.
 *
 * @param CD Count-down input (BOOL) - rising edge decrements
 * @param LOAD Load input (BOOL) - TRUE loads PV → CV
 * @param PV Preset value (INT) - starting count
 * @param instance Pointer to counter instance storage
 * @return Q output (BOOL) - TRUE when CV <= 0
 *
 * @example
 *   empty := CTD(pulse, load, 50);  (* Count down from 50 *)
 */
st_value_t st_builtin_ctd(st_value_t CD, st_value_t LOAD, st_value_t PV, st_counter_instance_t* instance);

/**
 * @brief Count Up/Down Counter (CTUD)
 *
 * Dual counter - increments on CU, decrements on CD.
 * QU goes TRUE when CV >= PV.
 * QD goes TRUE when CV <= 0.
 *
 * @param CU Count-up input (BOOL)
 * @param CD Count-down input (BOOL)
 * @param RESET Reset input (BOOL) - TRUE resets CV to 0
 * @param LOAD Load input (BOOL) - TRUE loads PV → CV
 * @param PV Preset value (INT)
 * @param instance Pointer to counter instance storage
 * @return Q output (BOOL) - QU (up done) as primary return
 *
 * Note: Access QD via instance->QD
 *
 * @example
 *   up_done := CTUD(inc_pulse, dec_pulse, reset, load, 100);
 */
st_value_t st_builtin_ctud(st_value_t CU, st_value_t CD, st_value_t RESET, st_value_t LOAD, st_value_t PV, st_counter_instance_t* instance);

/**
 * @brief Get current count value
 *
 * Returns CV (Current Value) for monitoring/display.
 *
 * @param instance Pointer to counter instance
 * @return Current count value
 */
int32_t st_counter_get_cv(st_counter_instance_t* instance);

#endif // ST_BUILTIN_COUNTERS_H
