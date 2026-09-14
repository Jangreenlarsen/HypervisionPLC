/**
 * @file st_builtin_signal.h
 * @brief ST Signal Processing Functions
 *
 * Signal processing and conditioning functions for Structured Text.
 *
 * Functions:
 * - SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX) : REAL - Linear scaling/mapping
 * - HYSTERESIS(IN, HIGH, LOW) : BOOL - Schmitt trigger with hysteresis
 * - BLINK(ENABLE, ON_TIME, OFF_TIME) : BOOL - Periodic blink/pulse generator
 * - FILTER(IN, TIME_CONSTANT) : REAL - First-order low-pass filter
 *
 * Usage:
 *   VAR
 *     adc_raw : REAL;
 *     pressure_bar : REAL;
 *   END_VAR
 *
 *   (* Scale 0-4095 ADC to 0-10 bar *)
 *   pressure_bar := SCALE(adc_raw, 0.0, 4095.0, 0.0, 10.0);
 */

#ifndef ST_BUILTIN_SIGNAL_H
#define ST_BUILTIN_SIGNAL_H

#include "st_types.h"
#include "st_stateful.h"

/* ============================================================================
 * STATELESS FUNCTIONS
 * ============================================================================ */

/**
 * @brief Linear scaling/mapping
 *
 * Maps input range [in_min, in_max] to output range [out_min, out_max].
 * Input is clamped to input range before scaling.
 *
 * Formula: OUT = (IN - IN_MIN) / (IN_MAX - IN_MIN) * (OUT_MAX - OUT_MIN) + OUT_MIN
 *
 * @param in Input value (REAL)
 * @param in_min Input range minimum (REAL)
 * @param in_max Input range maximum (REAL)
 * @param out_min Output range minimum (REAL)
 * @param out_max Output range maximum (REAL)
 * @return Scaled output value (REAL)
 *
 * @example
 *   pressure_bar := SCALE(adc_raw, 0.0, 4095.0, 0.0, 10.0);
 *   (* Maps 0-4095 ADC to 0-10 bar *)
 *
 * @note Division by zero protection: returns 0.0 if in_max == in_min (BUG-161 FIX,
 *       see .cpp — this comment previously said "returns out_min", which never matched the implementation
 */
st_value_t st_builtin_scale(st_value_t in, st_value_t in_min, st_value_t in_max,
                             st_value_t out_min, st_value_t out_max);

/* ============================================================================
 * STATEFUL FUNCTIONS
 * ============================================================================ */

/**
 * @brief Schmitt trigger with hysteresis
 *
 * Provides noise immunity by requiring input to cross HIGH threshold
 * to turn ON, and LOW threshold to turn OFF. Dead zone between thresholds
 * holds previous output state.
 *
 * Truth table:
 *   IN > HIGH  → Q = TRUE
 *   IN < LOW   → Q = FALSE
 *   LOW ≤ IN ≤ HIGH → Q holds previous state
 *
 * @param in Input signal (REAL)
 * @param high Upper threshold - switch-on point (REAL)
 * @param low Lower threshold - switch-off point (REAL)
 * @param instance Pointer to hysteresis instance storage
 * @return Q output (BOOL)
 *
 * @example
 *   heater_on := HYSTERESIS(temperature, 22.0, 18.0);
 *   (* Turn ON at 22°C, OFF at 18°C *)
 *
 * @note Requires stateful storage for Q state
 */
st_value_t st_builtin_hysteresis(st_value_t in, st_value_t high, st_value_t low,
                                  st_hysteresis_instance_t* instance);

/**
 * @brief Periodic blink/pulse generator
 *
 * Generates periodic ON/OFF signal with configurable durations.
 * Only active when ENABLE is TRUE.
 *
 * State machine:
 *   IDLE → ON_PHASE → OFF_PHASE → ON_PHASE → ...
 *
 * @param enable Enable blink generator (BOOL)
 * @param on_time ON duration in milliseconds (INT)
 * @param off_time OFF duration in milliseconds (INT)
 * @param instance Pointer to blink instance storage
 * @return Q output (BOOL)
 *
 * @example
 *   led_blink := BLINK(system_active, 500, 500);
 *   (* Blink LED: 500ms ON, 500ms OFF *)
 *
 * @note Requires stateful storage for state machine
 */
st_value_t st_builtin_blink(st_value_t enable, st_value_t on_time, st_value_t off_time,
                             st_blink_instance_t* instance);

/**
 * @brief First-order low-pass filter
 *
 * Smooths noisy signals using exponential moving average.
 * Formula: OUT = OUT_prev + α * (IN - OUT_prev)
 * where α = DT / (TIME_CONSTANT + DT)
 *
 * Cutoff frequency: fc = 1 / (2π * τ)
 *
 * @param in Input signal (REAL)
 * @param time_constant Filter time constant in milliseconds (INT)
 * @param instance Pointer to filter instance storage
 * @param cycle_time_ms Execution cycle time in milliseconds (BUG-153 fix)
 * @return Filtered output (REAL)
 *
 * @example
 *   smooth_sensor := FILTER(raw_sensor, 500);
 *   (* 500ms time constant → ~0.3 Hz cutoff *)
 *
 * @note Requires stateful storage for previous output
 * @note cycle_time_ms should match actual program execution interval
 */
st_value_t st_builtin_filter(st_value_t in, st_value_t time_constant,
                              st_filter_instance_t* instance, uint32_t cycle_time_ms);

#endif // ST_BUILTIN_SIGNAL_H
