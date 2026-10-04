// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file ir_pool_manager.h
 * @brief IR Pool Manager - Dynamic allocation of IR 220-251 for ST Logic EXPORT variables
 *
 * v5.1.0 - Implements flexible allocation of the 32 input registers (IR 220-251)
 * across ST Logic programs based on EXPORT keyword usage.
 *
 * LAYER: ST Logic Engine
 * Responsibility: Manage dynamic pool of IR 220-251 registers for exported ST variables
 *
 * Usage:
 *   uint8_t size_needed = ir_pool_calculate_size(&bytecode);
 *   uint8_t offset = ir_pool_allocate(state, program_id, size_needed);
 *   if (offset == 255) {
 *     // Pool exhausted
 *   }
 *   // Use IR[220 + offset] through IR[220 + offset + size - 1]
 */

#ifndef IR_POOL_MANAGER_H
#define IR_POOL_MANAGER_H

#include <stdint.h>
#include "st_logic_config.h"
#include "st_types.h"

/* ============================================================================
 * CONSTANTS
 * ============================================================================ */

#define IR_POOL_SIZE 32  // Total IR 220-251 registers available

/* ============================================================================
 * POOL ALLOCATION FUNCTIONS
 * ============================================================================ */

/**
 * @brief Calculate IR pool size needed for exported variables in bytecode
 * @param bytecode Compiled bytecode with export flags
 * @return Number of IR registers needed (accounts for REAL/DINT = 2 regs)
 */
uint8_t ir_pool_calculate_size(const st_bytecode_program_t *bytecode);

/**
 * @brief Allocate IR pool space for a program
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param size_needed Number of registers needed
 * @return Offset in pool (0-31), or 255 if pool full
 */
uint8_t ir_pool_allocate(st_logic_engine_state_t *state, uint8_t program_id, uint8_t size_needed);

/**
 * @brief Free IR pool allocation for a program
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 */
void ir_pool_free(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Get total IR pool usage across all programs
 * @param state Logic engine state
 * @return Number of registers currently allocated
 */
uint8_t ir_pool_get_total_used(const st_logic_engine_state_t *state);

/**
 * @brief Get free space in IR pool
 * @param state Logic engine state
 * @return Number of free registers
 */
uint8_t ir_pool_get_free_space(const st_logic_engine_state_t *state);

/**
 * @brief Compact IR pool after deletion (optional - moves allocations to fill gaps)
 * @param state Logic engine state
 * @note Not required but improves memory layout
 */
void ir_pool_compact(st_logic_engine_state_t *state);

/**
 * @brief Initialize IR pool for all programs (mark as unallocated)
 * @param state Logic engine state
 */
void ir_pool_init(st_logic_engine_state_t *state);

/**
 * @brief Reallocate IR pool for all compiled programs (after NVS load)
 * @param state Logic engine state
 * @note Call this after loading programs from NVS to restore IR allocations
 */
void ir_pool_reallocate_all(st_logic_engine_state_t *state);

/**
 * @brief Write EXPORT variables to IR 220-251 (BUG-178 FIX)
 * @param prog Logic program with compiled bytecode
 * @note Call this after program execution to sync EXPORT vars to input registers
 */
void ir_pool_write_exports(st_logic_program_config_t *prog);

#endif // IR_POOL_MANAGER_H
