// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file registers_persist.cpp
 * @brief Persistent register group management implementation
 *
 * LAYER 4: Register Persistence
 */

#include "registers_persist.h"
#include "registers.h"
#include "config_struct.h"
#include "debug.h"
#include <cstring>
#include <Arduino.h>

// External reference to global config
extern PersistConfig g_persist_config;

/* ============================================================================
 * SANITIZATION HELPER (BUG-140)
 * ============================================================================ */

/**
 * @brief Sanitize group_count to prevent out-of-bounds access
 * @param pr Persistence data structure
 * @return Clamped group_count (0-8)
 */
static inline uint8_t get_safe_group_count(PersistentRegisterData* pr) {
  if (pr->group_count > PERSIST_MAX_GROUPS) {
    return PERSIST_MAX_GROUPS;
  }
  return pr->group_count;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void registers_persist_init(PersistentRegisterData* data) {
  if (data == NULL) {
    debug_println("ERROR: registers_persist_init - NULL data");
    return;
  }

  // Already initialized via config_load or defaults
  debug_print("Persistence system initialized: ");
  debug_print_uint(data->group_count);
  debug_print(" groups, enabled=");
  debug_println(data->enabled ? "YES" : "NO");
}

/* ============================================================================
 * GROUP MANAGEMENT
 * ============================================================================ */

bool registers_persist_group_create(const char* group_name) {
  if (group_name == NULL || strlen(group_name) == 0) {
    debug_println("ERROR: Invalid group name");
    return false;
  }

  if (strlen(group_name) >= 16) {
    debug_println("ERROR: Group name too long (max 15 chars)");
    return false;
  }

  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // Check if group already exists
  if (registers_persist_group_find(group_name) != NULL) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_println("' already exists");
    return false;
  }

  // Check if we have space for new group
  if (pr->group_count >= PERSIST_MAX_GROUPS) {
    debug_print("ERROR: Maximum groups (");
    debug_print_uint(PERSIST_MAX_GROUPS);
    debug_println(") reached");
    return false;
  }

  // Create new group
  PersistGroup* grp = &pr->groups[pr->group_count];
  memset(grp, 0, sizeof(PersistGroup));
  strncpy(grp->name, group_name, 15);
  grp->name[15] = '\0';
  grp->reg_count = 0;
  grp->last_save_ms = 0;

  pr->group_count++;

  debug_print("✓ Created group '");
  debug_print(group_name);
  debug_println("'");

  return true;
}

bool registers_persist_group_delete(const char* group_name) {
  if (group_name == NULL) {
    return false;
  }

  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // Find group index (BUG-140: use safe count)
  uint8_t safe_count = get_safe_group_count(pr);
  int8_t idx = -1;
  for (uint8_t i = 0; i < safe_count; i++) {
    if (strcmp(pr->groups[i].name, group_name) == 0) {
      idx = i;
      break;
    }
  }

  if (idx < 0) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_println("' not found");
    return false;
  }

  // Shift all subsequent groups down
  for (uint8_t i = idx; i < safe_count - 1; i++) {
    pr->groups[i] = pr->groups[i + 1];
  }

  // Clear last group slot
  if (safe_count > 0) {
    memset(&pr->groups[safe_count - 1], 0, sizeof(PersistGroup));
  }

  // Decrement count (clamp to max if it was over)
  if (pr->group_count > 0) {
    pr->group_count--;
  }
  if (pr->group_count > PERSIST_MAX_GROUPS) {
    pr->group_count = PERSIST_MAX_GROUPS;
  }

  debug_print("✓ Deleted group '");
  debug_print(group_name);
  debug_println("'");

  return true;
}

PersistGroup* registers_persist_group_find(const char* group_name) {
  if (group_name == NULL) {
    return NULL;
  }

  PersistentRegisterData* pr = &g_persist_config.persist_regs;
  uint8_t safe_count = get_safe_group_count(pr);  // BUG-140

  for (uint8_t i = 0; i < safe_count; i++) {
    if (strcmp(pr->groups[i].name, group_name) == 0) {
      return &pr->groups[i];
    }
  }

  return NULL;
}

uint8_t registers_persist_get_group_count(void) {
  // BUG-140: Return safe count (clamped to max)
  return get_safe_group_count(&g_persist_config.persist_regs);
}

/* ============================================================================
 * REGISTER MANAGEMENT (within groups)
 * ============================================================================ */

