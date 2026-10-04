// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file pcnt_driver.h
 * @brief PCNT (Pulse Counter) hardware driver
 *
 * LAYER 0: Hardware Abstraction Driver
 * ESP32-specific pulse counter (PCNT) support
 */

#ifndef PCNT_DRIVER_H
#define PCNT_DRIVER_H

#include <stdint.h>

/* PCNT edge modes */
typedef enum {
  PCNT_EDGE_DISABLE = 0,
  PCNT_EDGE_RISING = 1,
  PCNT_EDGE_FALLING = 2
} pcnt_edge_mode_t;

/**
 * @brief Initialize PCNT unit
 * @param unit PCNT unit (0-3)
 */
void pcnt_unit_init(uint8_t unit);

/**
 * @brief Configure PCNT unit
 * @param unit PCNT unit (0-3)
 * @param gpio_pin GPIO pin to monitor
 * @param pos_edge Positive edge mode (rising)
 * @param neg_edge Negative edge mode (falling)
 */
void pcnt_unit_configure(uint8_t unit, uint8_t gpio_pin,
                        pcnt_edge_mode_t pos_edge, pcnt_edge_mode_t neg_edge);

/**
 * @brief Get current PCNT count
 * @param unit PCNT unit (0-3)
 * @return Current count value (NOTE: PCNT hardware is int16_t, range -32768 to +32767)
 *
 * IMPORTANT: ESP32 PCNT hardware counter is 16-bit SIGNED (-32768 to +32767).
 * This function returns uint32_t for interface compatibility, but the actual
 * hardware value wraps at ±32768. Callers must handle signed wrap-around.
 *
 * At high frequencies (e.g., 20 kHz), PCNT will wrap every ~1.6 seconds.
 * Use software delta accumulation to track total count beyond 16-bit range.
 */
uint32_t pcnt_unit_get_count(uint8_t unit);

/**
 * @brief Clear PCNT counter
 * @param unit PCNT unit (0-3)
 */
void pcnt_unit_clear(uint8_t unit);

/**
 * @brief Set PCNT counter value
 * @param unit PCNT unit (0-3)
 * @param value Value to set
 */
void pcnt_unit_set_count(uint8_t unit, uint32_t value);

#endif // PCNT_DRIVER_H
