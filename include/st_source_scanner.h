/**
 * @file st_source_scanner.h
 * @brief ST source code chunk scanner for multi-pass compilation
 *
 * Pre-scans ST source to find chunk boundaries (VAR blocks, FUNCTION
 * definitions, main body) without building an AST. Uses lexer tokens
 * for accurate boundary detection.
 *
 * Memory: ~200 bytes stack (lexer + token + chunk array pointers)
 */

#ifndef ST_SOURCE_SCANNER_H
#define ST_SOURCE_SCANNER_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum chunks per program (VAR + up to 16 functions + main body) */
#define ST_MAX_CHUNKS 20

/* Chunk types */
typedef enum {
  ST_CHUNK_PROGRAM_HEADER,  // PROGRAM name; (optional)
  ST_CHUNK_VAR_BLOCK,       // VAR ... END_VAR
  ST_CHUNK_FUNCTION,        // FUNCTION name : type ... END_FUNCTION
  ST_CHUNK_FUNCTION_BLOCK,  // FUNCTION_BLOCK name ... END_FUNCTION_BLOCK
  ST_CHUNK_MAIN_BODY,       // Everything after functions (BEGIN ... END_PROGRAM or statements)
} st_chunk_type_t;

/* Chunk descriptor — points into source string */
typedef struct {
  st_chunk_type_t type;
  uint32_t start_offset;    // Byte offset in source
  uint32_t end_offset;      // Byte offset (exclusive)
  char name[32];            // Function/program name (if applicable)
} st_chunk_t;

/* Scan result */
typedef struct {
  st_chunk_t chunks[ST_MAX_CHUNKS];
  uint8_t chunk_count;
  bool has_program_keyword;
  char program_name[32];
} st_scan_result_t;

/**
 * @brief Scan source code for chunk boundaries
 * @param source Null-terminated source code
 * @param result Output: scan result with chunk array
 * @return true if scan succeeded
 */
bool st_source_scan(const char *source, st_scan_result_t *result);

#endif // ST_SOURCE_SCANNER_H
