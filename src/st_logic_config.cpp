/**
 * @file st_logic_config.cpp
 * @brief Structured Text Logic Configuration Implementation
 */

#include "st_logic_config.h"
#include "st_parser.h"
#include "st_compiler.h"
#include "st_debug.h"  // FEAT-008: Reset debug state on delete/compile
#include "register_allocator.h"
#include "ir_pool_manager.h"  // v5.1.0 - IR pool management
#include "st_bytecode_persist.h"  // Bytecode cache in SPIFFS
#include "st_source_scanner.h"   // Chunked compilation pre-scanner
#include "st_stateful.h"         // st_stateful_storage_t for chunked compile
#include "debug.h"
#include "debug_flags.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <FS.h>
#include <SPIFFS.h>

/* ============================================================================
 * GLOBAL STATE
 * ============================================================================ */

// Global logic engine state - single instance for all 4 programs
static st_logic_engine_state_t g_logic_state;

// Global parser and compiler to avoid stack overflow
// (These are large structures, don't allocate on stack)
// Dynamic allocation: parser + compiler are only needed during compile (~12 KB saved)
static st_parser_t *g_parser = NULL;
static st_compiler_t *g_compiler = NULL;

/**
 * @brief Get pointer to global logic engine state
 */
st_logic_engine_state_t *st_logic_get_state(void) {
  return &g_logic_state;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void st_logic_init(st_logic_engine_state_t *state) {
  memset(state, 0, sizeof(*state));
  state->enabled = 1;
  state->execution_interval_ms = 10;  // Run every 10ms by default

  // Initialize each program
  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    memset(prog, 0, sizeof(*prog));
    snprintf(prog->name, sizeof(prog->name), "Logic%d", i + 1);
    prog->enabled = 0;  // Disabled by default
    prog->compiled = 0;
    prog->source_offset = 0xFFFFFFFF;  // Not allocated in pool
    prog->source_size = 0;
    prog->ir_pool_offset = 65535;  // v5.1.0 - IR pool not allocated
    prog->ir_pool_size = 0;
  }

  // v5.1.0 - Initialize IR pool manager
  ir_pool_init(state);

  // FEAT-008: Initialize debug states for each program
  for (int i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_debug_init(&state->debugger[i]);
  }
}

/* ============================================================================
 * POOL MANAGEMENT (Dynamic Allocation, v4.7.1)
 * ============================================================================ */

/**
 * @brief Get pointer to source code from pool
 */
const char* st_logic_get_source_code(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return NULL;

  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->source_offset == 0xFFFFFFFF || prog->source_size == 0) {
    return NULL;  // Not allocated
  }

  return &state->source_pool[prog->source_offset];
}

/**
 * @brief Calculate pool usage statistics
 */
void st_logic_get_pool_stats(st_logic_engine_state_t *state,
                              uint32_t *used_bytes, uint32_t *free_bytes, uint32_t *largest_free) {
  uint32_t total_used = 0;

  // Calculate used bytes
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    if (prog->source_offset != 0xFFFFFFFF) {
      total_used += prog->source_size;
    }
  }

  if (used_bytes) *used_bytes = total_used;
  if (free_bytes) *free_bytes = ST_LOGIC_POOL_SIZE - total_used;
  if (largest_free) *largest_free = ST_LOGIC_POOL_SIZE - total_used; // Simplified: assumes contiguous free space
}

/**
 * @brief Free program's pool allocation
 */
static void st_logic_pool_free(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return;

  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->source_offset == 0xFFFFFFFF) return;  // Not allocated

  // Compact pool: move all programs after this one down
  uint32_t free_offset = prog->source_offset;
  uint32_t free_size = prog->source_size;

  // Move data down
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *other = &state->programs[i];
    if (other->source_offset > free_offset && other->source_offset != 0xFFFFFFFF) {
      // Move this program's source code down
      memmove(&state->source_pool[other->source_offset - free_size],
              &state->source_pool[other->source_offset],
              other->source_size);
      other->source_offset -= free_size;
    }
  }

  // Mark as freed
  prog->source_offset = 0xFFFFFFFF;
  prog->source_size = 0;
}

/**
 * @brief Allocate space in pool for program
 * @return true if successful, false if pool full
 */
static bool st_logic_pool_allocate(st_logic_engine_state_t *state, uint8_t program_id, uint32_t size) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // Free existing allocation if any
  st_logic_pool_free(state, program_id);

  // Calculate used space
  uint32_t pool_used = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];
    if (prog->source_offset != 0xFFFFFFFF) {
      uint32_t end = prog->source_offset + prog->source_size;
      if (end > pool_used) {
        pool_used = end;
      }
    }
  }

  // Check if we have space
  if (pool_used + size > ST_LOGIC_POOL_SIZE) {
    return false;  // Pool full
  }

  // Allocate at end of used space
  st_logic_program_config_t *prog = &state->programs[program_id];
  prog->source_offset = pool_used;
  prog->source_size = size;

  return true;
}

/* ============================================================================
 * PROGRAM UPLOAD & COMPILATION
 * ============================================================================ */

