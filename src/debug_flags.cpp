// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file debug_flags.cpp
 * @brief Runtime debug flag management
 *
 * Provides global debug flags that can be toggled at runtime
 */

#include "debug_flags.h"

// Global debug flags (default: all disabled for production)
DebugFlags g_debug_flags = {
  .all = 0,
  .config_save = 0,
  .config_load = 0,
  .wifi_connect = 0,
  .network_validate = 0,
  .http_server = 0,
  .http_api = 0,
};

DebugFlags* debug_flags_get(void) {
  return &g_debug_flags;
}

void debug_flags_set_config_save(uint8_t enabled) {
  g_debug_flags.config_save = enabled ? 1 : 0;
}

void debug_flags_set_config_load(uint8_t enabled) {
  g_debug_flags.config_load = enabled ? 1 : 0;
}

void debug_flags_set_wifi_connect(uint8_t enabled) {
  g_debug_flags.wifi_connect = enabled ? 1 : 0;
}

void debug_flags_set_network_validate(uint8_t enabled) {
  g_debug_flags.network_validate = enabled ? 1 : 0;
}

void debug_flags_set_http_server(uint8_t enabled) {
  g_debug_flags.http_server = enabled ? 1 : 0;
}

void debug_flags_set_http_api(uint8_t enabled) {
  g_debug_flags.http_api = enabled ? 1 : 0;
}

void debug_flags_set_all(uint8_t enabled) {
  g_debug_flags.all = enabled ? 1 : 0;
  g_debug_flags.config_save = enabled ? 1 : 0;
  g_debug_flags.config_load = enabled ? 1 : 0;
  g_debug_flags.wifi_connect = enabled ? 1 : 0;
  g_debug_flags.network_validate = enabled ? 1 : 0;
  g_debug_flags.http_server = enabled ? 1 : 0;
  g_debug_flags.http_api = enabled ? 1 : 0;
}
