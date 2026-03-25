/**
 * @file timer_engine.h
 * @brief Timer orchestration and state machine (LAYER 5)
 *
 * LAYER 5: Feature Engines - Timer Engine
 * Responsibility: Timer state machines, mode dispatching, coil control
 *
 * This file handles:
 * - Timer initialization for all 4 modes
 * - Main state machine loop (call once per iteration)
 * - Mode 1: One-shot (3-phase: P1(T1) → P2(T2) → P3(T3))
 * - Mode 2: Monostable (retriggerable pulse: P2(T1) → P1)
 * - Mode 3: Astable (blink/oscillate: P1(T1) ↔ P2(T2))
 * - Mode 4: Input-triggered (responds to discrete input edge, runs subMode)
 * - Coil write callbacks (triggers monostable/astable)
 * - Alarm/timeout detection
 *
 * TIMING MODEL:
 * - Uses millis() for timing (same as Mega)
 * - Each timer tracks: phase, phase_start_ms, active flag
 * - Timeout = 5× (T1+T2+T3) for safety
 * - Alarm flag set on timeout
 *
 * Context: This is the "glue" between timers and Modbus coils
 */

#ifndef TIMER_ENGINE_H
#define TIMER_ENGINE_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize timer engine (all 4 timers to default/disabled)
 */
void timer_engine_init(void);

/**
 * @brief Main timer engine loop (call once per main loop iteration)
 * Processes all active timers, updates coils, detects timeouts
 */
void timer_engine_loop(void);

/**
 * @brief Configure a timer
 * @param id Timer ID (1-4)
 * @param cfg Configuration to apply
 * @return true if successful
 */
bool timer_engine_configure(uint8_t id, const TimerConfig* cfg);

/**
 * @brief Handle coil write (triggers monostable/astable timers)
 * Called from Modbus FC5/FC15 when a coil is written
 * @param coil_idx Coil index (0-NUM_COILS-1)
 * @param value Coil value (0 or 1)
 */
void timer_engine_on_coil_write(uint16_t coil_idx, uint8_t value);

/**
 * @brief Check if a coil is controlled by a timer
 * @param coil_idx Coil index
 * @return true if any timer controls this coil
 */
bool timer_engine_has_coil(uint16_t coil_idx);

/**
 * @brief Disable all timers
 */
void timer_engine_disable_all(void);

/**
 * @brief Reset alarms (clear alarm flags for all timers)
 */
void timer_engine_clear_alarms(void);

/**
 * @brief Get timer configuration
 * @param id Timer ID (1-4)
 * @param out Output configuration
 * @return true if found
 */
bool timer_engine_get_config(uint8_t id, TimerConfig* out);

/**
 * @brief Get timer runtime state (phase, active flag)
 * @param id Timer ID (1-4)
 * @param out_phase Current phase (0-3)
 * @param out_active Is timer active (0/1)
 * @return true if valid ID
 */
bool timer_engine_get_runtime(uint8_t id, uint8_t* out_phase, uint8_t* out_active);

#endif // TIMER_ENGINE_H