bool st_logic_upload(st_logic_engine_state_t *state, uint8_t program_id,
                      const char *source, uint32_t source_size) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  st_logic_program_config_t *prog = &state->programs[program_id];

  // Validate source code
  if (!source) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Source code pointer is NULL");
    return false;
  }

  if (source_size == 0) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Source code is empty (0 bytes)");
    return false;
  }

  if (source_size > ST_LOGIC_POOL_SIZE) {
    snprintf(prog->last_error, sizeof(prog->last_error),
             "Source code too large: %u bytes (max %u bytes pool). Remove comments to reduce size.",
             (unsigned int)source_size, ST_LOGIC_POOL_SIZE);
    return false;
  }

  // Try to allocate space in pool
  if (!st_logic_pool_allocate(state, program_id, source_size)) {
    // Calculate available space
    uint32_t used, free, largest;
    st_logic_get_pool_stats(state, &used, &free, &largest);

    snprintf(prog->last_error, sizeof(prog->last_error),
             "Pool full: need %u bytes, only %u free (used: %u/%u)",
             (unsigned int)source_size, (unsigned int)free, (unsigned int)used, ST_LOGIC_POOL_SIZE);
    return false;
  }

  // Copy source code to pool
  memcpy(&state->source_pool[prog->source_offset], source, source_size);
  prog->compiled = 0;  // Mark as needing compilation

  // Invalidate bytecode cache (source changed)
  st_bytecode_invalidate(program_id);

  return true;
}

// Internal monolithic compile (used as fallback from chunked path)
static bool st_logic_compile_monolithic(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // FEAT-008: Reset debug state before recompiling (old snapshot is now invalid)
  st_debug_state_t *debug = &state->debugger[program_id];
  st_debug_stop(debug);
  st_debug_init(debug);

  st_logic_program_config_t *prog = &state->programs[program_id];

  // Free old dynamic allocations if recompiling
  if (prog->bytecode.instructions) {
    free(prog->bytecode.instructions);
    prog->bytecode.instructions = NULL;
    prog->bytecode.instr_count = 0;
    prog->bytecode.instr_capacity = 0;
  }
  if (prog->bytecode.func_registry) {
    free(prog->bytecode.func_registry);
    prog->bytecode.func_registry = NULL;
  }

  // Get source code from pool
  const char *source_raw = st_logic_get_source_code(state, program_id);
  if (!source_raw || prog->source_size == 0) {
    snprintf(prog->last_error, sizeof(prog->last_error), "No source code uploaded");
    return false;
  }

  // BUG-212: Create null-terminated copy of source code
  // Source pool entries are NOT null-terminated (BUG-202 variant).
  // The lexer requires '\0' to detect EOF. Without it, the parser reads
  // past the source into adjacent pool data, causing parse corruption.
  char *source_code = (char *)malloc(prog->source_size + 1);
  if (!source_code) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for source copy");
    return false;
  }
  memcpy(source_code, source_raw, prog->source_size);
  source_code[prog->source_size] = '\0';

  // Dynamically allocate parser and compiler (~12 KB, only needed during compile)
  g_parser = (st_parser_t *)malloc(sizeof(st_parser_t));
  if (!g_parser) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for parser");
    free(source_code);
    return false;
  }

  st_parser_init(g_parser, source_code);
  st_program_t *program = st_parser_parse_program(g_parser);

  if (!program) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Parse error: %s", g_parser->error_msg);
    // Pool may not have been freed if parse failed partway — ensure cleanup
    extern void ast_pool_free(void);
    ast_pool_free();
    free(g_parser);
    g_parser = NULL;
    free(source_code);
    return false;
  }

  // Free parser and source BEFORE compiler allocation to reduce heap pressure.
  // Parser and source are no longer needed — AST (program) holds all parsed data.
  free(g_parser);
  g_parser = NULL;
  free(source_code);
  source_code = NULL;

  g_compiler = (st_compiler_t *)malloc(sizeof(st_compiler_t));
  if (!g_compiler) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for compiler");
    st_program_free(program);
    return false;
  }

  st_compiler_init(g_compiler);
  // Compile into prog->bytecode (instructions dynamically allocated to exact size)
  st_bytecode_program_t *bytecode = st_compiler_compile(g_compiler, program, &prog->bytecode);

  if (!bytecode) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Compile error: %s", g_compiler->error_msg);
    st_program_free(program);
    free(g_compiler);
    g_compiler = NULL;
    return false;
  }

  prog->compiled = 1;
  prog->execution_count = 0;
  prog->error_count = 0;

  // FEAT-008: Set program_id in line map for source-level breakpoints
  g_line_map.program_id = program_id;

  // v5.1.0 - Allocate IR pool for EXPORT variables
  // Free old allocation if recompiling
  if (prog->ir_pool_offset != 65535) {
    ir_pool_free(state, program_id);
  }

  // Calculate required IR pool size
  uint8_t ir_size_needed = ir_pool_calculate_size(&prog->bytecode);
  if (ir_size_needed > 0) {
    uint8_t ir_offset = ir_pool_allocate(state, program_id, ir_size_needed);
    if (ir_offset == 255) {
      // Pool exhausted - compilation succeeded but IR export disabled
      snprintf(prog->last_error, sizeof(prog->last_error),
               "Warning: IR pool exhausted (%d regs needed, %d free). EXPORT disabled.",
               ir_size_needed, ir_pool_get_free_space(state));
      debug_printf("[WARN] Logic%d: IR pool exhausted, EXPORT disabled\n", program_id + 1);
      prog->ir_pool_offset = 65535;  // No allocation
      prog->ir_pool_size = 0;
    }
  } else {
    // No exported variables
    prog->ir_pool_offset = 65535;
    prog->ir_pool_size = 0;
  }

  st_program_free(program);
  // bytecode points to prog->bytecode (struct not freed, instructions owned by bytecode)

  // Free compiler (parser and source already freed before compilation)
  free(g_compiler);
  g_compiler = NULL;

  // Save compiled bytecode to SPIFFS cache for fast boot
  const char *cache_source = st_logic_get_source_code(state, program_id);
  if (cache_source && prog->source_size > 0) {
    st_bytecode_save(program_id, &prog->bytecode, cache_source, prog->source_size);
  }

  return true;
}

