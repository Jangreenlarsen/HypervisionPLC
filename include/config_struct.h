// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file config_struct.h
 * @brief Configuration struct utilities
 *
 * Contains the global g_persist_config structure that holds all persistent
 * configuration including registers, coils, counters, and timers.
 */

#ifndef config_struct_H
#define config_struct_H

#include "types.h"

// Global persistent configuration (defined in config_struct.cpp)
extern PersistConfig g_persist_config;

/**
 * @brief Create default configuration structure
 * @return Pointer to global g_persist_config
 */
PersistConfig* config_struct_create_default(void);

#endif // config_struct_H
