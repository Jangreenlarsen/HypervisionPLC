/**
 * @file st_bytecode_persist.h
 * @brief Bytecode persistence to SPIFFS
 *
 * Saves compiled bytecode to /logic_N.bc files.
 * At boot, loads cached bytecode instead of recompiling from source.
 * Uses CRC32 of source code as invalidation key.
 *
 * Format: 16-byte header + 32-byte name + variable table + instructions + optional function registry
 */

#ifndef ST_BYTECODE_PERSIST_H
#define ST_BYTECODE_PERSIST_H

#include <stdint.h>
#include <stdbool.h>
#include "st_types.h"

/* Magic number "STBC" */
#define ST_BYTECODE_MAGIC   0x53544243
#define ST_BYTECODE_VERSION 3  // v3: var_names reduced from 32 to 16 bytes

/* Bytecode file header (16 bytes) */
typedef struct __attribute__((packed)) {
  uint32_t magic;             // 0x53544243 ("STBC")
  uint16_t version;           // Format version
  uint16_t instr_count;       // Number of bytecode instructions
  uint8_t  var_count;         // Number of variables
  uint8_t  exported_var_count;// Number of exported variables
  uint8_t  has_func_registry; // 1 if function registry follows instructions
  uint8_t  reserved;          // Padding
  uint32_t source_crc32;      // CRC32 of source code (invalidation key)
} st_bc_header_t;

/**
 * @brief Save compiled bytecode to SPIFFS
 * @param program_id Program index (0-3)
 * @param bytecode Compiled bytecode program
 * @param source Source code (for CRC32 calculation)
 * @param source_size Size of source code
 * @return true if saved successfully
 */
bool st_bytecode_save(uint8_t program_id, const st_bytecode_program_t *bytecode,
                      const char *source, uint32_t source_size);

/**
 * @brief Load cached bytecode from SPIFFS
 * @param program_id Program index (0-3)
 * @param bytecode Output: bytecode program (instructions will be malloc'd)
 * @param source Source code (for CRC32 validation)
 * @param source_size Size of source code
 * @return true if loaded successfully (false = cache miss/invalid, must recompile)
 */
bool st_bytecode_load(uint8_t program_id, st_bytecode_program_t *bytecode,
                      const char *source, uint32_t source_size);

/**
 * @brief Delete cached bytecode file
 * @param program_id Program index (0-3)
 */
void st_bytecode_invalidate(uint8_t program_id);

/**
 * @brief Calculate CRC32 of data
 * @param data Pointer to data
 * @param len Length of data
 * @return CRC32 value
 */
uint32_t st_crc32(const uint8_t *data, uint32_t len);

#endif // ST_BYTECODE_PERSIST_H