/* ============================================================================
 * CHUNKED COMPILATION (Fase 2: reduced peak heap)
 *
 * Compiles ST programs in chunks using a small AST pool (~4.5 KB per chunk)
 * instead of the full pool (23-82 KB). Each function is compiled separately,
 * then segments are assembled with jump relocation.
 *
 * Peak heap: ~20 KB vs 36-94 KB for monolithic compilation.
 * ============================================================================ */

// Small AST pool size for chunked compilation (32 nodes × ~144 bytes = ~4.6 KB)
#define CHUNK_AST_POOL_NODES 32

/**
 * @brief Relocate jump addresses in a bytecode segment
 *
 * After concatenating segments, jumps within each segment need their
 * target addresses offset by the segment's base position in the final array.
 */
static void relocate_segment(st_bytecode_instr_t *instructions, uint16_t count, uint16_t base_offset) {
  for (uint16_t i = 0; i < count; i++) {
    st_bytecode_instr_t *instr = &instructions[i];
    switch (instr->opcode) {
      case ST_OP_JMP:
      case ST_OP_JMP_IF_FALSE:
      case ST_OP_JMP_IF_TRUE:
        instr->arg.int_arg += base_offset;
        break;
      default:
        break;
    }
  }
}

/**
 * @brief Compile ST program using chunked multi-pass pipeline
 *
 * Pipeline:
 *   1. Pre-scan: find chunk boundaries (VAR, FUNCTION, main body)
 *   2. VAR pass: parse variable declarations, build symbol table
 *   3. Per-function: parse + compile each function to a segment
 *   4. Main body: parse + compile main body to a segment
 *   5. Assembly: concatenate segments with jump relocation
 *
 * Falls back to monolithic st_logic_compile() if source has no functions
 * (chunking overhead not worth it for simple programs).
 */
