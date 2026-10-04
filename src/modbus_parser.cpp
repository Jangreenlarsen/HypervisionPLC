// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_parser.cpp
 * @brief Modbus frame parser implementation (LAYER 1)
 *
 * Parses raw Modbus frames into structured request objects.
 * All multi-byte values are big-endian (Modbus standard).
 */

#include "modbus_parser.h"
#include "constants.h"
#include "debug.h"
#include <string.h>

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/**
 * @brief Extract 16-bit big-endian value from data buffer
 * @param data Pointer to data (2 bytes)
 * @return 16-bit value
 */
static uint16_t extract_uint16_be(const uint8_t* data) {
  return (uint16_t)((data[0] << 8) | data[1]);
}

/* ============================================================================
 * READ REQUEST PARSING (FC01-04)
 * ============================================================================ */

bool modbus_parse_read_request(const ModbusFrame* frame, ModbusReadRequest* req) {
  if (frame == NULL || req == NULL) return false;

  // FC01-04 format: [Starting Address Hi] [Starting Address Lo] [Quantity Hi] [Quantity Lo]
  // Data length should be 4 bytes (excluding slave_id, FC, CRC)
  if (frame->length != 8) {  // 1 (ID) + 1 (FC) + 4 (data) + 2 (CRC) = 8
    debug_println("ERROR: Invalid read request length");
    return false;
  }

  req->starting_address = extract_uint16_be(&frame->data[0]);
  req->quantity = extract_uint16_be(&frame->data[2]);

  // Validate quantity based on function code
  uint16_t max_quantity = 0;
  switch (frame->function_code) {
    case FC_READ_COILS:
    case FC_READ_DISCRETE_INPUTS:
      max_quantity = 2000;  // Max 2000 coils/discrete inputs per Modbus spec
      break;
    case FC_READ_HOLDING_REGS:
    case FC_READ_INPUT_REGS:
      max_quantity = 125;   // Max 125 registers per Modbus spec
      break;
    default:
      debug_println("ERROR: Invalid function code in read request");
      return false;
  }

  if (req->quantity == 0 || req->quantity > max_quantity) {
    debug_println("ERROR: Invalid quantity in read request");
    return false;
  }

  return true;
}

/* ============================================================================
 * WRITE SINGLE PARSING (FC05-06)
 * ============================================================================ */

bool modbus_parse_write_single_coil(const ModbusFrame* frame, ModbusWriteSingleCoilRequest* req) {
  if (frame == NULL || req == NULL) return false;

  // FC05 format: [Output Address Hi] [Output Address Lo] [Output Value Hi] [Output Value Lo]
  // Data length should be 4 bytes
  if (frame->length != 8) {
    debug_println("ERROR: Invalid write single coil length");
    return false;
  }

  req->output_address = extract_uint16_be(&frame->data[0]);
  req->output_value = extract_uint16_be(&frame->data[2]);

  // Validate coil value (must be 0x0000 or 0xFF00 per Modbus spec)
  if (req->output_value != 0x0000 && req->output_value != 0xFF00) {
    debug_println("ERROR: Invalid coil value (must be 0x0000 or 0xFF00)");
    return false;
  }

  return true;
}

bool modbus_parse_write_single_register(const ModbusFrame* frame, ModbusWriteSingleRegisterRequest* req) {
  if (frame == NULL || req == NULL) return false;

  // FC06 format: [Register Address Hi] [Register Address Lo] [Register Value Hi] [Register Value Lo]
  // Data length should be 4 bytes
  if (frame->length != 8) {
    debug_println("ERROR: Invalid write single register length");
    return false;
  }

  req->register_address = extract_uint16_be(&frame->data[0]);
  req->register_value = extract_uint16_be(&frame->data[2]);

  return true;
}

/* ============================================================================
 * WRITE MULTIPLE PARSING (FC0F-10)
 * ============================================================================ */

bool modbus_parse_write_multiple_coils(const ModbusFrame* frame, ModbusWriteMultipleCoilsRequest* req) {
  if (frame == NULL || req == NULL) return false;

  // FC0F format: [Starting Address Hi] [Starting Address Lo] [Quantity Hi] [Quantity Lo] [Byte Count] [Coil Values...]
  // Minimum data length: 5 bytes (address + quantity + byte_count)
  if (frame->length < 10) {  // 1 (ID) + 1 (FC) + 5 (min data) + 2 (CRC) = 9, but need at least 1 byte of data
    debug_println("ERROR: Invalid write multiple coils length");
    return false;
  }

  req->starting_address = extract_uint16_be(&frame->data[0]);
  req->quantity_of_outputs = extract_uint16_be(&frame->data[2]);
  req->byte_count = frame->data[4];

  // Validate quantity
  if (req->quantity_of_outputs == 0 || req->quantity_of_outputs > 1968) {  // Max per Modbus spec
    debug_println("ERROR: Invalid quantity in write multiple coils");
    return false;
  }

  // Validate byte count matches quantity
  uint16_t expected_bytes = (req->quantity_of_outputs + 7) / 8;  // Round up to nearest byte
  if (req->byte_count != expected_bytes) {
    debug_println("ERROR: Byte count mismatch in write multiple coils");
    return false;
  }

  // Validate frame length
  if (frame->length != 9 + req->byte_count) {  // 1 (ID) + 1 (FC) + 5 (header) + byte_count + 2 (CRC)
    debug_println("ERROR: Frame length mismatch in write multiple coils");
    return false;
  }

  // Copy coil values
  memcpy(req->output_values, &frame->data[5], req->byte_count);

  return true;
}

bool modbus_parse_write_multiple_registers(const ModbusFrame* frame, ModbusWriteMultipleRegistersRequest* req) {
  if (frame == NULL || req == NULL) return false;

  // FC10 format: [Starting Address Hi] [Starting Address Lo] [Quantity Hi] [Quantity Lo] [Byte Count] [Register Values...]
  // Minimum data length: 5 bytes (address + quantity + byte_count)
  if (frame->length < 11) {  // 1 (ID) + 1 (FC) + 5 (header) + 2 (min 1 register) + 2 (CRC) = 11
    debug_println("ERROR: Invalid write multiple registers length");
    return false;
  }

  req->starting_address = extract_uint16_be(&frame->data[0]);
  req->quantity_of_registers = extract_uint16_be(&frame->data[2]);
  req->byte_count = frame->data[4];

  // Validate quantity
  if (req->quantity_of_registers == 0 || req->quantity_of_registers > 123) {  // Max per Modbus spec
    debug_println("ERROR: Invalid quantity in write multiple registers");
    return false;
  }

  // Validate byte count matches quantity
  uint16_t expected_bytes = req->quantity_of_registers * 2;
  if (req->byte_count != expected_bytes) {
    debug_println("ERROR: Byte count mismatch in write multiple registers");
    return false;
  }

  // Validate frame length
  if (frame->length != 9 + req->byte_count) {  // 1 (ID) + 1 (FC) + 5 (header) + byte_count + 2 (CRC)
    debug_println("ERROR: Frame length mismatch in write multiple registers");
    return false;
  }

  // Extract register values (big-endian)
  for (uint16_t i = 0; i < req->quantity_of_registers; i++) {
    req->register_values[i] = extract_uint16_be(&frame->data[5 + i * 2]);
  }

  return true;
}

