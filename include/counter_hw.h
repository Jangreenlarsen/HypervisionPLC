// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_hw.h
 * @brief Hardware PCNT mode counter (LAYER 5)
 *
 * LAYER 5: Feature Engines - Counter HW Mode
 * Responsibility: Hardware PCNT pulse counting ONLY
 *
 * This file handles:
 * - PCNT (Pulse Counter) unit configuration (ESP32 native hardware)
 * - Direct pulse counting from GPIO via PCNT
 * - Overflow detection via PCNT ISR
 * - Value accumulation and auto-reset
 *
 * Does NOT handle:
 * - Polling-based counting (→ counter_sw.h)
 * - Interrupt-based counting (→ counter_sw_isr.h)
 * - Frequency measurement (→ counter_frequency.h)
 * - Prescaler division (→ counter_engine.h)
 *
 * Context: HW mode suitable for high-frequency signals (~20 kHz)
 * Uses ESP32 PCNT peripheral for maximum precision and efficiency
 *
 * Hardware mapping (ESP32-WROOM-32):
 * - PCNT unit 0: GPIO19 (typical)
 * - PCNT unit 1: GPIO25 (typical)
 * - PCNT unit 2: GPIO27 (typical)
 * - PCNT unit 3: GPIO33 (typical)
 * Configurable via gpio_driver
 */

#ifndef COUNTER_HW_H
#define COUNTER_HW_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Initialize hardware PCNT counter mode
 * @param id Counter ID (1-4)
 */
void counter_hw_init(uint8_t id);

/**
 * @brief Configure PCNT unit for counter
 * @param id Counter ID (1-4)
 * @param gpio_pin GPIO pin to connect to PCNT
 * @return true if successful
 */
bool counter_hw_configure(uint8_t id, uint8_t gpio_pin);

/**
 * @brief Process hardware counter (call from main loop for timing-based operations)
 * @param id Counter ID (1-4)
 */
void counter_hw_loop(uint8_t id);

/**
 * @brief Reset hardware counter to start value
 * @param id Counter ID (1-4)
 */
void counter_hw_reset(uint8_t id);

/**
 * @brief Get current counter value from PCNT hardware
 * @param id Counter ID (1-4)
 * @return Current pulse count from PCNT
 */
uint64_t counter_hw_get_value(uint8_t id);

/**
 * @brief Set counter value (initialize PCNT to value)
 * @param id Counter ID (1-4)
 * @param value Value to set
 */
void counter_hw_set_value(uint8_t id, uint64_t value);

/**
 * @brief Start counter (enable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_hw_start(uint8_t id);  // BUG FIX 2.1

/**
 * @brief Stop counter (disable counting runtime)
 * @param id Counter ID (1-4)
 */
void counter_hw_stop(uint8_t id);  // BUG FIX 2.1

/**
 * @brief Get overflow flag
 * @param id Counter ID (1-4)
 * @return 1 if overflow occurred, 0 otherwise
 */
uint8_t counter_hw_get_overflow(uint8_t id);

/**
 * @brief Clear overflow flag
 * @param id Counter ID (1-4)
 */
void counter_hw_clear_overflow(uint8_t id);

#endif // COUNTER_HW_H