bool st_logic_compile_chunked(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  st_logic_program_config_t *prog = &state->programs[program_id];

  // Get source code
  const char *source_raw = st_logic_get_source_code(state, program_id);
  if (!source_raw || prog->source_size == 0) {
    snprintf(prog->last_error, sizeof(prog->last_error), "No source code uploaded");
    return false;
  }

  // Create null-terminated copy
  char *source_code = (char *)malloc(prog->source_size + 1);
  if (!source_code) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for source copy");
    return false;
  }
  memcpy(source_code, source_raw, prog->source_size);
  source_code[prog->source_size] = '\0';

  // Step 1: Pre-scan for chunk boundaries (heap-allocated to avoid stack overflow)
  st_scan_result_t *scan = (st_scan_result_t *)malloc(sizeof(st_scan_result_t));
  if (!scan) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for scanner");
    free(source_code);
    return false;
  }
  if (!st_source_scan(source_code, scan)) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Source scanner failed");
    free(scan);
    free(source_code);
    return false;
  }

  // Count function chunks — if none, fall back to monolithic compile
  uint8_t func_count = 0;
  for (uint8_t i = 0; i < scan->chunk_count; i++) {
    if (scan->chunks[i].type == ST_CHUNK_FUNCTION ||
        scan->chunks[i].type == ST_CHUNK_FUNCTION_BLOCK) {
      func_count++;
    }
  }

  if (func_count == 0) {
    // No functions — monolithic compile is simpler and equally efficient
    free(scan);
    free(source_code);
    return st_logic_compile_monolithic(state, program_id);
  }

  debug_printf("[CHUNKED] Program %d: %d chunks (%d functions)\n",
               program_id, scan->chunk_count, func_count);

  // FEAT-008: Reset debug state before recompiling
  st_debug_state_t *debug = &state->debugger[program_id];
  st_debug_stop(debug);
  st_debug_init(debug);

  // Free old dynamic allocations if recompiling
  if (prog->bytecode.instructions) {
    free(prog->bytecode.instructions);
    prog->bytecode.instructions = NULL;
    prog->bytecode.instr_count = 0;
    prog->bytecode.instr_capacity = 0;
  }
  if (prog->bytecode.func_registry) {
    free(prog->bytecode.func_registry);
    prog->bytecode.func_registry = NULL;
  }

  // Step 2: VAR pass — parse variable declarations with small AST pool
  g_compiler = (st_compiler_t *)malloc(sizeof(st_compiler_t));
  if (!g_compiler) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for compiler");
    free(source_code);
    return false;
  }
  st_compiler_init(g_compiler);

  // Parse full source for VAR declarations (uses small AST pool)
  if (!ast_pool_init_with_size(CHUNK_AST_POOL_NODES)) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: AST pool alloc failed (VAR pass)");
    free(g_compiler); g_compiler = NULL;
    free(source_code);
    return false;
  }

  g_parser = (st_parser_t *)malloc(sizeof(st_parser_t));
  if (!g_parser) {
    ast_pool_free();
    snprintf(prog->last_error, sizeof(prog->last_error), "Insufficient heap for parser");
    free(g_compiler); g_compiler = NULL;
    free(source_code);
    return false;
  }

  st_parser_init(g_parser, source_code);

  // Parse full program to extract VAR declarations (uses small AST pool)
  // st_parser_parse_program handles PROGRAM keyword, VAR blocks, and function defs
  st_program_t *parsed_program = st_parser_parse_program(g_parser);
  if (!parsed_program) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Chunked VAR parse: %s", g_parser->error_msg);
    ast_pool_free();
    free(g_parser); g_parser = NULL;
    free(g_compiler); g_compiler = NULL;
    free(scan);
    free(source_code);
    return false;
  }

  // Build symbol table from parsed variables
  uint8_t var_count = parsed_program->var_count;
  for (uint8_t i = 0; i < var_count; i++) {
    st_variable_decl_t *var = &parsed_program->variables[i];
    uint8_t idx = st_compiler_add_symbol(g_compiler, var->name, var->type,
                                          var->is_input, var->is_output, var->is_exported);
    if (idx == 0xFF) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: symbol table full");
      st_program_free(parsed_program);
      ast_pool_free();
      free(g_compiler); g_compiler = NULL;
      free(scan);
      free(source_code);
      return false;
    }
  }

  // Free parsed program and AST pool — symbol table is built in compiler
  st_program_free(parsed_program);
  ast_pool_free();
  free(g_parser);
  g_parser = NULL;

  debug_printf("[CHUNKED] VAR pass done: %d variables\n", var_count);

  // Allocate function registry (builtins use CALL_BUILTIN opcode, not registry)
  st_function_registry_t *registry = (st_function_registry_t *)malloc(sizeof(st_function_registry_t));
  if (!registry) {
    snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: registry alloc failed");
    free(g_compiler); g_compiler = NULL;
    free(scan);
    free(source_code);
    return false;
  }
  memset(registry, 0, sizeof(*registry));
  g_compiler->func_registry = registry;

  // Step 3 + 4: Compile each function and main body as separate segments
  // Collect segments in temporary arrays
  struct {
    st_bytecode_instr_t *instructions;
    uint16_t count;
    uint8_t chunk_index;  // index into scan.chunks
  } segments[ST_MAX_CHUNKS];
  uint8_t segment_count = 0;

  for (uint8_t c = 0; c < scan->chunk_count; c++) {
    st_chunk_t *chunk = &scan->chunks[c];

    // Skip VAR blocks (already processed)
    if (chunk->type == ST_CHUNK_VAR_BLOCK) continue;

    // Extract chunk source substring
    uint32_t chunk_len = chunk->end_offset - chunk->start_offset;
    if (chunk_len == 0) continue;

    char *chunk_source = (char *)malloc(chunk_len + 1);
    if (!chunk_source) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: chunk source alloc failed");
      goto chunked_cleanup;
    }
    memcpy(chunk_source, source_code + chunk->start_offset, chunk_len);
    chunk_source[chunk_len] = '\0';

    // Init small AST pool for this chunk
    if (!ast_pool_init_with_size(CHUNK_AST_POOL_NODES)) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: AST pool alloc failed (chunk %d)", c);
      free(chunk_source);
      goto chunked_cleanup;
    }

    // Parse chunk
    g_parser = (st_parser_t *)malloc(sizeof(st_parser_t));
    if (!g_parser) {
      ast_pool_free();
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: parser alloc failed");
      free(chunk_source);
      goto chunked_cleanup;
    }
    st_parser_init(g_parser, chunk_source);

    st_ast_node_t *ast = NULL;
    bool is_main = (chunk->type == ST_CHUNK_MAIN_BODY);

    if (chunk->type == ST_CHUNK_FUNCTION || chunk->type == ST_CHUNK_FUNCTION_BLOCK) {
      ast = st_parser_parse_function_def(g_parser);
    } else if (is_main) {
      ast = st_parser_parse_statements(g_parser);
    }

    if (!ast) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked parse error (chunk %d): %s",
               c, g_parser->error_msg);
      ast_pool_free();
      free(g_parser); g_parser = NULL;
      free(chunk_source);
      goto chunked_cleanup;
    }

    // Reset compiler bytecode pointer for this segment
    g_compiler->bytecode_ptr = 0;

    // Compile segment
    uint16_t seg_count = 0;
    st_bytecode_instr_t *seg_instr = st_compiler_compile_segment(
        g_compiler, ast, is_main /* emit_halt for main body */, &seg_count);

    ast_pool_free();
    free(g_parser);
    g_parser = NULL;
    free(chunk_source);

    if (!seg_instr && seg_count == 0 && !is_main) {
      // Empty function — skip
      continue;
    }

    if (!seg_instr) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked compile error (chunk %d): %s",
               c, g_compiler->error_msg);
      goto chunked_cleanup;
    }

    segments[segment_count].instructions = seg_instr;
    segments[segment_count].count = seg_count;
    segments[segment_count].chunk_index = c;
    segment_count++;

    debug_printf("[CHUNKED] Chunk %d (%s): %u instructions\n",
                 c, chunk->name[0] ? chunk->name : "main", seg_count);
  }

  // Step 5: Assemble segments
  {
    // Calculate total instruction count
    uint16_t total_instr = 0;
    for (uint8_t s = 0; s < segment_count; s++) {
      total_instr += segments[s].count;
    }

    if (total_instr == 0) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: no instructions generated");
      goto chunked_cleanup;
    }

    // Allocate final instruction array
    prog->bytecode.instructions = (st_bytecode_instr_t *)malloc(total_instr * sizeof(st_bytecode_instr_t));
    if (!prog->bytecode.instructions) {
      snprintf(prog->last_error, sizeof(prog->last_error), "Chunked: final instr alloc failed (%u)", total_instr);
      goto chunked_cleanup;
    }

    // Copy and relocate each segment
    uint16_t offset = 0;
    for (uint8_t s = 0; s < segment_count; s++) {
      memcpy(&prog->bytecode.instructions[offset], segments[s].instructions,
             segments[s].count * sizeof(st_bytecode_instr_t));

      // Relocate jumps if not at offset 0
      if (offset > 0) {
        relocate_segment(&prog->bytecode.instructions[offset], segments[s].count, offset);
      }

      // Update function registry entries for this segment
      st_chunk_t *chunk = &scan->chunks[segments[s].chunk_index];
      if (chunk->type == ST_CHUNK_FUNCTION || chunk->type == ST_CHUNK_FUNCTION_BLOCK) {
        // Find function in registry by name and update bytecode_addr
        for (uint8_t f = registry->builtin_count; f < registry->builtin_count + registry->user_count; f++) {
          if (strcmp(registry->functions[f].name, chunk->name) == 0) {
            registry->functions[f].bytecode_addr += offset;
            break;
          }
        }
      }

      offset += segments[s].count;

      // Free segment
      free(segments[s].instructions);
      segments[s].instructions = NULL;
    }

    prog->bytecode.instr_count = total_instr;
    prog->bytecode.instr_capacity = total_instr;

    // Copy symbol table to bytecode
    prog->bytecode.var_count = g_compiler->symbol_table.count;
    prog->bytecode.exported_var_count = 0;
    for (int i = 0; i < g_compiler->symbol_table.count; i++) {
      st_symbol_t *sym = &g_compiler->symbol_table.symbols[i];
      prog->bytecode.variables[i].int_val = 0;
      strncpy(prog->bytecode.var_names[i], sym->name, sizeof(prog->bytecode.var_names[i]) - 1);
      prog->bytecode.var_names[i][sizeof(prog->bytecode.var_names[i]) - 1] = '\0';
      prog->bytecode.var_types[i] = sym->type;
      prog->bytecode.var_export_flags[i] = sym->is_exported;
      if (sym->is_exported) prog->bytecode.exported_var_count++;
    }

    // Set program name
    if (scan->has_program_keyword && scan->program_name[0]) {
      strncpy(prog->bytecode.name, scan->program_name, sizeof(prog->bytecode.name) - 1);
    } else {
      snprintf(prog->bytecode.name, sizeof(prog->bytecode.name), "Logic%d", program_id + 1);
    }
    prog->bytecode.enabled = 1;

    // Transfer function registry
    prog->bytecode.func_registry = registry;
    g_compiler->func_registry = NULL;

    // Allocate stateful storage if needed
    if (g_compiler->edge_instance_count > 0 ||
        g_compiler->timer_instance_count > 0 ||
        g_compiler->counter_instance_count > 0) {
      st_stateful_storage_t *stateful = (st_stateful_storage_t *)malloc(sizeof(st_stateful_storage_t));
      if (stateful) {
        st_stateful_init(stateful);
        stateful->edge_count = g_compiler->edge_instance_count;
        stateful->timer_count = g_compiler->timer_instance_count;
        stateful->counter_count = g_compiler->counter_instance_count;
        prog->bytecode.stateful = (struct st_stateful_storage*)stateful;
      }
    } else {
      prog->bytecode.stateful = NULL;
    }

    prog->compiled = 1;
    prog->execution_count = 0;
    prog->error_count = 0;

    // IR pool allocation
    if (prog->ir_pool_offset != 65535) {
      ir_pool_free(state, program_id);
    }
    uint8_t ir_size_needed = ir_pool_calculate_size(&prog->bytecode);
    if (ir_size_needed > 0) {
      ir_pool_allocate(state, program_id, ir_size_needed);
    } else {
      prog->ir_pool_offset = 65535;
      prog->ir_pool_size = 0;
    }

    // Save bytecode cache
    const char *cache_source = st_logic_get_source_code(state, program_id);
    if (cache_source && prog->source_size > 0) {
      st_bytecode_save(program_id, &prog->bytecode, cache_source, prog->source_size);
    }

    free(g_compiler);
    g_compiler = NULL;
    free(scan);
    free(source_code);

    debug_printf("[CHUNKED] Assembly complete: %u total instructions\n", total_instr);
    return true;
  }

