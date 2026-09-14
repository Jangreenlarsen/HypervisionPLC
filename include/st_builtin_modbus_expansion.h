/**
 * @file st_builtin_modbus_expansion.h
 * @brief FEAT-410: ST Logic builtins for expansion-board Modbus data-plan
 * (MBX_*) — mirrors st_builtin_modbus.h's MB_* family 1:1, med (board,
 * kanal) som to ekstra, indledende argumenter. Se
 * modbus_expansion_async.h for hvorfor multi-register-varianterne
 * (MBX_READ_HOLDINGS/MBX_WRITE_HOLDINGS) IKKE er en del af v1.
 */

#ifndef ST_BUILTIN_MODBUS_EXPANSION_H
#define ST_BUILTIN_MODBUS_EXPANSION_H

#include "st_types.h"

/**
 * @brief MBX_READ_COIL(board, kanal, slave_id, address) → BOOL
 */
st_value_t st_builtin_mbx_read_coil(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address);

/**
 * @brief MBX_READ_INPUT(board, kanal, slave_id, address) → BOOL
 */
st_value_t st_builtin_mbx_read_input(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address);

/**
 * @brief MBX_READ_HOLDING(board, kanal, slave_id, address) → INT
 */
st_value_t st_builtin_mbx_read_holding(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address);

/**
 * @brief MBX_READ_INPUT_REG(board, kanal, slave_id, address) → INT
 */
st_value_t st_builtin_mbx_read_input_reg(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address);

/**
 * @brief MBX_WRITE_COIL(board, kanal, slave_id, address, value) → BOOL
 */
st_value_t st_builtin_mbx_write_coil(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t value);

/**
 * @brief MBX_WRITE_HOLDING(board, kanal, slave_id, address, value) → BOOL
 */
st_value_t st_builtin_mbx_write_holding(st_value_t board, st_value_t channel, st_value_t slave_id, st_value_t address, st_value_t value);

/**
 * @brief MBX_SUCCESS() → BOOL — TRUE hvis det seneste MBX_*-kald lykkedes
 */
st_value_t st_builtin_mbx_success_func();

/**
 * @brief MBX_BUSY() → BOOL — TRUE hvis den asynkrone expansion-kø har afventende forespørgsler
 */
st_value_t st_builtin_mbx_busy_func();

/**
 * @brief MBX_ERROR() → INT — seneste mb_error_code_t
 */
st_value_t st_builtin_mbx_error_func();

extern int32_t g_mbx_last_error;
extern bool    g_mbx_success;
extern uint8_t g_mbx_request_count;
extern bool    g_mbx_cache_enabled;

#endif // ST_BUILTIN_MODBUS_EXPANSION_H
