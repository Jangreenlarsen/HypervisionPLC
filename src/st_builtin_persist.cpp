/**
 * @file st_builtin_persist.cpp
 * @brief ST Logic persistence built-in functions implementation
 *
 * Implements SAVE() and LOAD() for register persistence from ST programs.
 */

#include "st_builtin_persist.h"
#include "registers_persist.h"
#include "config_save.h"
#include "config_load.h"
#include "config_struct.h"
#include "debug.h"
#include <Arduino.h>

// External reference to global config
extern PersistConfig g_persist_config;

/* ============================================================================
 * SAVE() IMPLEMENTATION
 * ============================================================================ */

st_value_t st_builtin_persist_save(st_value_t group_id_arg) {
  static uint32_t last_save_ms = 0;
  st_value_t result;
  uint8_t group_id = (uint8_t)group_id_arg.int_val;

  // Rate limiting: Max 1 save per 5 seconds (protect flash wear)
  uint32_t now = millis();
  if (now - last_save_ms < 5000) {
    debug_print("SAVE(");
    debug_print_uint(group_id);
    debug_println(") rate limited (wait 5s between saves)");
    result.int_val = -2;  // Error: Rate limited
    return result;
  }

  // Check if persistence system is enabled
  if (!registers_persist_is_enabled()) {
    debug_print("SAVE(");
    debug_print_uint(group_id);
    debug_println(") failed: Persistence system disabled");
    result.int_val = -1;
    return result;
  }

  // Step 1: Snapshot group(s) register values
  debug_print("SAVE(");
  debug_print_uint(group_id);
  debug_println("): Snapshotting register groups...");

  bool success = registers_persist_group_save_by_id(group_id);
  if (!success) {
    debug_print("SAVE(");
    debug_print_uint(group_id);
    debug_println(") failed: Could not snapshot group");
    result.int_val = -1;
    return result;
  }

  // Step 2: Save entire config to NVS (includes persist_regs)
  debug_print("SAVE(");
  debug_print_uint(group_id);
  debug_println("): Writing to NVS...");

  success = config_save_to_nvs(&g_persist_config);
  if (!success) {
    debug_print("SAVE(");
    debug_print_uint(group_id);
    debug_println(") failed: NVS write error");
    result.int_val = -1;
    return result;
  }

  // Success!
  last_save_ms = now;
  debug_print("✓ SAVE(");
  debug_print_uint(group_id);
  debug_print(") completed: ");
  if (group_id == 0) {
    debug_print_uint(g_persist_config.persist_regs.group_count);
    debug_println(" groups saved to NVS");
  } else {
    debug_println("group saved to NVS");
  }

  result.int_val = 0;  // Success
  return result;
}

/* ============================================================================
 * LOAD() IMPLEMENTATION
 * ============================================================================ */

st_value_t st_builtin_persist_load(st_value_t group_id_arg) {
  st_value_t result;
  uint8_t group_id = (uint8_t)group_id_arg.int_val;

  // Step 1: Load config from NVS
  debug_print("LOAD(");
  debug_print_uint(group_id);
  debug_println("): Reading from NVS...");

  // SECURITY FIX: PersistConfig is ~30KB — too large for the ESP32 main-
  // loop stack (~8KB). This used to be a stack-local variable, so any ST
  // program that simply called LOAD() would reliably stack-overflow and
  // crash/reboot the device. Heap-allocate it instead, matching the
  // pattern already used in config_save.cpp.
  PersistConfig *temp_config = (PersistConfig*)malloc(sizeof(PersistConfig));
  if (!temp_config) {
    debug_print("LOAD(");
    debug_print_uint(group_id);
    debug_println(") failed: out of memory");
    result.int_val = -1;
    return result;
  }

  bool success = config_load_from_nvs(temp_config);
  if (!success) {
    debug_print("LOAD(");
    debug_print_uint(group_id);
    debug_println(") failed: NVS read error");
    free(temp_config);
    result.int_val = -1;
    return result;
  }

  // Step 2: Copy loaded persist_regs to global config
  // Note: We only restore persist_regs, not the entire config
  // (to avoid overwriting runtime changes to counters, timers, etc.)
  g_persist_config.persist_regs = temp_config->persist_regs;
  free(temp_config);

  // Step 3: Restore group(s) register values
  debug_print("LOAD(");
  debug_print_uint(group_id);
  debug_println("): Restoring register values...");

  success = registers_persist_group_restore_by_id(group_id);
  if (!success) {
    debug_print("LOAD(");
    debug_print_uint(group_id);
    debug_println(") failed: Could not restore group");
    result.int_val = -1;
    return result;
  }

  // Success!
  debug_print("✓ LOAD(");
  debug_print_uint(group_id);
  debug_print(") completed: ");
  if (group_id == 0) {
    debug_print_uint(g_persist_config.persist_regs.group_count);
    debug_println(" groups restored from NVS");
  } else {
    debug_println("group restored from NVS");
  }

  result.int_val = 0;  // Success
  return result;
}