chunked_cleanup:
  // Free any remaining segments
  for (uint8_t s = 0; s < segment_count; s++) {
    if (segments[s].instructions) {
      free(segments[s].instructions);
    }
  }
  if (registry && g_compiler && g_compiler->func_registry == registry) {
    free(registry);
  }
  if (g_compiler) {
    g_compiler->func_registry = NULL;
    free(g_compiler);
    g_compiler = NULL;
  }
  free(scan);
  free(source_code);
  return false;
}

/* Public API: uses monolithic compile with bytecode caching */
bool st_logic_compile(st_logic_engine_state_t *state, uint8_t program_id) {
  return st_logic_compile_monolithic(state, program_id);
}

/* ============================================================================
 * VARIABLE BINDING (DEPRECATED)
 *
 * NOTE: Variable binding is now handled by the unified VariableMapping system.
 * Instead of using st_logic_bind_variable(), create a VariableMapping entry:
 *
 * Example:
 *   VariableMapping map = {
 *     .source_type = MAPPING_SOURCE_ST_VAR,
 *     .st_program_id = 0,
 *     .st_var_index = 2,
 *     .is_input = 0,  // OUTPUT mode
 *     .coil_reg = 100  // Write to HR#100
 *   };
 *   g_persist_config.var_maps[g_persist_config.var_map_count++] = map;
 *   config_save();
 *
 * This eliminates code duplication and provides a single place to manage
 * all variable bindings (GPIO + ST).
 * ============================================================================ */

