// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_fc_write.cpp
 * @brief Modbus write function code handlers implementation (LAYER 2)
 *
 * Implements FC05-06, FC0F-10: Write Single/Multiple Coils/Registers
 */

#include "modbus_fc_write.h"
#include "modbus_parser.h"
#include "modbus_serializer.h"
#include "registers.h"
#include "constants.h"
#include "debug.h"
#include <string.h>

/* ============================================================================
 * FC05: WRITE SINGLE COIL
 * ============================================================================ */

bool modbus_fc05_write_single_coil(const ModbusFrame* request_frame, ModbusFrame* response_frame) {
  if (request_frame == NULL || response_frame == NULL) return false;

  // Parse request
  ModbusWriteSingleCoilRequest req;
  if (!modbus_parse_write_single_coil(request_frame, &req)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_SINGLE_COIL, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
    return false;
  }

  // Validate address range
  if (req.output_address >= (COILS_SIZE * 8)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_SINGLE_COIL, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    return false;
  }

  // Write coil value (0x0000 = OFF, 0xFF00 = ON)
  uint8_t coil_value = (req.output_value == 0xFF00) ? 1 : 0;
  registers_set_coil(req.output_address, coil_value);

  // Serialize response (echo back request)
  return modbus_serialize_write_single_coil_response(response_frame, request_frame->slave_id,
                                                       req.output_address, req.output_value);
}

/* ============================================================================
 * FC06: WRITE SINGLE REGISTER
 * ============================================================================ */

bool modbus_fc06_write_single_register(const ModbusFrame* request_frame, ModbusFrame* response_frame) {
  if (request_frame == NULL || response_frame == NULL) return false;

  // Parse request
  ModbusWriteSingleRegisterRequest req;
  if (!modbus_parse_write_single_register(request_frame, &req)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_SINGLE_REG, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
    return false;
  }

  // Validate address range
  if (req.register_address >= HOLDING_REGS_SIZE) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_SINGLE_REG, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    return false;
  }

  // Write register value
  registers_set_holding_register(req.register_address, req.register_value);

  // Serialize response (echo back request)
  return modbus_serialize_write_single_register_response(response_frame, request_frame->slave_id,
                                                           req.register_address, req.register_value);
}

/* ============================================================================
 * FC0F: WRITE MULTIPLE COILS
 * ============================================================================ */

bool modbus_fc0f_write_multiple_coils(const ModbusFrame* request_frame, ModbusFrame* response_frame) {
  if (request_frame == NULL || response_frame == NULL) return false;

  // Parse request
  ModbusWriteMultipleCoilsRequest req;
  if (!modbus_parse_write_multiple_coils(request_frame, &req)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_MULTIPLE_COILS, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
    return false;
  }

  // Validate address range
  if (req.starting_address + req.quantity_of_outputs > (COILS_SIZE * 8)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_MULTIPLE_COILS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    return false;
  }

  // Write coil values
  for (uint16_t i = 0; i < req.quantity_of_outputs; i++) {
    uint16_t byte_idx = i / 8;
    uint16_t bit_idx = i % 8;
    uint8_t coil_value = (req.output_values[byte_idx] >> bit_idx) & 1;
    registers_set_coil(req.starting_address + i, coil_value);
  }

  // Serialize response
  return modbus_serialize_write_multiple_coils_response(response_frame, request_frame->slave_id,
                                                          req.starting_address, req.quantity_of_outputs);
}

/* ============================================================================
 * FC10: WRITE MULTIPLE REGISTERS
 * ============================================================================ */

bool modbus_fc10_write_multiple_registers(const ModbusFrame* request_frame, ModbusFrame* response_frame) {
  if (request_frame == NULL || response_frame == NULL) return false;

  // Parse request
  ModbusWriteMultipleRegistersRequest req;
  if (!modbus_parse_write_multiple_registers(request_frame, &req)) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_MULTIPLE_REGS, MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE);
    return false;
  }

  // Validate address range
  if (req.starting_address + req.quantity_of_registers > HOLDING_REGS_SIZE) {
    modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                     FC_WRITE_MULTIPLE_REGS, MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS);
    return false;
  }

  // Write register values
  for (uint16_t i = 0; i < req.quantity_of_registers; i++) {
    registers_set_holding_register(req.starting_address + i, req.register_values[i]);
  }

  // Serialize response
  return modbus_serialize_write_multiple_registers_response(response_frame, request_frame->slave_id,
                                                              req.starting_address, req.quantity_of_registers);
}

