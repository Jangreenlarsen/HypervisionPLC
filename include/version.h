// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file version.h
 * @brief Version information and changelog
 *
 * NOTE: This file is deprecated. Use include/constants.h for PROJECT_VERSION
 */

#ifndef version_H
#define version_H

// Auto-generated build information (updated by generate_build_info.py)
#include "build_version.h"
#include "constants.h"  // Get PROJECT_VERSION from single source of truth

// For backward compatibility (deprecated - use constants.h instead)
#define PROJECT_BUILD_DATE __DATE__
#define PROJECT_NAME "Modbus RTU Server (ESP32)"

void version_print_changelog(void);

#endif // version_H