bool registers_persist_group_add_reg(const char* group_name, uint16_t reg_addr) {
  if (reg_addr > 255) {
    debug_print("ERROR: Invalid register address ");
    debug_print_uint(reg_addr);
    debug_println(" (must be 0-255)");
    return false;
  }

  PersistGroup* grp = registers_persist_group_find(group_name);
  if (grp == NULL) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_println("' not found");
    return false;
  }

  // Check if register already in group
  for (uint8_t i = 0; i < grp->reg_count; i++) {
    if (grp->reg_addresses[i] == reg_addr) {
      debug_print("Register ");
      debug_print_uint(reg_addr);
      debug_println(" already in group");
      return true;  // Not an error, just idempotent
    }
  }

  // Check if group is full
  if (grp->reg_count >= PERSIST_GROUP_MAX_REGS) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_print("' is full (max ");
    debug_print_uint(PERSIST_GROUP_MAX_REGS);
    debug_println(" registers)");
    return false;
  }

  // Add register
  grp->reg_addresses[grp->reg_count] = reg_addr;
  grp->reg_values[grp->reg_count] = 0;  // Will be populated on save
  grp->reg_count++;

  debug_print("✓ Added register ");
  debug_print_uint(reg_addr);
  debug_print(" to group '");
  debug_print(group_name);
  debug_println("'");

  return true;
}

bool registers_persist_group_remove_reg(const char* group_name, uint16_t reg_addr) {
  PersistGroup* grp = registers_persist_group_find(group_name);
  if (grp == NULL) {
    return false;
  }

  // Find register index
  int8_t idx = -1;
  for (uint8_t i = 0; i < grp->reg_count; i++) {
    if (grp->reg_addresses[i] == reg_addr) {
      idx = i;
      break;
    }
  }

  if (idx < 0) {
    debug_print("Register ");
    debug_print_uint(reg_addr);
    debug_println(" not found in group");
    return false;
  }

  // Shift all subsequent registers down
  for (uint8_t i = idx; i < grp->reg_count - 1; i++) {
    grp->reg_addresses[i] = grp->reg_addresses[i + 1];
    grp->reg_values[i] = grp->reg_values[i + 1];
  }

  grp->reg_count--;

  debug_print("✓ Removed register ");
  debug_print_uint(reg_addr);
  debug_print(" from group '");
  debug_print(group_name);
  debug_println("'");

  return true;
}

bool registers_persist_group_has_reg(const char* group_name, uint16_t reg_addr) {
  PersistGroup* grp = registers_persist_group_find(group_name);
  if (grp == NULL) {
    return false;
  }

  for (uint8_t i = 0; i < grp->reg_count; i++) {
    if (grp->reg_addresses[i] == reg_addr) {
      return true;
    }
  }

  return false;
}

/* ============================================================================
 * SAVE/RESTORE OPERATIONS
 * ============================================================================ */

bool registers_persist_group_save(const char* group_name) {
  PersistGroup* grp = registers_persist_group_find(group_name);
  if (grp == NULL) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_println("' not found");
    return false;
  }

  // Snapshot current register values
  for (uint8_t i = 0; i < grp->reg_count; i++) {
    uint16_t addr = grp->reg_addresses[i];
    uint16_t value = registers_get_holding_register(addr);
    grp->reg_values[i] = value;
  }

  // Update timestamp
  grp->last_save_ms = millis();

  debug_print("✓ Saved group '");
  debug_print(group_name);
  debug_print("' (");
  debug_print_uint(grp->reg_count);
  debug_println(" registers)");

  return true;
}

bool registers_persist_group_save_by_id(uint8_t group_id) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // ID 0 = save all groups
  if (group_id == 0) {
    return registers_persist_save_all_groups();
  }

  // Validate ID (1-8)
  if (group_id < 1 || group_id > PERSIST_MAX_GROUPS) {
    debug_print("ERROR: Invalid group ID ");
    debug_print_uint(group_id);
    debug_println(" (valid: 0-8)");
    return false;
  }

  // Check if group exists
  uint8_t idx = group_id - 1;  // Convert to 0-indexed
  if (idx >= pr->group_count) {
    debug_print("ERROR: Group #");
    debug_print_uint(group_id);
    debug_println(" not found");
    return false;
  }

  // Save by name
  return registers_persist_group_save(pr->groups[idx].name);
}

