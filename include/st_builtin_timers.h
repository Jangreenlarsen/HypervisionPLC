// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_timers.h
 * @brief ST Timer Functions (TON, TOF, TP)
 *
 * IEC 61131-3 standard timer function blocks.
 *
 * Functions:
 * - TON(IN, PT) : BOOL - On-Delay Timer
 * - TOF(IN, PT) : BOOL - Off-Delay Timer
 * - TP(IN, PT) : BOOL  - Pulse Timer
 *
 * All timers return Q (output) and track ET (elapsed time).
 *
 * Usage:
 *   VAR
 *     motor_running : BOOL;
 *   END_VAR
 *
 *   motor_running := TON(start_button, 5000);  (* 5 second delay *)
 *
 * Note: PT (Preset Time) is in milliseconds.
 */

#ifndef ST_BUILTIN_TIMERS_H
#define ST_BUILTIN_TIMERS_H

#include "st_types.h"
#include "st_stateful.h"
#include <Arduino.h>  // For millis()

/* ============================================================================
 * TIMER FUNCTIONS
 * ============================================================================ */

/**
 * @brief On-Delay Timer (TON)
 *
 * Output Q goes TRUE after input IN has been TRUE for PT milliseconds.
 * Output Q goes FALSE immediately when IN goes FALSE.
 *
 * Timing diagram:
 *   IN:  ──┐         ┌──
 *          └─────────┘
 *   Q:   ────┐     ┌────
 *            └─────┘
 *          <--PT-->
 *
 * @param IN Input signal (BOOL)
 * @param PT Preset time in milliseconds (INT)
 * @param instance Pointer to timer instance storage
 * @return Q output (BOOL) - TRUE after delay, FALSE immediately
 *
 * @example
 *   motor := TON(start_button, 3000);  (* 3 second start delay *)
 */
st_value_t st_builtin_ton(st_value_t IN, st_value_t PT, st_timer_instance_t* instance);

/**
 * @brief Off-Delay Timer (TOF)
 *
 * Output Q goes TRUE immediately when input IN goes TRUE.
 * Output Q goes FALSE after IN has been FALSE for PT milliseconds.
 *
 * Timing diagram:
 *   IN:  ──┐         ┌──
 *          └─────────┘
 *   Q:   ──┐           ┌──
 *          └───────────┘
 *              <--PT-->
 *
 * @param IN Input signal (BOOL)
 * @param PT Preset time in milliseconds (INT)
 * @param instance Pointer to timer instance storage
 * @return Q output (BOOL) - TRUE immediately, FALSE after delay
 *
 * @example
 *   fan := TOF(motor_running, 60000);  (* Fan runs 1 min after motor stops *)
 */
st_value_t st_builtin_tof(st_value_t IN, st_value_t PT, st_timer_instance_t* instance);

/**
 * @brief Pulse Timer (TP)
 *
 * Output Q produces a pulse of PT milliseconds on rising edge of IN.
 * Pulse completes even if IN goes FALSE before PT expires.
 *
 * Timing diagram:
 *   IN:  ──┐   ┌──
 *          └───┘
 *   Q:   ────┐   ┌────
 *            └───┘
 *          <-PT->
 *
 * @param IN Input signal (BOOL)
 * @param PT Preset time in milliseconds (INT)
 * @param instance Pointer to timer instance storage
 * @return Q output (BOOL) - Pulse of PT duration
 *
 * @example
 *   valve_pulse := TP(trigger, 500);  (* 500ms valve pulse *)
 */
st_value_t st_builtin_tp(st_value_t IN, st_value_t PT, st_timer_instance_t* instance);

/**
 * @brief Get elapsed time from timer instance
 *
 * Returns ET (Elapsed Time) in milliseconds for display/monitoring.
 *
 * @param instance Pointer to timer instance
 * @return Elapsed time in milliseconds
 */
uint32_t st_timer_get_et(st_timer_instance_t* instance);

#endif // ST_BUILTIN_TIMERS_H
