/**
 * @file st_builtin_modbus.h
 * @brief ST Logic Modbus Master builtin functions
 *
 * Wrapper functions for calling Modbus Master from ST Logic programs.
 */

#ifndef ST_BUILTIN_MODBUS_H
#define ST_BUILTIN_MODBUS_H

#include "st_types.h"

/* ============================================================================
 * ST LOGIC MODBUS FUNCTIONS
 * ============================================================================ */

/**
 * @brief MB_READ_COIL(slave_id, address) → BOOL
 *
 * Reads a single coil from remote slave device.
 * Returns FALSE on error (check mb_last_error).
 */
st_value_t st_builtin_mb_read_coil(st_value_t slave_id, st_value_t address);

/**
 * @brief MB_READ_INPUT(slave_id, address) → BOOL
 *
 * Reads a single discrete input from remote slave device.
 * Returns FALSE on error (check mb_last_error).
 */
st_value_t st_builtin_mb_read_input(st_value_t slave_id, st_value_t address);

/**
 * @brief MB_READ_HOLDING(slave_id, address) → INT
 *
 * Reads a single holding register from remote slave device.
 * Returns 0 on error (check mb_last_error).
 */
st_value_t st_builtin_mb_read_holding(st_value_t slave_id, st_value_t address);

/**
 * @brief MB_READ_INPUT_REG(slave_id, address) → INT
 *
 * Reads a single input register from remote slave device.
 * Returns 0 on error (check mb_last_error).
 */
st_value_t st_builtin_mb_read_input_reg(st_value_t slave_id, st_value_t address);

/**
 * @brief MB_WRITE_COIL(slave_id, address, value) → BOOL
 *
 * Writes a single coil to remote slave device.
 * Returns TRUE on success, FALSE on error.
 */
st_value_t st_builtin_mb_write_coil(st_value_t slave_id, st_value_t address, st_value_t value);

/**
 * @brief MB_WRITE_HOLDING(slave_id, address, value) → BOOL
 *
 * Writes a single holding register to remote slave device.
 * Returns TRUE on success, FALSE on error.
 */
st_value_t st_builtin_mb_write_holding(st_value_t slave_id, st_value_t address, st_value_t value);

/* ============================================================================
 * MULTI-REGISTER FUNCTIONS (v7.9.2 — FC03 multi / FC16)
 * ============================================================================ */

/**
 * @brief MB_READ_HOLDINGS(slave_id, address, count, array) → BOOL
 *
 * Reads multiple consecutive holding registers from remote slave (FC03).
 * Results are written directly into the provided ARRAY OF INT variable.
 * Also cached in individual cache entries (addr, addr+1, ...).
 *
 * Usage:
 *   VAR regs : ARRAY[0..3] OF INT; END_VAR
 *   MB_READ_HOLDINGS(1, 100, 4, regs);
 *   (* regs[0] = reg100, regs[1] = reg101, ... *)
 *
 * Returns TRUE if queued successfully. count: 1-16 registers.
 */
st_value_t st_builtin_mb_read_holdings(st_value_t slave_id, st_value_t address, st_value_t count);

/**
 * @brief MB_WRITE_HOLDINGS(slave_id, address, count, array) → BOOL
 *
 * Writes multiple consecutive holding registers via FC16.
 * Values are read directly from the provided ARRAY OF INT variable.
 *
 * Usage:
 *   VAR regs : ARRAY[0..1] OF INT; END_VAR
 *   regs[0] := 1234;
 *   regs[1] := 5678;
 *   MB_WRITE_HOLDINGS(1, 200, 2, regs);
 *
 * Returns TRUE if queued successfully. count: 1-16 registers.
 */
st_value_t st_builtin_mb_write_holdings(st_value_t slave_id, st_value_t address, st_value_t count);

/* ============================================================================
 * ASYNC STATUS FUNCTIONS (v7.7.0 — 0-arg builtins)
 * ============================================================================ */

/**
 * @brief MB_SUCCESS() → BOOL
 * TRUE if last cache read had valid data.
 */
st_value_t st_builtin_mb_success_func();

/**
 * @brief MB_BUSY() → BOOL
 * TRUE if async queue has pending requests.
 */
st_value_t st_builtin_mb_busy_func();

/**
 * @brief MB_ERROR() → INT
 * Returns last Modbus error code (0=OK, 1=Timeout, etc.)
 */
st_value_t st_builtin_mb_error_func();

/**
 * @brief MB_CACHE(enabled) → BOOL
 * Enable/disable cache deduplication for subsequent MB_READ_* calls.
 * When FALSE, reads always queue a fresh request (bypass dedup).
 * When TRUE (default), reads use cached value if request already pending.
 * Returns previous cache state.
 */
st_value_t st_builtin_mb_cache_func(st_value_t enabled);

/* ============================================================================
 * GLOBAL STATUS VARIABLES (accessible from ST Logic)
 * ============================================================================ */

// These are updated after each Modbus call
extern int32_t g_mb_last_error;   // mb_error_code_t (0=OK, 1=Timeout, etc.)
extern bool g_mb_success;         // TRUE if last operation succeeded
extern uint8_t g_mb_request_count; // Current request count in this execution
extern bool g_mb_cache_enabled;   // TRUE = cache dedup active (default), FALSE = always refresh

// Multi-register buffer for MB_SET_REG/MB_GET_REG (v7.9.2)
#define MB_MULTI_REG_MAX 16
extern uint16_t g_mb_multi_reg_buf[MB_MULTI_REG_MAX];

#endif // ST_BUILTIN_MODBUS_H
