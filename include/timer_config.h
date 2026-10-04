// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file timer_config.h
 * @brief Timer configuration and validation (LAYER 5)
 *
 * LAYER 5: Feature Engines - Timer Configuration
 * Responsibility: TimerConfig struct management, defaults, validation
 *
 * This file handles:
 * - TimerConfig struct storage and access
 * - Configuration validation
 * - Default value generation
 * - Sanitization of config values
 *
 * Does NOT handle:
 * - Timer state machine (→ timer_engine.cpp)
 * - Timing logic (→ timer_engine.cpp)
 */

#ifndef TIMER_CONFIG_H
#define TIMER_CONFIG_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * TIMER CONFIGURATION DEFAULTS
 * ============================================================================ */

#define MAX_TIMERS 4

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize all timers to default/disabled state
 */
void timer_config_init(void);

/**
 * @brief Get default configuration for a timer
 * @param id Timer ID (1-4)
 * @return Default TimerConfig
 */
TimerConfig timer_config_defaults(uint8_t id);

/**
 * @brief Validate timer configuration
 * @param cfg Configuration to validate
 * @return true if valid, false otherwise
 */
bool timer_config_validate(const TimerConfig* cfg);

/**
 * @brief Sanitize timer configuration (fix invalid values)
 * @param cfg Configuration to sanitize (modified in place)
 */
void timer_config_sanitize(TimerConfig* cfg);

/**
 * @brief Get timer configuration by ID
 * @param id Timer ID (1-4)
 * @param out Output configuration
 * @return true if found, false otherwise
 */
bool timer_config_get(uint8_t id, TimerConfig* out);

/**
 * @brief Set timer configuration by ID
 * @param id Timer ID (1-4)
 * @param cfg Configuration to set
 * @return true if successful, false otherwise
 */
bool timer_config_set(uint8_t id, const TimerConfig* cfg);

/**
 * @brief Get all timer configurations
 * @return Pointer to internal timer array
 */
TimerConfig* timer_config_get_all(void);

#endif // TIMER_CONFIG_H

