// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file st_logic_engine.h
 * @brief Structured Text Logic Engine - Main Execution Loop
 *
 * Integrates ST programs with Modbus register I/O.
 * Handles reading inputs, executing bytecode, writing outputs.
 *
 * Call from main Modbus loop:
 *   st_logic_engine_loop(&logic_state, &holding_regs, &input_regs);
 */

#ifndef ST_LOGIC_ENGINE_H
#define ST_LOGIC_ENGINE_H

#include "st_logic_config.h"
#include "registers.h"

/* ============================================================================
 * MAIN LOGIC ENGINE INTERFACE
 * ============================================================================ */

/**
 * @brief Execute one iteration of logic engine
 *
 * For each enabled program:
 *   1. Read VAR_INPUT from Modbus holding registers
 *   2. Execute compiled bytecode
 *   3. Write VAR_OUTPUT to Modbus holding registers
 *
 * @param state Logic engine state
 * @param holding_regs Modbus holding registers array
 * @param input_regs Modbus input registers array
 * @return true if all programs executed without error
 */
bool st_logic_engine_loop(st_logic_engine_state_t *state,
                           uint16_t *holding_regs, uint16_t *input_regs);

/**
 * @brief Read VAR_INPUT values from Modbus registers
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param holding_regs Modbus holding registers
 * @return true if successful
 */
bool st_logic_read_inputs(st_logic_engine_state_t *state, uint8_t program_id,
                           uint16_t *holding_regs);

/**
 * @brief Write VAR_OUTPUT values to Modbus registers
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param holding_regs Modbus holding registers
 * @return true if successful
 */
bool st_logic_write_outputs(st_logic_engine_state_t *state, uint8_t program_id,
                             uint16_t *holding_regs);

/**
 * @brief Execute a single logic program (bytecode)
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @return true if successful (no errors during execution)
 */
bool st_logic_execute_program(st_logic_engine_state_t *state, uint8_t program_id);

/**
 * @brief Print logic engine status
 * @param state Logic engine state
 */
void st_logic_print_status(st_logic_engine_state_t *state);

/**
 * @brief Print individual program info
 * @param state Logic engine state
 * @param program_id Program ID (0-3)
 * @param show_source Show ST source code (0=hide, 1=show) - v5.1.0
 */
void st_logic_print_program(st_logic_engine_state_t *state, uint8_t program_id, uint8_t show_source);

/* ============================================================================
 * BUG-038 FIX: Variable access synchronization
 * Use these to protect ST variable access in gpio_mapping.cpp
 * ============================================================================ */

/**
 * @brief Lock ST variables for exclusive access
 * Call before reading/writing ST program variables from outside st_logic_engine
 */
void st_logic_lock_variables(void);

/* BUG-436: kaldes af st_logic_reinit() — en udfoerelse der koerer samtidig
 * (HIGH-task paa Core 0) skriver saa ikke sine gamle variabler tilbage oven i
 * den nulstillede tilstand. */
void st_logic_mark_reinit(uint8_t program_id);

/**
 * @brief Unlock ST variables after access
 * Call after reading/writing ST program variables
 */
void st_logic_unlock_variables(void);

#endif // ST_LOGIC_ENGINE_H
