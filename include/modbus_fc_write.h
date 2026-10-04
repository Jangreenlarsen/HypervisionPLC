// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_fc_write.h
 * @brief Modbus write function code handlers (LAYER 2)
 *
 * LAYER 2: Function Code Handlers - Write Operations
 * Responsibility: Implement FC05-06, FC0F-10 (write single/multiple coils/registers)
 *
 * This file handles:
 * - FC05: Write Single Coil (0x05)
 * - FC06: Write Single Register (0x06)
 * - FC0F: Write Multiple Coils (0x0F)
 * - FC10: Write Multiple Registers (0x10)
 *
 * Does NOT handle:
 * - Read operations (→ modbus_fc_read.h)
 * - Frame parsing (→ modbus_parser.h)
 * - Response serialization (→ modbus_serializer.h)
 */

#ifndef modbus_fc_write_H
#define modbus_fc_write_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"

/* ============================================================================
 * WRITE FUNCTION CODE HANDLERS (FC05-06, FC0F-10)
 * ============================================================================ */

/**
 * @brief Handle FC05: Write Single Coil
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc05_write_single_coil(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC06: Write Single Register
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc06_write_single_register(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC0F: Write Multiple Coils
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc0f_write_multiple_coils(const ModbusFrame* request_frame, ModbusFrame* response_frame);

/**
 * @brief Handle FC10: Write Multiple Registers
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_fc10_write_multiple_registers(const ModbusFrame* request_frame, ModbusFrame* response_frame);

#endif // modbus_fc_write_H
