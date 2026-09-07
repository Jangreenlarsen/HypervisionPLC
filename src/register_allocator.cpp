/**
 * @file register_allocator.cpp
 * @brief Global register allocation tracker (LAYER 4.5)
 *
 * Prevents register conflicts between Counter, Timer, and ST Logic subsystems.
 *
 * (NEW in v4.2.0 - BUG-025 fix)
 */

#include "register_allocator.h"
#include "constants.h"
#include "types.h"
#include "counter_config.h"
#include "timer_config.h"
#include "timer_engine.h"
#include "gpio_mapping.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

// Global allocation map - tracks which subsystem owns each register
// BUG-026 FIX (v4.2.3): Expanded to include counter/timer default zone
// BUG-028 FIX (v4.2.3): Expanded to 180 for 64-bit counter support
// HR0-99: General allocation zone (ST Logic vars, user manual)
// HR100-179: Counter/Timer allocation zone (smart defaults with 64-bit support)
// RegisterOwner size: 1+1+4 = 6 bytes (optimized)
// Total: 180*6 = 1080 bytes in DRAM (acceptable footprint)
#define ALLOCATOR_SIZE 180

// Allocate in DRAM (initialized at boot)
static RegisterOwner allocation_map[ALLOCATOR_SIZE];

// Track if allocator is initialized
static bool allocator_initialized = false;

/* ============================================================================
 * PUBLIC API IMPLEMENTATION
 * ============================================================================ */

void register_allocator_init(void) {
  if (allocator_initialized) {
    return;  // Already initialized
  }

  // 1. Mark all registers as free
  memset(allocation_map, 0, sizeof(allocation_map));

  // 2. Pre-allocate ST Logic fixed registers (200-293)
  // These are READ-ONLY system registers, always reserved
  // Note: We don't track 200-293 in allocation map (only track 0-99)
  // ST Logic registers are protected at a higher level (cli_commands_logic.cpp)

  // 3. Pre-allocate counter smart defaults (if enabled)
  // BUG-028 FIX: Allocate multi-word ranges for 32/64-bit counters
  // BUG-030 FIX: Also allocate compare_value_reg (multi-word for 32/64-bit)
  for (uint8_t id = 1; id <= 4; id++) {
    CounterConfig cfg;
    if (counter_config_get(id, &cfg) && cfg.enabled) {
      // Calculate word count based on bit width
      uint8_t words = (cfg.bit_width <= 16) ? 1 : (cfg.bit_width == 32) ? 2 : 4;

      // Allocate value register range (1-4 words depending on bit_width)
      register_allocator_allocate_range(cfg.value_reg, words, REG_OWNER_COUNTER, id, "val");

      // Allocate raw register range (1-4 words depending on bit_width)
      register_allocator_allocate_range(cfg.raw_reg, words, REG_OWNER_COUNTER, id, "raw");

      // Allocate single-word registers (always 16-bit)
      // Note: overflow flag is now in ctrl_reg bit 3, no separate register needed
      register_allocator_allocate(cfg.freq_reg, REG_OWNER_COUNTER, id, "frq");
      register_allocator_allocate(cfg.ctrl_reg, REG_OWNER_COUNTER, id, "ctl");

      // Allocate compare_value register range (1-4 words depending on bit_width)
      if (cfg.compare_enabled && cfg.compare_value_reg < ALLOCATOR_SIZE) {
        register_allocator_allocate_range(cfg.compare_value_reg, words, REG_OWNER_COUNTER, id, "cmp");
      }
    }
  }

  // 4. Pre-allocate timer ctrl-regs (if configured)
  for (uint8_t id = 1; id <= 4; id++) {
    TimerConfig cfg;
    if (timer_config_get(id, &cfg) && cfg.enabled && cfg.ctrl_reg < HOLDING_REGS_SIZE) {
      register_allocator_allocate(cfg.ctrl_reg, REG_OWNER_TIMER, id, "ctl");
    }
  }

  // 5. Pre-allocate ST variable bindings (from VariableMapping array)
  // BUG FIX: Must be done here at boot to prevent conflicts with counters
  // Register bindings from persistent config before system fully starts
  extern PersistConfig g_persist_config;
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    VariableMapping *map = &g_persist_config.var_maps[i];

    // Only allocate holding register bindings (input_type=0 or output_type=0)
    if (map->source_type == MAPPING_SOURCE_ST_VAR) {
      // Check INPUT bindings
      if (map->is_input && map->input_type == 0 && map->input_reg < ALLOCATOR_SIZE) {
        register_allocator_allocate(map->input_reg, REG_OWNER_ST_VAR,
                                   map->st_program_id + 1, "in");
      }

      // Check OUTPUT bindings (only allocate if different from input)
      if (!map->is_input && map->output_type == 0 && map->output_reg < ALLOCATOR_SIZE) {
        register_allocator_allocate(map->output_reg, REG_OWNER_ST_VAR,
                                   map->st_program_id + 1, "out");
      }
    }
  }

