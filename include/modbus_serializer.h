// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_serializer.h
 * @brief Modbus response serializer - Build response frames (LAYER 1)
 *
 * LAYER 1: Protocol Core - Modbus Serialization
 * Responsibility: Build Modbus response frames from data
 *
 * This file handles:
 * - Building FC01-04 responses (read data)
 * - Building FC05-06 responses (write acknowledgment)
 * - Building FC0F-10 responses (write multiple acknowledgment)
 * - Building error responses (exceptions)
 * - Setting CRC16 in response frame
 *
 * Does NOT handle:
 * - Parsing requests (→ modbus_parser.h)
 * - Processing function codes (→ modbus_fc_*.h)
 */

#ifndef modbus_serializer_H
#define modbus_serializer_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"
#include "types.h"

/* Modbus Exception Codes */
#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION       0x01
#define MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS   0x02
#define MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE     0x03
#define MODBUS_EXCEPTION_SLAVE_DEVICE_FAILURE   0x04

/* ============================================================================
 * READ RESPONSE SERIALIZATION (FC01-04)
 * ============================================================================ */

/**
 * @brief Serialize read coils/discrete inputs response (FC01, FC02)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param function_code Function code (0x01 or 0x02)
 * @param data Coil/discrete input data (packed bits)
 * @param byte_count Number of bytes in data
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_read_bits_response(ModbusFrame* frame, uint8_t slave_id, uint8_t function_code,
                                          const uint8_t* data, uint8_t byte_count);

/**
 * @brief Serialize read registers response (FC03, FC04)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param function_code Function code (0x03 or 0x04)
 * @param data Register data (16-bit values)
 * @param register_count Number of registers in data
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_read_registers_response(ModbusFrame* frame, uint8_t slave_id, uint8_t function_code,
                                                const uint16_t* data, uint16_t register_count);

/* ============================================================================
 * WRITE RESPONSE SERIALIZATION (FC05-06)
 * ============================================================================ */

/**
 * @brief Serialize write single coil response (FC05)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param output_address Coil address
 * @param output_value Coil value (0x0000 or 0xFF00)
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_write_single_coil_response(ModbusFrame* frame, uint8_t slave_id,
                                                   uint16_t output_address, uint16_t output_value);

/**
 * @brief Serialize write single register response (FC06)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param register_address Register address
 * @param register_value Register value
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_write_single_register_response(ModbusFrame* frame, uint8_t slave_id,
                                                       uint16_t register_address, uint16_t register_value);

/* ============================================================================
 * WRITE MULTIPLE RESPONSE SERIALIZATION (FC0F-10)
 * ============================================================================ */

/**
 * @brief Serialize write multiple coils response (FC0F)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param starting_address Starting coil address
 * @param quantity_of_outputs Number of coils written
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_write_multiple_coils_response(ModbusFrame* frame, uint8_t slave_id,
                                                      uint16_t starting_address, uint16_t quantity_of_outputs);

/**
 * @brief Serialize write multiple registers response (FC10)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param starting_address Starting register address
 * @param quantity_of_registers Number of registers written
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_write_multiple_registers_response(ModbusFrame* frame, uint8_t slave_id,
                                                          uint16_t starting_address, uint16_t quantity_of_registers);

/* ============================================================================
 * ERROR RESPONSE SERIALIZATION
 * ============================================================================ */

/**
 * @brief Serialize error response (exception)
 * @param frame Output Modbus frame
 * @param slave_id Slave ID
 * @param function_code Original function code
 * @param exception_code Exception code (0x01-0x04)
 * @return true if serialized successfully, false otherwise
 */
bool modbus_serialize_error_response(ModbusFrame* frame, uint8_t slave_id,
                                      uint8_t function_code, uint8_t exception_code);

#endif // modbus_serializer_H