/* ============================================================================
 * PROGRAM MANAGEMENT
 * ============================================================================ */

bool st_logic_set_enabled(st_logic_engine_state_t *state, uint8_t program_id, uint8_t enabled) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  st_logic_program_config_t *prog = &state->programs[program_id];
  prog->enabled = (enabled != 0);

  return true;
}

bool st_logic_delete(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return false;

  // FEAT-008: Reset debug state before deleting program
  st_debug_state_t *debug = &state->debugger[program_id];
  st_debug_stop(debug);
  st_debug_init(debug);

  // Invalidate bytecode cache
  st_bytecode_invalidate(program_id);

  // Free pool allocations
  st_logic_pool_free(state, program_id);
  ir_pool_free(state, program_id);  // v5.1.0 - Free IR pool

  // Free dynamic bytecode allocations before clearing program
  st_logic_program_config_t *prog = &state->programs[program_id];
  if (prog->bytecode.instructions) {
    free(prog->bytecode.instructions);
    prog->bytecode.instructions = NULL;
  }
  if (prog->bytecode.func_registry) {
    free(prog->bytecode.func_registry);
    prog->bytecode.func_registry = NULL;
  }

  // Clear the program itself
  memset(prog, 0, sizeof(*prog));
  snprintf(prog->name, sizeof(prog->name), "Logic%d", program_id + 1);
  prog->enabled = 0;
  prog->compiled = 0;
  prog->source_offset = 0xFFFFFFFF;  // Not allocated
  prog->source_size = 0;
  prog->ir_pool_offset = 65535;  // v5.1.0 - IR pool not allocated
  prog->ir_pool_size = 0;

  // Clear all variable bindings for this program
  extern PersistConfig g_persist_config;
  uint8_t i = 0;
  while (i < g_persist_config.var_map_count) {
    if (g_persist_config.var_maps[i].st_program_id == program_id) {
      // BUG-047 FIX: Free allocated registers before removing binding
      VariableMapping *map = &g_persist_config.var_maps[i];

      // BUG-109 FIX: Free multi-register bindings (DINT/REAL use word_count=2)
      uint8_t word_count = (map->word_count > 0) ? map->word_count : 1;

      // Free input register(s) if allocated (HR type)
      if (map->is_input && map->input_type == 0 && map->input_reg < ALLOCATOR_SIZE) {
        for (uint8_t w = 0; w < word_count; w++) {
          if (map->input_reg + w < ALLOCATOR_SIZE) {
            register_allocator_free(map->input_reg + w);
          }
        }
      }

      // Free output register/coil if allocated
      if (!map->is_input) {
        if (map->output_type == 0 && map->coil_reg < ALLOCATOR_SIZE) {
          // Holding register - free all words
          for (uint8_t w = 0; w < word_count; w++) {
            if (map->coil_reg + w < ALLOCATOR_SIZE) {
              register_allocator_free(map->coil_reg + w);
            }
          }
        }
        // Note: Coils don't use register allocator (only HR 0-299 tracked)
      }

      // Remove this binding by shifting all subsequent bindings down
      for (uint8_t j = i; j < g_persist_config.var_map_count - 1; j++) {
        g_persist_config.var_maps[j] = g_persist_config.var_maps[j + 1];
      }
      g_persist_config.var_map_count--;
      // Don't increment i - check this position again
    } else {
      i++;
    }
  }

  return true;
}

