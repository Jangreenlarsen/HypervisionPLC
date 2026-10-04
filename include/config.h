// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file config.h
 * @brief Build-time configuration
 */

#ifndef CONFIG_H
#define CONFIG_H

// Feature flags
#define ENABLE_DEBUG_OUTPUT     1
#define ENABLE_FREQUENCY_MEAS   1
#define ENABLE_DEBOUNCE         1

// Hardware-specific
#define ESP32_WROOM_32          1

#endif // CONFIG_H