bool registers_persist_save_all_groups(void) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;
  uint8_t safe_count = get_safe_group_count(pr);  // BUG-140

  if (safe_count == 0) {
    debug_println("No groups to save");
    return true;
  }

  uint8_t saved_count = 0;
  for (uint8_t i = 0; i < safe_count; i++) {
    if (registers_persist_group_save(pr->groups[i].name)) {
      saved_count++;
    }
  }

  debug_print("✓ Saved ");
  debug_print_uint(saved_count);
  debug_print(" of ");
  debug_print_uint(pr->group_count);
  debug_println(" groups");

  return true;
}

bool registers_persist_group_restore(const char* group_name) {
  PersistGroup* grp = registers_persist_group_find(group_name);
  if (grp == NULL) {
    debug_print("ERROR: Group '");
    debug_print(group_name);
    debug_println("' not found");
    return false;
  }

  // Restore register values from saved snapshot
  for (uint8_t i = 0; i < grp->reg_count; i++) {
    uint16_t addr = grp->reg_addresses[i];
    uint16_t value = grp->reg_values[i];
    registers_set_holding_register(addr, value);
  }

  debug_print("✓ Restored group '");
  debug_print(group_name);
  debug_print("' (");
  debug_print_uint(grp->reg_count);
  debug_println(" registers)");

  return true;
}

bool registers_persist_group_restore_by_id(uint8_t group_id) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // ID 0 = restore all groups
  if (group_id == 0) {
    return registers_persist_restore_all_groups();
  }

  // Validate ID (1-8)
  if (group_id < 1 || group_id > PERSIST_MAX_GROUPS) {
    debug_print("ERROR: Invalid group ID ");
    debug_print_uint(group_id);
    debug_println(" (valid: 0-8)");
    return false;
  }

  // Check if group exists
  uint8_t idx = group_id - 1;  // Convert to 0-indexed
  if (idx >= pr->group_count) {
    debug_print("ERROR: Group #");
    debug_print_uint(group_id);
    debug_println(" not found");
    return false;
  }

  // Restore by name
  return registers_persist_group_restore(pr->groups[idx].name);
}

bool registers_persist_restore_all_groups(void) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;
  uint8_t safe_count = get_safe_group_count(pr);  // BUG-140

  if (safe_count == 0) {
    debug_println("No groups to restore");
    return true;
  }

  uint8_t restored_count = 0;
  for (uint8_t i = 0; i < safe_count; i++) {
    if (registers_persist_group_restore(pr->groups[i].name)) {
      restored_count++;
    }
  }

  debug_print("✓ Restored ");
  debug_print_uint(restored_count);
  debug_print(" of ");
  debug_print_uint(pr->group_count);
  debug_println(" groups");

  return true;
}

/* ============================================================================
 * AUTO-LOAD ON BOOT (v4.3.0)
 * ============================================================================ */

void registers_persist_set_auto_load_enabled(bool enabled) {
  g_persist_config.persist_regs.auto_load_enabled = enabled ? 1 : 0;

  debug_print("Auto-load on boot: ");
  debug_println(enabled ? "ENABLED" : "DISABLED");
}

bool registers_persist_is_auto_load_enabled(void) {
  return g_persist_config.persist_regs.auto_load_enabled != 0;
}

bool registers_persist_auto_load_add_group(uint8_t group_id) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // Validate ID (1-8)
  if (group_id < 1 || group_id > PERSIST_MAX_GROUPS) {
    debug_print("ERROR: Invalid group ID ");
    debug_print_uint(group_id);
    debug_println(" (valid: 1-8)");
    return false;
  }

  // Check if group exists
  uint8_t idx = group_id - 1;
  if (idx >= pr->group_count) {
    debug_print("ERROR: Group #");
    debug_print_uint(group_id);
    debug_println(" does not exist");
    return false;
  }

  // Check if already in auto-load list
  for (uint8_t i = 0; i < 7; i++) {
    if (pr->auto_load_group_ids[i] == group_id) {
      debug_print("Group #");
      debug_print_uint(group_id);
      debug_println(" already in auto-load list");
      return false;
    }
  }

  // Find empty slot
  for (uint8_t i = 0; i < 7; i++) {
    if (pr->auto_load_group_ids[i] == 0) {
      pr->auto_load_group_ids[i] = group_id;
      debug_print("✓ Added group #");
      debug_print_uint(group_id);
      debug_println(" to auto-load list");
      return true;
    }
  }

  debug_println("ERROR: Auto-load list full (max 7 groups)");
  return false;
}

