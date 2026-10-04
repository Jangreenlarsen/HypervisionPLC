// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file debug_flags.h
 * @brief Runtime debug flag management
 */

#ifndef DEBUG_FLAGS_H
#define DEBUG_FLAGS_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get debug flags structure
 */
DebugFlags* debug_flags_get(void);

/**
 * @brief Enable/disable config save debug
 */
void debug_flags_set_config_save(uint8_t enabled);

/**
 * @brief Enable/disable config load debug
 */
void debug_flags_set_config_load(uint8_t enabled);

/**
 * @brief Enable/disable WiFi connection debug (network_manager, wifi_driver)
 */
void debug_flags_set_wifi_connect(uint8_t enabled);

/**
 * @brief Enable/disable network config validation debug
 */
void debug_flags_set_network_validate(uint8_t enabled);

/**
 * @brief Enable/disable HTTP server debug
 */
void debug_flags_set_http_server(uint8_t enabled);

/**
 * @brief Enable/disable HTTP API debug
 */
void debug_flags_set_http_api(uint8_t enabled);

/**
 * @brief Enable/disable all debug flags
 */
void debug_flags_set_all(uint8_t enabled);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_FLAGS_H
