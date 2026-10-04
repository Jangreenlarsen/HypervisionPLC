// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file counter_config.h
 * @brief Counter configuration and validation
 *
 * LAYER 5: Feature Engines - Counter Configuration
 * Responsibility: CounterConfig struct management, defaults, and validation only
 *
 * This file handles:
 * - CounterConfig struct validation
 * - Default configuration generation
 * - Sanitization of config values
 *
 * Does NOT handle:
 * - Counter state machine (→ counter_engine.cpp)
 * - Mode-specific logic (→ counter_sw/isr/hw.cpp)
 * - Frequency measurement (→ counter_frequency.cpp)
 */

#ifndef COUNTER_CONFIG_H
#define COUNTER_CONFIG_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * COUNTER CONFIGURATION DEFAULTS
 * ============================================================================ */

#define MAX_COUNTERS 4

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize all counters to default/disabled state
 */
void counter_config_init(void);

/**
 * @brief Get default configuration for a counter
 * @param id Counter ID (1-4)
 * @return Default CounterConfig
 */
CounterConfig counter_config_defaults(uint8_t id);

/**
 * @brief Validate counter configuration
 * @param cfg Configuration to validate
 * @return true if valid, false otherwise
 */
bool counter_config_validate(const CounterConfig* cfg);

/**
 * @brief Sanitize counter configuration (fix invalid values)
 * @param cfg Configuration to sanitize (modified in place)
 */
void counter_config_sanitize(CounterConfig* cfg);

/**
 * @brief Get counter configuration by ID
 * @param id Counter ID (1-4)
 * @param out Output configuration
 * @return true if found, false otherwise
 */
bool counter_config_get(uint8_t id, CounterConfig* out);

/**
 * @brief Set counter configuration by ID
 * @param id Counter ID (1-4)
 * @param cfg Configuration to set
 * @return true if successful, false otherwise
 */
bool counter_config_set(uint8_t id, const CounterConfig* cfg);

/**
 * @brief Get all counter configurations
 * @return Pointer to internal counter array
 */
CounterConfig* counter_config_get_all(void);

#endif // COUNTER_CONFIG_H
