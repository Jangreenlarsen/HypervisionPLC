// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file config_load.h
 * @brief Configuration persistence - load from NVS (LAYER 6)
 */

#ifndef config_load_H
#define config_load_H

#include <stdint.h>
#include "types.h"

bool config_load_from_nvs(PersistConfig* out);

#endif // config_load_H
