// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_parser.h
 * @brief Modbus frame parser - Convert raw bytes to request structs (LAYER 1)
 *
 * LAYER 1: Protocol Core - Modbus Parsing
 * Responsibility: Parse raw Modbus frames into structured request objects
 *
 * This file handles:
 * - Parsing FC01-04 (read requests)
 * - Parsing FC05-06 (write single)
 * - Parsing FC0F-10 (write multiple)
 * - Extracting addresses, quantities, values
 * - Input validation
 *
 * Does NOT handle:
 * - Building responses (→ modbus_serializer.h)
 * - Processing requests (→ modbus_fc_*.h)
 */

#ifndef modbus_parser_H
#define modbus_parser_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"
#include "types.h"

/* ============================================================================
 * READ REQUEST PARSING (FC01-04)
 * ============================================================================ */

/**
 * @brief Parse read request (FC01, FC02, FC03, FC04)
 * @param frame Input Modbus frame
 * @param req Output request structure
 * @return true if parsed successfully, false otherwise
 */
bool modbus_parse_read_request(const ModbusFrame* frame, ModbusReadRequest* req);

/* ============================================================================
 * WRITE SINGLE PARSING (FC05-06)
 * ============================================================================ */

/**
 * @brief Parse write single coil request (FC05)
 * @param frame Input Modbus frame
 * @param req Output request structure
 * @return true if parsed successfully, false otherwise
 */
bool modbus_parse_write_single_coil(const ModbusFrame* frame, ModbusWriteSingleCoilRequest* req);

/**
 * @brief Parse write single register request (FC06)
 * @param frame Input Modbus frame
 * @param req Output request structure
 * @return true if parsed successfully, false otherwise
 */
bool modbus_parse_write_single_register(const ModbusFrame* frame, ModbusWriteSingleRegisterRequest* req);

/* ============================================================================
 * WRITE MULTIPLE PARSING (FC0F-10)
 * ============================================================================ */

/**
 * @brief Parse write multiple coils request (FC0F)
 * @param frame Input Modbus frame
 * @param req Output request structure
 * @return true if parsed successfully, false otherwise
 */
bool modbus_parse_write_multiple_coils(const ModbusFrame* frame, ModbusWriteMultipleCoilsRequest* req);

/**
 * @brief Parse write multiple registers request (FC10)
 * @param frame Input Modbus frame
 * @param req Output request structure
 * @return true if parsed successfully, false otherwise
 */
bool modbus_parse_write_multiple_registers(const ModbusFrame* frame, ModbusWriteMultipleRegistersRequest* req);

#endif // modbus_parser_H
