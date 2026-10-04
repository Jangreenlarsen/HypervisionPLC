// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file config_apply.h
 * @brief Configuration apply - activate loaded config in system (LAYER 6)
 */

#ifndef config_apply_H
#define config_apply_H

#include "types.h"

bool config_apply(const PersistConfig* cfg);

#endif // config_apply_H
