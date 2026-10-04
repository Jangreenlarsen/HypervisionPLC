// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_builtin_persist.h
 * @brief ST Logic persistence built-in functions (v4.0+)
 *
 * Implements SAVE() and LOAD() functions for ST Logic programs.
 *
 * Usage from ST code:
 *   SAVE();  (* Save all persistent register groups to NVS *)
 *   LOAD();  (* Restore all groups from NVS *)
 *
 * Note: In v4.0, SAVE() and LOAD() save/restore ALL groups.
 * Future versions may support SAVE("group_name") syntax.
 */

#ifndef ST_BUILTIN_PERSIST_H
#define ST_BUILTIN_PERSIST_H

#include "st_types.h"

/**
 * @brief SAVE(id) - Save persistent register groups to NVS
 *
 * @param group_id Group ID (0 = all, 1-8 = specific group)
 * @return 0 on success, -1 on failure, -2 if rate limited
 *
 * Rate limiting: Max 1 save per 5 seconds to protect flash wear.
 *
 * Steps:
 * 1. Check rate limit (max 1 save / 5 seconds)
 * 2. Snapshot group(s) register values (registers_persist_save_by_id)
 * 3. Write entire config to NVS (config_save_to_nvs)
 * 4. Return status
 *
 * Usage from ST Logic:
 *   SAVE(0);  - Save all groups
 *   SAVE(1);  - Save group #1
 */
st_value_t st_builtin_persist_save(st_value_t group_id_arg);

/**
 * @brief LOAD(id) - Restore persistent register groups from NVS
 *
 * @param group_id Group ID (0 = all, 1-8 = specific group)
 * @return 0 on success, -1 on failure
 *
 * Steps:
 * 1. Read config from NVS (config_load_from_nvs)
 * 2. Restore group(s) register values (registers_persist_restore_by_id)
 * 3. Return status
 *
 * Usage from ST Logic:
 *   LOAD(0);  - Load all groups
 *   LOAD(1);  - Load group #1
 */
st_value_t st_builtin_persist_load(st_value_t group_id_arg);

#endif // ST_BUILTIN_PERSIST_H
