// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file registers.h
 * @brief Register storage and access (holding regs, input regs, coils, inputs)
 *
 * LAYER 4: Register/Coil Storage
 * Responsibility: Arrays of registers and coils, access functions
 *
 * This file provides:
 * - Holding registers (0-159): Read/write via Modbus FC03/FC06/FC10
 * - Input registers (0-159): Read-only via Modbus FC04
 * - Coils (0-255): Read/write via Modbus FC01/FC05/FC0F
 * - Discrete inputs (0-255): Read-only via Modbus FC02
 */

#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>
#include "constants.h"

/* ============================================================================
 * HOLDING REGISTERS (Read/Write)
 * ============================================================================ */

/**
 * @brief Get holding register value
 * @param addr Register address (0-159)
 * @return Register value (16-bit)
 */
uint16_t registers_get_holding_register(uint16_t addr);

/**
 * @brief Set holding register value
 * @param addr Register address (0-159)
 * @param value Value to set (16-bit)
 */
void registers_set_holding_register(uint16_t addr, uint16_t value);

/**
 * @brief Get all holding registers
 * @return Pointer to holding register array
 */
uint16_t* registers_get_holding_regs(void);

/* ============================================================================
 * INPUT REGISTERS (Read-Only)
 * ============================================================================ */

/**
 * @brief Get input register value
 * @param addr Register address (0-159)
 * @return Register value (16-bit)
 */
uint16_t registers_get_input_register(uint16_t addr);

/**
 * @brief Set input register value (from drivers)
 * @param addr Register address (0-159)
 * @param value Value to set (16-bit)
 */
void registers_set_input_register(uint16_t addr, uint16_t value);

/**
 * @brief Get all input registers
 * @return Pointer to input register array
 */
uint16_t* registers_get_input_regs(void);

/* ============================================================================
 * COILS (Read/Write)
 * ============================================================================ */

/**
 * @brief Get coil value
 * @param idx Coil index (0-31, packed bits)
 * @return 1 if set, 0 if clear
 */
uint8_t registers_get_coil(uint16_t idx);

/**
 * @brief Set coil value
 * @param idx Coil index (0-31, packed bits)
 * @param value Value to set (0 or 1)
 */
void registers_set_coil(uint16_t idx, uint8_t value);

/**
 * @brief Get all coils
 * @return Pointer to coils byte array
 */
uint8_t* registers_get_coils(void);

/* ============================================================================
 * DISCRETE INPUTS (Read-Only)
 * ============================================================================ */

/**
 * @brief Get discrete input value
 * @param idx Input index (0-31, packed bits)
 * @return 1 if set, 0 if clear
 */
uint8_t registers_get_discrete_input(uint16_t idx);

/**
 * @brief Set discrete input value (from GPIO/sensors)
 * @param idx Input index (0-31, packed bits)
 * @param value Value to set (0 or 1)
 */
void registers_set_discrete_input(uint16_t idx, uint8_t value);

/**
 * @brief Get all discrete inputs
 * @return Pointer to discrete inputs byte array
 */
uint8_t* registers_get_discrete_inputs(void);

/* ============================================================================
 * UTILITY / INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize all registers to zero
 */
void registers_init(void);

/**
 * @brief Get current millis() - for timing
 * @return Milliseconds since boot
 */
uint32_t registers_get_millis(void);

/* ============================================================================
 * DYNAMIC REGISTER/COIL UPDATES
 * ============================================================================ */

/**
 * @brief Update DYNAMIC registers from counter/timer sources
 * Called once per loop to sync DYNAMIC register values
 */
void registers_update_dynamic_registers(void);

/**
 * @brief Update DYNAMIC coils from counter/timer sources
 * Called once per loop to sync DYNAMIC coil values
 */
void registers_update_dynamic_coils(void);

/**
 * @brief Update ST Logic status input registers
 * Called once per loop to sync ST Logic program status to input registers 200-251
 */
void registers_update_st_logic_status(void);

/**
 * @brief Process ST Logic control register writes
 * Called when a holding register in the ST Logic control range (200-235) is written
 * @param addr Register address (200-235)
 * @param value Written value
 */
void registers_process_st_logic_control(uint16_t addr, uint16_t value);

/**
 * @brief Process ST Logic execution interval register writes (v4.1.0)
 * Called when holding register HR 236-237 (execution interval) is written
 * Validates interval (allowed: 10, 20, 25, 50, 75, 100 ms)
 * @param addr Register address (236 or 237)
 * @param value Written value
 */
void registers_process_st_logic_interval(uint16_t addr, uint16_t value);

#endif // REGISTERS_H
