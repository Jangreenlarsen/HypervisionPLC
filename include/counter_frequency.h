// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_frequency.h
 * @brief Frequency measurement for counters (LAYER 5)
 *
 * LAYER 5: Feature Engines - Frequency Measurement
 * Responsibility: Hz measurement and validation ONLY
 *
 * This file handles:
 * - Periodic frequency calculation (once per second)
 * - Delta count validation (detects overflow/underflow)
 * - Timing window validation (1-2 second windows)
 * - Result clamping (0-20000 Hz range)
 * - Reset on timeout/overflow
 *
 * Does NOT handle:
 * - Counter state machine (→ counter_engine.h)
 * - Prescaler division (→ counter_engine.h)
 * - Mode-specific counting (→ counter_sw/isr/hw.h)
 *
 * Context: Frequency measurement works identically across all counter modes
 * Delta-based approach: Hz = (count_delta) / (time_delta_seconds)
 * Updated: ~1 second timing windows for stability
 */

#ifndef COUNTER_FREQUENCY_H
#define COUNTER_FREQUENCY_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Initialize frequency measurement for counter
 * @param id Counter ID (1-4)
 */
void counter_frequency_init(uint8_t id);

/**
 * @brief Update frequency measurement (call from counter_engine loop)
 * @param id Counter ID (1-4)
 * @param current_value Current counter value (from any mode)
 * @return Measured frequency in Hz (0-20000), or 0 if not ready
 */
uint16_t counter_frequency_update(uint8_t id, uint64_t current_value);

/**
 * @brief Get last measured frequency
 * @param id Counter ID (1-4)
 * @return Last measured frequency in Hz
 */
uint16_t counter_frequency_get(uint8_t id);

/**
 * @brief Reset frequency measurement (when counter resets)
 * @param id Counter ID (1-4)
 */
void counter_frequency_reset(uint8_t id);

/**
 * @brief Get frequency measurement state (debugging)
 * @param id Counter ID (1-4)
 * @param out_hz Output: current Hz
 * @param out_window Output: sampling window in ms
 * @return true if frequency is valid/fresh
 */
bool counter_frequency_is_valid(uint8_t id, uint16_t* out_hz, uint32_t* out_window);

#endif // COUNTER_FREQUENCY_H

