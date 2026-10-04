// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_fc_dispatch.h
 * @brief Modbus function code dispatcher (LAYER 2)
 *
 * LAYER 2: Function Code Handlers - Dispatch
 * Responsibility: Route function codes to appropriate handlers
 *
 * This file handles:
 * - Dispatching FC01-04 to read handlers
 * - Dispatching FC05-06, 0F-10 to write handlers
 * - Returning error for unsupported function codes
 *
 * Does NOT handle:
 * - Implementing function codes (→ modbus_fc_read.h, modbus_fc_write.h)
 * - Frame parsing (→ modbus_parser.h)
 * - Response serialization (→ modbus_serializer.h)
 */

#ifndef modbus_fc_dispatch_H
#define modbus_fc_dispatch_H

#include <stdint.h>
#include <stdbool.h>
#include "modbus_frame.h"

/* ============================================================================
 * FUNCTION CODE DISPATCH
 * ============================================================================ */

/**
 * @brief Dispatch Modbus request to appropriate handler
 * @param request_frame Input request frame
 * @param response_frame Output response frame
 * @return true if handled successfully, false otherwise
 */
bool modbus_dispatch_function_code(const ModbusFrame* request_frame, ModbusFrame* response_frame);

#endif // modbus_fc_dispatch_H
