// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_serializer.cpp
 * @brief Modbus response serializer implementation (LAYER 1)
 *
 * Builds Modbus response frames from data.
 * All multi-byte values are big-endian (Modbus standard).
 */

#include "modbus_serializer.h"
#include "constants.h"
#include "debug.h"
#include <string.h>

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Pack 16-bit value into big-endian bytes
 * @param dest Destination buffer (2 bytes)
 * @param value 16-bit value to pack
 */
static void pack_uint16_be(uint8_t* dest, uint16_t value) {
  dest[0] = (value >> 8) & 0xFF;
  dest[1] = value & 0xFF;
}

/* ============================================================================
 * READ RESPONSE SERIALIZATION (FC01-04)
 * ============================================================================ */

bool modbus_serialize_read_bits_response(ModbusFrame* frame, uint8_t slave_id, uint8_t function_code,
                                          const uint8_t* data, uint8_t byte_count) {
  if (frame == NULL || data == NULL) return false;

  // Response format: [Slave ID] [FC] [Byte Count] [Data...] [CRC]
  frame->slave_id = slave_id;
  frame->function_code = function_code;
  frame->data[0] = byte_count;
  memcpy(&frame->data[1], data, byte_count);

  // Total length: 1 (ID) + 1 (FC) + 1 (byte_count) + byte_count + 2 (CRC)
  frame->length = 5 + byte_count;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

bool modbus_serialize_read_registers_response(ModbusFrame* frame, uint8_t slave_id, uint8_t function_code,
                                                const uint16_t* data, uint16_t register_count) {
  if (frame == NULL || data == NULL) return false;

  // Response format: [Slave ID] [FC] [Byte Count] [Register Values...] [CRC]
  uint8_t byte_count = register_count * 2;

  frame->slave_id = slave_id;
  frame->function_code = function_code;
  frame->data[0] = byte_count;

  // Pack register values (big-endian)
  for (uint16_t i = 0; i < register_count; i++) {
    pack_uint16_be(&frame->data[1 + i * 2], data[i]);
  }

  // Total length: 1 (ID) + 1 (FC) + 1 (byte_count) + byte_count + 2 (CRC)
  frame->length = 5 + byte_count;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

/* ============================================================================
 * WRITE RESPONSE SERIALIZATION (FC05-06)
 * ============================================================================ */

bool modbus_serialize_write_single_coil_response(ModbusFrame* frame, uint8_t slave_id,
                                                   uint16_t output_address, uint16_t output_value) {
  if (frame == NULL) return false;

  // Response format: [Slave ID] [FC] [Output Address Hi] [Output Address Lo] [Output Value Hi] [Output Value Lo] [CRC]
  // Echo back the request (standard Modbus response for FC05)
  frame->slave_id = slave_id;
  frame->function_code = FC_WRITE_SINGLE_COIL;
  pack_uint16_be(&frame->data[0], output_address);
  pack_uint16_be(&frame->data[2], output_value);

  // Total length: 1 (ID) + 1 (FC) + 4 (data) + 2 (CRC) = 8
  frame->length = 8;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

bool modbus_serialize_write_single_register_response(ModbusFrame* frame, uint8_t slave_id,
                                                       uint16_t register_address, uint16_t register_value) {
  if (frame == NULL) return false;

  // Response format: [Slave ID] [FC] [Register Address Hi] [Register Address Lo] [Register Value Hi] [Register Value Lo] [CRC]
  // Echo back the request (standard Modbus response for FC06)
  frame->slave_id = slave_id;
  frame->function_code = FC_WRITE_SINGLE_REG;
  pack_uint16_be(&frame->data[0], register_address);
  pack_uint16_be(&frame->data[2], register_value);

  // Total length: 1 (ID) + 1 (FC) + 4 (data) + 2 (CRC) = 8
  frame->length = 8;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

/* ============================================================================
 * WRITE MULTIPLE RESPONSE SERIALIZATION (FC0F-10)
 * ============================================================================ */

bool modbus_serialize_write_multiple_coils_response(ModbusFrame* frame, uint8_t slave_id,
                                                      uint16_t starting_address, uint16_t quantity_of_outputs) {
  if (frame == NULL) return false;

  // Response format: [Slave ID] [FC] [Starting Address Hi] [Starting Address Lo] [Quantity Hi] [Quantity Lo] [CRC]
  frame->slave_id = slave_id;
  frame->function_code = FC_WRITE_MULTIPLE_COILS;
  pack_uint16_be(&frame->data[0], starting_address);
  pack_uint16_be(&frame->data[2], quantity_of_outputs);

  // Total length: 1 (ID) + 1 (FC) + 4 (data) + 2 (CRC) = 8
  frame->length = 8;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

bool modbus_serialize_write_multiple_registers_response(ModbusFrame* frame, uint8_t slave_id,
                                                          uint16_t starting_address, uint16_t quantity_of_registers) {
  if (frame == NULL) return false;

  // Response format: [Slave ID] [FC] [Starting Address Hi] [Starting Address Lo] [Quantity Hi] [Quantity Lo] [CRC]
  frame->slave_id = slave_id;
  frame->function_code = FC_WRITE_MULTIPLE_REGS;
  pack_uint16_be(&frame->data[0], starting_address);
  pack_uint16_be(&frame->data[2], quantity_of_registers);

  // Total length: 1 (ID) + 1 (FC) + 4 (data) + 2 (CRC) = 8
  frame->length = 8;

  // Set CRC
  modbus_frame_set_crc(frame);

  return true;
}

/* ============================================================================
 * ERROR RESPONSE SERIALIZATION
 * ============================================================================ */

bool modbus_serialize_error_response(ModbusFrame* frame, uint8_t slave_id,
                                      uint8_t function_code, uint8_t exception_code) {
  if (frame == NULL) return false;

  // Error response format: [Slave ID] [FC | 0x80] [Exception Code] [CRC]
  frame->slave_id = slave_id;
  frame->function_code = function_code | 0x80;  // Set bit 7 to indicate error
  frame->data[0] = exception_code;

  // Total length: 1 (ID) + 1 (FC) + 1 (exception) + 2 (CRC) = 5
  frame->length = 5;

  // Set CRC
  modbus_frame_set_crc(frame);

  debug_print("Modbus error response: FC=0x");
  debug_print_uint(function_code);
  debug_print(" Exception=0x");
  debug_print_uint(exception_code);
  debug_newline();

  return true;
}