st_logic_program_config_t *st_logic_get_program(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id >= ST_LOGIC_MAX_PROGRAMS) return NULL;
  return &state->programs[program_id];
}

/**
 * @brief Update binding_count cache for all programs (BUG-005 fix)
 *
 * Counts variable bindings from g_persist_config.var_maps and updates
 * each program's cached binding_count field for performance optimization.
 * This avoids O(n*m) nested loop in registers_update_st_logic_status().
 */
void st_logic_update_binding_counts(st_logic_engine_state_t *state) {
  extern PersistConfig g_persist_config;

  // Reset all binding counts
  for (uint8_t prog_id = 0; prog_id < ST_LOGIC_MAX_PROGRAMS; prog_id++) {
    state->programs[prog_id].binding_count = 0;
  }

  // Count bindings for each program
  for (uint8_t i = 0; i < g_persist_config.var_map_count; i++) {
    const VariableMapping *map = &g_persist_config.var_maps[i];
    if (map->source_type == MAPPING_SOURCE_ST_VAR && map->st_program_id < ST_LOGIC_MAX_PROGRAMS) {
      state->programs[map->st_program_id].binding_count++;
    }
  }
}

/**
 * @brief Reset performance statistics for a program (v4.1.0)
 * @param state Logic engine state
 * @param program_id Program ID (0-3), or 0xFF for all programs
 */
void st_logic_reset_stats(st_logic_engine_state_t *state, uint8_t program_id) {
  if (program_id == 0xFF) {
    // Reset all programs
    for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
      st_logic_program_config_t *prog = &state->programs[i];
      prog->min_execution_us = 0;
      prog->max_execution_us = 0;
      prog->total_execution_us = 0;
      prog->overrun_count = 0;
      prog->execution_count = 0;
      prog->error_count = 0;
    }
  } else if (program_id < ST_LOGIC_MAX_PROGRAMS) {
    // Reset single program
    st_logic_program_config_t *prog = &state->programs[program_id];
    prog->min_execution_us = 0;
    prog->max_execution_us = 0;
    prog->total_execution_us = 0;
    prog->overrun_count = 0;
    prog->execution_count = 0;
    prog->error_count = 0;
  }
}

/**
 * @brief Reset global cycle statistics (v4.1.0)
 * @param state Logic engine state
 */
void st_logic_reset_cycle_stats(st_logic_engine_state_t *state) {
  state->cycle_min_ms = 0;
  state->cycle_max_ms = 0;
  state->cycle_overrun_count = 0;
  state->total_cycles = 0;
}

/* ============================================================================
 * PERSISTENCE (SPIFFS STORAGE)
 * ============================================================================ */

/**
 * @brief Save ST Logic programs to SPIFFS (unlimited size)
 * @return true if successful
 */
bool st_logic_save_to_nvs(void) {
  st_logic_engine_state_t *state = st_logic_get_state();
  DebugFlags* dbg = debug_flags_get();

  // Mount SPIFFS if not already mounted
  if (!SPIFFS.begin(true)) {
    if (dbg->config_save) {
      debug_println("ST_LOGIC SAVE: SPIFFS mount failed");
    }
    return false;
  }

  if (dbg->config_save) {
    debug_println("ST_LOGIC SAVE: Saving programs to SPIFFS");
  }

  // Save each program
  uint8_t saved_count = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];

    // Delete file if program is empty
    char filename[32];
    snprintf(filename, sizeof(filename), "/logic_%d.dat", i);

    if (prog->source_size == 0) {
      if (SPIFFS.exists(filename)) {
        SPIFFS.remove(filename);
        if (dbg->config_save) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_println(": deleted (empty)");
        }
      }
      continue;
    }

    // Open file for writing
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
      if (dbg->config_save) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": FAILED to open file");
      }
      continue;
    }

    // Write: enabled flag (1 byte) + source size (4 bytes) + source code
    file.write(prog->enabled);
    file.write((uint8_t*)&prog->source_size, sizeof(uint32_t));

    // Get source code from pool and write
    const char *source_code = st_logic_get_source_code(state, i);
    if (source_code && prog->source_size > 0 && prog->source_size <= ST_LOGIC_POOL_SIZE) {
      file.write((uint8_t*)source_code, prog->source_size);
    }
    file.close();

    if (dbg->config_save) {
      debug_print("  Program ");
      debug_print_uint(i);
      debug_print(": saved ");
      debug_print_uint(prog->source_size);
      debug_print(" bytes (enabled=");
      debug_print_uint(prog->enabled);
      debug_println(")");
    }
    saved_count++;
  }

  if (dbg->config_save) {
    debug_print("ST_LOGIC SAVE: Saved ");
    debug_print_uint(saved_count);
    debug_println(" programs to SPIFFS");
  }

  return true;
}

