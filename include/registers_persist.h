// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file registers_persist.h
 * @brief Persistent register group management (v4.0+)
 *
 * LAYER 4: Register Persistence
 * Responsibility: Manage named groups of registers for NVS persistence
 *
 * Features:
 * - Up to 8 named groups (e.g., "sensors", "setpoints")
 * - Each group can contain up to 16 holding registers
 * - Save/restore specific groups or all groups
 * - Used by ST Logic SAVE()/LOAD() functions and CLI
 */

#ifndef REGISTERS_PERSIST_H
#define REGISTERS_PERSIST_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize persistence system
 * @param data Pointer to PersistentRegisterData in PersistConfig
 */
void registers_persist_init(PersistentRegisterData* data);

/* ============================================================================
 * GROUP MANAGEMENT
 * ============================================================================ */

/**
 * @brief Create a new persistence group
 * @param group_name Group name (max 15 chars, null-terminated)
 * @return true if created, false if group already exists or max groups reached
 */
bool registers_persist_group_create(const char* group_name);

/**
 * @brief Delete a persistence group
 * @param group_name Group name to delete
 * @return true if deleted, false if not found
 */
bool registers_persist_group_delete(const char* group_name);

/**
 * @brief Find a group by name
 * @param group_name Group name to find
 * @return Pointer to PersistGroup or NULL if not found
 */
PersistGroup* registers_persist_group_find(const char* group_name);

/**
 * @brief Get number of active groups
 * @return Number of groups (0-8)
 */
uint8_t registers_persist_get_group_count(void);

/* ============================================================================
 * REGISTER MANAGEMENT (within groups)
 * ============================================================================ */

/**
 * @brief Add register to a group
 * @param group_name Group name
 * @param reg_addr Register address (0-255)
 * @return true if added, false if group full or not found
 */
bool registers_persist_group_add_reg(const char* group_name, uint16_t reg_addr);

/**
 * @brief Remove register from a group
 * @param group_name Group name
 * @param reg_addr Register address to remove
 * @return true if removed, false if not found
 */
bool registers_persist_group_remove_reg(const char* group_name, uint16_t reg_addr);

/**
 * @brief Check if register exists in a group
 * @param group_name Group name
 * @param reg_addr Register address
 * @return true if register is in group, false otherwise
 */
bool registers_persist_group_has_reg(const char* group_name, uint16_t reg_addr);

/* ============================================================================
 * SAVE/RESTORE OPERATIONS
 * ============================================================================ */

/**
 * @brief Save specific group (snapshot current register values)
 * @param group_name Group name to save
 * @return true if saved, false if group not found
 *
 * This function copies current holding register values into the group's
 * reg_values[] array. Call config_save_to_nvs() afterwards to persist to flash.
 */
bool registers_persist_group_save(const char* group_name);

/**
 * @brief Save group by ID (snapshot current register values)
 * @param group_id Group ID (1-8), or 0 for all groups
 * @return true if saved, false if group not found
 *
 * Used by ST Logic SAVE(id) function.
 */
bool registers_persist_group_save_by_id(uint8_t group_id);

/**
 * @brief Save all groups (snapshot current register values)
 * @return true if successful
 *
 * Saves all active groups. Call config_save_to_nvs() afterwards to persist.
 */
bool registers_persist_save_all_groups(void);

/**
 * @brief Restore specific group (write values to holding registers)
 * @param group_name Group name to restore
 * @return true if restored, false if group not found
 *
 * This function writes the saved register values from the group's
 * reg_values[] array back to holding_regs[].
 */
bool registers_persist_group_restore(const char* group_name);

/**
 * @brief Restore group by ID (write values to holding registers)
 * @param group_id Group ID (1-8), or 0 for all groups
 * @return true if restored, false if group not found
 *
 * Used by ST Logic LOAD(id) function.
 */
bool registers_persist_group_restore_by_id(uint8_t group_id);

/**
 * @brief Restore all groups (write values to holding registers)
 * @return true if successful
 *
 * Restores all active groups from their saved values.
 */
bool registers_persist_restore_all_groups(void);

/* ============================================================================
 * AUTO-LOAD ON BOOT (v4.3.0)
 * ============================================================================ */

/**
 * @brief Enable/disable auto-load on boot
 * @param enabled true to enable, false to disable
 */
void registers_persist_set_auto_load_enabled(bool enabled);

/**
 * @brief Check if auto-load is enabled
 * @return true if enabled, false otherwise
 */
bool registers_persist_is_auto_load_enabled(void);

/**
 * @brief Add group to auto-load list
 * @param group_id Group ID (1-8)
 * @return true if added, false if already in list or list full
 */
bool registers_persist_auto_load_add_group(uint8_t group_id);

/**
 * @brief Remove group from auto-load list
 * @param group_id Group ID (1-8)
 * @return true if removed, false if not found
 */
bool registers_persist_auto_load_remove_group(uint8_t group_id);

/**
 * @brief Execute auto-load (restore configured groups from NVS)
 * @return Number of groups successfully restored
 *
 * Called during boot sequence if auto_load_enabled is true.
 */
uint8_t registers_persist_auto_load_execute(void);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * @brief List all groups (for CLI "show persist")
 *
 * Prints all active groups with their register counts to console.
 */
void registers_persist_list_groups(void);

/**
 * @brief Enable/disable persistence system
 * @param enabled true to enable, false to disable
 */
void registers_persist_set_enabled(bool enabled);

/**
 * @brief Check if persistence system is enabled
 * @return true if enabled, false otherwise
 */
bool registers_persist_is_enabled(void);

#endif // REGISTERS_PERSIST_H