bool registers_persist_auto_load_remove_group(uint8_t group_id) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  // Find and remove
  for (uint8_t i = 0; i < 7; i++) {
    if (pr->auto_load_group_ids[i] == group_id) {
      // Shift remaining entries down
      for (uint8_t j = i; j < 6; j++) {
        pr->auto_load_group_ids[j] = pr->auto_load_group_ids[j + 1];
      }
      pr->auto_load_group_ids[6] = 0;  // Clear last entry

      debug_print("✓ Removed group #");
      debug_print_uint(group_id);
      debug_println(" from auto-load list");
      return true;
    }
  }

  debug_print("Group #");
  debug_print_uint(group_id);
  debug_println(" not found in auto-load list");
  return false;
}

uint8_t registers_persist_auto_load_execute(void) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;

  if (!pr->auto_load_enabled) {
    return 0;
  }

  debug_println("\n[Auto-Load] Restoring persistence groups from NVS...");

  uint8_t restored_count = 0;
  for (uint8_t i = 0; i < 7; i++) {
    uint8_t group_id = pr->auto_load_group_ids[i];
    if (group_id == 0) break;  // End of list

    debug_print("[Auto-Load] Restoring group #");
    debug_print_uint(group_id);
    debug_print("...");

    if (registers_persist_group_restore_by_id(group_id)) {
      restored_count++;
    } else {
      debug_println(" FAILED");
    }
  }

  if (restored_count > 0) {
    debug_print("[Auto-Load] ✓ Restored ");
    debug_print_uint(restored_count);
    debug_println(" groups");
  } else {
    debug_println("[Auto-Load] No groups configured for auto-load");
  }

  return restored_count;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

void registers_persist_list_groups(void) {
  PersistentRegisterData* pr = &g_persist_config.persist_regs;
  uint8_t safe_count = get_safe_group_count(pr);  // BUG-140

  debug_println("\n=== Persistence Groups ===");
  debug_print("Total groups: ");
  debug_print_uint(safe_count);
  debug_println("\n");

  for (uint8_t i = 0; i < safe_count; i++) {
    PersistGroup* grp = &pr->groups[i];

    debug_print("Group #");
    debug_print_uint(i + 1);  // Group ID (1-indexed for ST Logic)
    debug_print(" \"");
    debug_print(grp->name);
    debug_print("\" (");
    debug_print_uint(grp->reg_count);
    debug_println(" registers)");

    for (uint8_t j = 0; j < grp->reg_count; j++) {
      debug_print("  Reg ");
      debug_print_uint(grp->reg_addresses[j]);
      debug_print(" = ");
      debug_print_uint(grp->reg_values[j]);
      debug_println("");
    }

    if (grp->last_save_ms > 0) {
      debug_print("  Last save: ");
      debug_print_uint((millis() - grp->last_save_ms) / 1000);
      debug_println(" sec ago");
    }

    debug_println("");
  }

  debug_println("ST Logic usage:");
  debug_println("  SAVE(0)  - Save all groups");
  debug_println("  SAVE(1)  - Save group #1");
  debug_println("  LOAD(0)  - Load all groups");
  debug_println("  LOAD(1)  - Load group #1");
  debug_println("");

  // Show auto-load configuration
  debug_print("Auto-load on boot: ");
  debug_println(pr->auto_load_enabled ? "ENABLED" : "DISABLED");

  if (pr->auto_load_enabled) {
    debug_print("Auto-load groups: ");
    bool has_groups = false;
    for (uint8_t i = 0; i < 7; i++) {
      if (pr->auto_load_group_ids[i] == 0) break;
      if (has_groups) debug_print(", ");
      debug_print("#");
      debug_print_uint(pr->auto_load_group_ids[i]);
      has_groups = true;
    }
    if (!has_groups) {
      debug_print("(none configured)");
    }
    debug_println("");
  }

  debug_println("CLI commands:");
  debug_println("  set persist auto-load enable");
  debug_println("  set persist auto-load add <group_id>");
  debug_println("  set persist auto-load remove <group_id>");
  debug_println("");
}

void registers_persist_set_enabled(bool enabled) {
  g_persist_config.persist_regs.enabled = enabled ? 1 : 0;

  debug_print("Persistence system ");
  debug_println(enabled ? "ENABLED" : "DISABLED");
}

bool registers_persist_is_enabled(void) {
  return g_persist_config.persist_regs.enabled != 0;
}