/**
 * @brief Load ST Logic programs from SPIFFS
 * @return true if successful
 */
bool st_logic_load_from_nvs(void) {
  st_logic_engine_state_t *state = st_logic_get_state();
  DebugFlags* dbg = debug_flags_get();

  // Mount SPIFFS if not already mounted
  if (!SPIFFS.begin(true)) {
    if (dbg->config_load) {
      debug_println("ST_LOGIC LOAD: SPIFFS mount failed");
    }
    return false;
  }

  if (dbg->config_load) {
    debug_println("ST_LOGIC LOAD: Loading programs from SPIFFS");
  }

  // Load each program
  uint8_t loaded_count = 0;
  for (uint8_t i = 0; i < ST_LOGIC_MAX_PROGRAMS; i++) {
    st_logic_program_config_t *prog = &state->programs[i];

    char filename[32];
    snprintf(filename, sizeof(filename), "/logic_%d.dat", i);

    // Check if file exists
    if (!SPIFFS.exists(filename)) {
      // No file for this program slot - OK (empty slot)
      continue;
    }

    // Open file for reading
    File file = SPIFFS.open(filename, FILE_READ);
    if (!file) {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": FAILED to open file");
      }
      continue;
    }

    // Read: enabled flag (1 byte) + source size (4 bytes) + source code
    if (file.available() < 5) {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_println(": file too small");
      }
      file.close();
      continue;
    }

    prog->enabled = file.read();
    file.read((uint8_t*)&prog->source_size, sizeof(uint32_t));

    if (prog->source_size > 0 && prog->source_size <= ST_LOGIC_POOL_SIZE) {
      // Allocate space in pool
      if (!st_logic_pool_allocate(state, i, prog->source_size)) {
        if (dbg->config_load) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_println(": FAILED to allocate pool space");
        }
        file.close();
        continue;
      }

      // Read source code from file into pool
      file.read((uint8_t*)&state->source_pool[prog->source_offset], prog->source_size);
      prog->compiled = 0;  // Mark as needing recompilation
      file.close();

      // Try loading cached bytecode first (avoids 36-94 KB peak heap)
      const char *pool_source = st_logic_get_source_code(state, i);
      if (pool_source && st_bytecode_load(i, &prog->bytecode, pool_source, prog->source_size)) {
        prog->compiled = 1;
        prog->bytecode.enabled = prog->enabled;

        // Allocate IR pool for EXPORT variables (same as in st_logic_compile)
        uint8_t ir_size_needed = ir_pool_calculate_size(&prog->bytecode);
        if (ir_size_needed > 0) {
          uint8_t ir_offset = ir_pool_allocate(state, i, ir_size_needed);
          if (ir_offset == 255) {
            prog->ir_pool_offset = 65535;
            prog->ir_pool_size = 0;
          }
        } else {
          prog->ir_pool_offset = 65535;
          prog->ir_pool_size = 0;
        }

        if (dbg->config_load) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_print(": loaded ");
          debug_print_uint(prog->source_size);
          debug_print(" bytes, bytecode cached (");
          debug_print_uint(prog->bytecode.instr_count);
          debug_println(" instr)");
        }
        loaded_count++;
      } else {
        // Cache miss or invalid — full compile
        if (dbg->config_load) {
          debug_print("  Program ");
          debug_print_uint(i);
          debug_print(": loaded ");
          debug_print_uint(prog->source_size);
          debug_print(" bytes, enabled=");
          debug_print_uint(prog->enabled);
          debug_print(", compiling...");
        }

        // Compile (uses chunked for functions, monolithic for simple programs)
        if (st_logic_compile(state, i)) {
          if (dbg->config_load) {
            debug_println(" OK");
          }
          loaded_count++;
        } else {
          if (dbg->config_load) {
            debug_print(" FAILED: ");
            debug_println(prog->last_error);
          }
        }
      }
    } else {
      if (dbg->config_load) {
        debug_print("  Program ");
        debug_print_uint(i);
        debug_print(": invalid size ");
        debug_print_uint(prog->source_size);
        debug_println("");
      }
      file.close();
    }
  }

  if (dbg->config_load) {
    debug_print("ST_LOGIC LOAD: Loaded ");
    debug_print_uint(loaded_count);
    debug_println(" programs from SPIFFS");
  }

  // BUG-005 FIX: Update binding count cache after loading programs
  st_logic_update_binding_counts(state);

  return true;
}

// Legacy compatibility functions - redirect to SPIFFS storage
bool st_logic_save_to_persist_config(PersistConfig *config) {
  (void)config;  // Unused - ST Logic now saves to SPIFFS filesystem
  return st_logic_save_to_nvs();  // Function name kept for compatibility
}

bool st_logic_load_from_persist_config(const PersistConfig *config) {
  (void)config;  // Unused - ST Logic now loads from SPIFFS filesystem
  return st_logic_load_from_nvs();  // Function name kept for compatibility
}
