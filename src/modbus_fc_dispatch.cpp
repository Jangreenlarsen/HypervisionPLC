// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_fc_dispatch.cpp
 * @brief Modbus function code dispatcher implementation (LAYER 2)
 *
 * Routes function codes to appropriate handlers.
 * Returns error for unsupported function codes.
 */

#include "modbus_fc_dispatch.h"
#include "modbus_fc_read.h"
#include "modbus_fc_write.h"
#include "modbus_serializer.h"
#include "constants.h"
#include "debug.h"
#include <stddef.h>

/* ============================================================================
 * FUNCTION CODE DISPATCH
 * ============================================================================ */

bool modbus_dispatch_function_code(const ModbusFrame* request_frame, ModbusFrame* response_frame) {
  if (request_frame == NULL || response_frame == NULL) return false;

  uint8_t fc = request_frame->function_code;

  // Dispatch to appropriate handler based on function code
  switch (fc) {
    case FC_READ_COILS:
      return modbus_fc01_read_coils(request_frame, response_frame);

    case FC_READ_DISCRETE_INPUTS:
      return modbus_fc02_read_discrete_inputs(request_frame, response_frame);

    case FC_READ_HOLDING_REGS:
      return modbus_fc03_read_holding_registers(request_frame, response_frame);

    case FC_READ_INPUT_REGS:
      return modbus_fc04_read_input_registers(request_frame, response_frame);

    case FC_WRITE_SINGLE_COIL:
      return modbus_fc05_write_single_coil(request_frame, response_frame);

    case FC_WRITE_SINGLE_REG:
      return modbus_fc06_write_single_register(request_frame, response_frame);

    case FC_WRITE_MULTIPLE_COILS:
      return modbus_fc0f_write_multiple_coils(request_frame, response_frame);

    case FC_WRITE_MULTIPLE_REGS:
      return modbus_fc10_write_multiple_registers(request_frame, response_frame);

    default:
      // Unsupported function code
      debug_print("Unsupported function code: 0x");
      debug_print_uint(fc);
      debug_newline();

      modbus_serialize_error_response(response_frame, request_frame->slave_id,
                                       fc, MODBUS_EXCEPTION_ILLEGAL_FUNCTION);
      return false;
  }
}

