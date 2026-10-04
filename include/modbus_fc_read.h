// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_fc_read.h
 * @brief Modbus read function code handlers (LAYER 2)
 *
 * LAYER 2: Function Code Handlers - Read Operations
 * Responsibility: Implement FC01-04 (read coils, discrete inputs, holding regs, input regs)
 *
 * This file handles:
 * - FC01: Read Coils (0x01)
 * - FC02: Read Discrete Inputs (0x02)
 * - FC03: Read Holding Registers (0x03)
 * - FC04: Read Input Registers (0x04)
 *
 * Does NOT handle:
 * - Write operations (→ modbus_fc_write.h)
 * - Frame parsing (→ modbus_parser.h)
 * - Response serialization (→ modbus_serializer.h)
 */

#ifndef modbus_fc_read_H
#define modbus_fc_read_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"

/* ============================================================================
 * READ FUNCTION CODE HANDLERS (FC01-04)
 * ============================================================================ */

/**
 * @brief Handle FC01: Read Coils
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc01_read_coils(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC02: Read Discrete Inputs
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc02_read_discrete_inputs(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC03: Read Holding Registers
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc03_read_holding_registers(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC04: Read Input Registers
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc04_read_input_registers(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle reset-on-read for counter compare status and value registers
 * @param starting_address First register address that was read
 * @param quantity Number of registers that were read
 *
 * Checks if any counter has reset_on_read enabled and if the ctrl_reg/value_reg/raw_reg
 * was in the read range. If so:
 * - Clears bit 4 (compare status) in ctrl_reg
 * - Resets counter value to start_value if value/raw reg was read
 *
 * Should be called after register read operations (Modbus FC03 or CLI read reg)
 */
void modbus_handle_reset_on_read(uint16_t starting_address, uint16_t quantity);

#endif // modbus_fc_read_H
