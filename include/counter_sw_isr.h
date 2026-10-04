// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_sw_isr.h
 * @brief Software ISR mode counter (LAYER 5)
 *
 * LAYER 5: Feature Engines - Counter SW-ISR Mode
 * Responsibility: Interrupt-driven edge detection ONLY
 *
 * This file handles:
 * - GPIO interrupt attachment/detachment
 * - ISR event counting (triggered by GPIO edge)
 * - Debounce filtering at interrupt level
 * - Value counting and overflow detection
 *
 * Does NOT handle:
 * - Polling-based counting (→ counter_sw.h)
 * - Hardware timer counting (→ counter_hw.h)
 * - Frequency measurement (→ counter_frequency.h)
 * - Prescaler division (→ counter_engine.h)
 *
 * Context: ISR mode suitable for medium-frequency signals (~20 kHz)
 * Uses ESP32 GPIO interrupt hardware for deterministic edge detection
 */

#ifndef COUNTER_SW_ISR_H
#define COUNTER_SW_ISR_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Initialize ISR counter mode
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_init(uint8_t id);

/**
 * @brief Attach interrupt to GPIO pin for counter
 * @param id Counter ID (1-4)
 * @param gpio_pin GPIO pin number (e.g., 0-39 on ESP32)
 */
void counter_sw_isr_attach(uint8_t id, uint8_t gpio_pin);

/**
 * @brief Detach interrupt from counter
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_detach(uint8_t id);

/**
 * @brief Process any pending debounce timers (call from main loop)
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_loop(uint8_t id);

/**
 * @brief Reset ISR counter to start value
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_reset(uint8_t id);

/**
 * @brief Get current counter value from ISR mode
 * @param id Counter ID (1-4)
 * @return Current counter value
 */
uint64_t counter_sw_isr_get_value(uint8_t id);

/**
 * @brief Set counter value (for testing/reset)
 * @param id Counter ID (1-4)
 * @param value Value to set
 */
void counter_sw_isr_set_value(uint8_t id, uint64_t value);

/**
 * @brief Get overflow flag
 * @param id Counter ID (1-4)
 * @return 1 if overflow, 0 otherwise
 */
uint8_t counter_sw_isr_get_overflow(uint8_t id);

/**
 * @brief Clear overflow flag
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_clear_overflow(uint8_t id);

/**
 * @brief Start counter (enable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_start(uint8_t id);  // BUG FIX 2.1

/**
 * @brief Stop counter (disable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_sw_isr_stop(uint8_t id);  // BUG FIX 2.1

#endif // COUNTER_SW_ISR_H