#if defined(ANALOG_IO_ENABLED)
  // 6. Pre-allocate analog I/O registers (FEAT-034/035/036, ES32D26 only).
  // Kun for enabled kanaler — samme princip som counters ovenfor, saa en
  // deaktiveret AI/AO-kanals registre er fri til andet brug.
  {
    extern PersistConfig g_persist_config;
    for (uint8_t i = 0; i < 4; i++) {
      if (g_persist_config.analog_ai_v[i].enabled) {
        register_allocator_allocate(g_persist_config.analog_ai_v[i].raw_reg, REG_OWNER_ANALOG, i + 1, "Vr");
        register_allocator_allocate(g_persist_config.analog_ai_v[i].value_reg, REG_OWNER_ANALOG, i + 1, "Vv");
      }
      if (g_persist_config.analog_ai_i[i].enabled) {
        register_allocator_allocate(g_persist_config.analog_ai_i[i].raw_reg, REG_OWNER_ANALOG, i + 5, "Ir");
        register_allocator_allocate(g_persist_config.analog_ai_i[i].value_reg, REG_OWNER_ANALOG, i + 5, "Iv");
      }
    }
    for (uint8_t i = 0; i < 2; i++) {
      if (g_persist_config.analog_ao[i].enabled) {
        register_allocator_allocate(g_persist_config.analog_ao[i].value_reg, REG_OWNER_ANALOG, i + 9, "AO");
      }
    }
  }
#endif

  allocator_initialized = true;

  debug_println("[ALLOCATOR] Register allocator initialized");
}

bool register_allocator_check(uint16_t reg_addr, RegisterOwner* owner) {
  if (reg_addr >= ALLOCATOR_SIZE) {
    return false;  // Out of bounds
  }

  RegisterOwner* reg_owner = &allocation_map[reg_addr];

  if (owner != NULL) {
    *owner = *reg_owner;
  }

  return (reg_owner->type == REG_OWNER_NONE);  // true if free
}

bool register_allocator_allocate(uint16_t reg_addr, RegisterOwnerType type,
                                 uint8_t subsystem_id, const char* description) {
  if (reg_addr >= ALLOCATOR_SIZE) {
    debug_println("[ALLOCATOR] ERROR: Register address out of bounds");
    return false;
  }

  RegisterOwner* owner = &allocation_map[reg_addr];

  // Check if already allocated
  if (owner->type != REG_OWNER_NONE) {
    debug_print("[ALLOCATOR] ERROR: HR");
    debug_print_uint(reg_addr);
    debug_print(" already allocated to ");
    debug_print(owner->description);
    debug_println("");
    return false;
  }

  // Allocate
  owner->type = type;
  owner->subsystem_id = subsystem_id;

  if (description != NULL) {
    strncpy(owner->description, description, sizeof(owner->description) - 1);
    owner->description[sizeof(owner->description) - 1] = '\0';
  } else {
    owner->description[0] = '\0';
  }

  return true;
}

void register_allocator_free(uint16_t reg_addr) {
  if (reg_addr >= ALLOCATOR_SIZE) {
    return;
  }

  RegisterOwner* owner = &allocation_map[reg_addr];

  if (owner->type != REG_OWNER_NONE) {
    debug_print("[ALLOCATOR] Freed HR");
    debug_print_uint(reg_addr);
    debug_print(" (was ");
    debug_print(owner->description);
    debug_println(")");
  }

  memset(owner, 0, sizeof(RegisterOwner));
}

uint16_t register_allocator_find_free(uint16_t start, uint16_t end) {
  if (start >= ALLOCATOR_SIZE || end >= ALLOCATOR_SIZE) {
    return 0xFFFF;  // Invalid range
  }

  for (uint16_t i = start; i <= end; i++) {
    if (allocation_map[i].type == REG_OWNER_NONE) {
      return i;
    }
  }

  return 0xFFFF;  // No free register found
}

const RegisterOwner* register_allocator_get(uint16_t reg_addr) {
  if (reg_addr >= ALLOCATOR_SIZE) {
    return NULL;
  }

  return &allocation_map[reg_addr];
}

bool register_allocator_allocate_range(uint16_t start_addr, uint8_t count,
                                       RegisterOwnerType type, uint8_t subsystem_id,
                                       const char* description) {
  if (start_addr >= ALLOCATOR_SIZE || start_addr + count > ALLOCATOR_SIZE) {
    return false;  // Out of bounds
  }

  // First check if all registers are free
  for (uint8_t i = 0; i < count; i++) {
    if (allocation_map[start_addr + i].type != REG_OWNER_NONE) {
      return false;  // Conflict found
    }
  }

  // Allocate all
  for (uint8_t i = 0; i < count; i++) {
    register_allocator_allocate(start_addr + i, type, subsystem_id, description);
  }

  return true;
}

void register_allocator_free_range(uint16_t start_addr, uint8_t count) {
  if (start_addr >= ALLOCATOR_SIZE || start_addr + count > ALLOCATOR_SIZE) {
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    register_allocator_free(start_addr + i);
  }
}

void register_allocator_debug_dump(void) {
  debug_println("[ALLOCATOR] === Register Allocation Map ===");

  uint8_t allocated_count = 0;
  for (uint16_t i = 0; i < ALLOCATOR_SIZE; i++) {
    RegisterOwner* owner = &allocation_map[i];
    if (owner->type != REG_OWNER_NONE) {
      debug_print("  HR");
      debug_print_uint(i);
      debug_print(" -> type=");
      debug_print_uint(owner->type);
      debug_print(", subsys=");
      debug_print_uint(owner->subsystem_id);
      debug_print(", desc=\"");
      debug_print(owner->description);
      debug_println("\"");
      allocated_count++;
    }
  }

  debug_print("[ALLOCATOR] Total allocated: ");
  debug_print_uint(allocated_count);
  debug_print(" / ");
  debug_print_uint(ALLOCATOR_SIZE);
  debug_println("");
}
