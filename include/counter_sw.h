// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_sw.h
 * @brief Software polling mode counter (LAYER 5)
 *
 * LAYER 5: Feature Engines - Counter SW Mode
 * Responsibility: Software polling-based edge detection ONLY
 *
 * This file handles:
 * - GPIO polling via discrete inputs
 * - Edge detection (rising/falling/both)
 * - Debounce filtering
 * - Value counting and overflow detection
 *
 * Does NOT handle:
 * - Interrupt-based counting (→ counter_sw_isr.h)
 * - Hardware timer counting (→ counter_hw.h)
 * - Frequency measurement (→ counter_frequency.h)
 * - Prescaler division (→ counter_engine.h)
 *
 * Context: Software polling is suitable for low-frequency signals (~500 Hz max)
 */

#ifndef COUNTER_SW_H
#define COUNTER_SW_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Initialize software counter mode
 * @param id Counter ID (1-4)
 */
void counter_sw_init(uint8_t id);

/**
 * @brief Process software counter in polling mode
 * @param id Counter ID (1-4)
 */
void counter_sw_loop(uint8_t id);

/**
 * @brief Reset software counter to start value
 * @param id Counter ID (1-4)
 */
void counter_sw_reset(uint8_t id);

/**
 * @brief Get current counter value from software mode
 * @param id Counter ID (1-4)
 * @return Current counter value
 */
uint64_t counter_sw_get_value(uint8_t id);

/**
 * @brief Set counter value (for testing/reset)
 * @param id Counter ID (1-4)
 * @param value Value to set
 */
void counter_sw_set_value(uint8_t id, uint64_t value);

/**
 * @brief Get overflow flag
 * @param id Counter ID (1-4)
 * @return 1 if overflow, 0 otherwise
 */
uint8_t counter_sw_get_overflow(uint8_t id);

/**
 * @brief Clear overflow flag
 * @param id Counter ID (1-4)
 */
void counter_sw_clear_overflow(uint8_t id);

/**
 * @brief Start counter (enable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_sw_start(uint8_t id);  // BUG FIX 2.1

/**
 * @brief Stop counter (disable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_sw_stop(uint8_t id);  // BUG FIX 2.1

#endif // COUNTER_SW_H

