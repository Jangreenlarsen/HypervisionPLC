/**
 * @file register_allocator.h
 * @brief Global register allocation tracker (LAYER 4.5)
 *
 * Prevents register conflicts between Counter, Timer, and ST Logic subsystems.
 * Maintains a single source of truth for which subsystem owns each Modbus register.
 *
 * Responsibility:
 * - Track allocation of all Modbus holding registers
 * - Prevent conflicts between subsystems
 * - Provide allocation validation before configuration
 * - Suggest free registers when conflicts detected
 *
 * (NEW in v4.2.0 - BUG-025 fix)
 */

#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

// Allocator tracks HR0-179 (ST vars, counters, timers)
// BUG-026 FIX (v4.2.3): Expanded from 100 to 160 for counter defaults
// BUG-028 FIX (v4.2.3): Expanded from 160 to 180 for 64-bit counter support (HR100-170)
#define ALLOCATOR_SIZE 180

/* ============================================================================
 * TYPES
 * ============================================================================ */

/**
 * @brief Subsystem owner of a register
 */
typedef enum {
  REG_OWNER_NONE = 0,      // Free register
  REG_OWNER_COUNTER,       // Counter subsystem (Counter 1-4)
  REG_OWNER_TIMER,         // Timer subsystem (Timer 1-4)
  REG_OWNER_ST_FIXED,      // ST Logic fixed registers (200-293, reserved)
  REG_OWNER_ST_VAR,        // ST Logic variable binding
  REG_OWNER_USER,          // User manual allocation (future use)
  REG_OWNER_ANALOG         // FEAT-034/035/036: analog I/O (Vi1-4, Ii1-4, AO1-2)
} RegisterOwnerType;

/**
 * @brief Register allocation metadata
 * Memory optimized: 6 bytes per entry (was 22 bytes)
 * Total for 160 regs: 960 bytes (was 3520 bytes) - saves 2560 bytes DRAM!
 */
typedef struct {
  uint8_t type;           // RegisterOwnerType (0-5)
  uint8_t subsystem_id;   // Counter/Timer/Logic ID (1-4), or 0
  char description[4];    // Short desc: "C1i", "T2c", "L1v" (4 chars max)
  // Removed timestamp (not used in production)
} RegisterOwner;

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize register allocator at startup
 *
 * Pre-allocates:
 * - ST Logic fixed registers (200-293)
 * - Counter smart defaults (if enabled)
 * - Timer control registers (if configured)
 * - ST variable bindings (from persistent config)
 *
 * Call from main.cpp setup() before any configuration
 */
void register_allocator_init(void);

/**
 * @brief Check if register is available
 * @param reg_addr Register address to check
 * @param owner Output: current owner (if allocated)
 * @return true if register is free, false if allocated
 */
bool register_allocator_check(uint16_t reg_addr, RegisterOwner* owner);

/**
 * @brief Allocate register to subsystem
 * @param reg_addr Register address to allocate
 * @param type Subsystem owner type
 * @param subsystem_id Counter/Timer/Logic ID (1-4) or 0 for global
 * @param description Human-readable description
 * @return true if allocated successfully, false if already allocated
 */
bool register_allocator_allocate(uint16_t reg_addr, RegisterOwnerType type,
                                 uint8_t subsystem_id, const char* description);

/**
 * @brief Free register (make available for reallocation)
 * @param reg_addr Register address to free
 */
void register_allocator_free(uint16_t reg_addr);

/**
 * @brief Find next free register in range
 * @param start Start address
 * @param end End address (inclusive)
 * @return First free register address in range, or 0xFFFF if none free
 */
uint16_t register_allocator_find_free(uint16_t start, uint16_t end);

/**
 * @brief Get current allocation map entry
 * @param reg_addr Register address
 * @return Pointer to RegisterOwner struct (valid until next allocator call)
 */
const RegisterOwner* register_allocator_get(uint16_t reg_addr);

/**
 * @brief Allocate multiple contiguous registers
 * @param start_addr First register address
 * @param count Number of registers to allocate
 * @param type Subsystem owner type
 * @param subsystem_id Counter/Timer/Logic ID
 * @param description Description for entire range
 * @return true if all allocated successfully, false if any conflict
 */
bool register_allocator_allocate_range(uint16_t start_addr, uint8_t count,
                                       RegisterOwnerType type, uint8_t subsystem_id,
                                       const char* description);

/**
 * @brief Free multiple contiguous registers
 * @param start_addr First register address
 * @param count Number of registers to free
 */
void register_allocator_free_range(uint16_t start_addr, uint8_t count);

/**
 * @brief DEBUG: Print allocation map (all allocated registers)
 */
void register_allocator_debug_dump(void);

#endif // REGISTER_ALLOCATOR_H
