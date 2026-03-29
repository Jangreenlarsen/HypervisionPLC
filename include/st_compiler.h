/**
 * @file st_compiler.h
 * @brief Structured Text Bytecode Compiler
 *
 * Converts AST (from parser) into bytecode (stack-based VM instructions).
 * Single-pass compiler with symbol table and jump target resolution.
 *
 * Usage:
 *   st_compiler_t compiler;
 *   st_compiler_init(&compiler);
 *   st_bytecode_program_t *bytecode = st_compiler_compile(&compiler, program);
 *   if (!bytecode) {
 *     printf("Compile error: %s\n", compiler.error_msg);
 *   }
 */

#ifndef ST_COMPILER_H
#define ST_COMPILER_H

#include "st_types.h"

/* Symbol table entry (variable name → index mapping) */
typedef struct {
  char name[64];
  uint8_t index;              // Variable index in bytecode program
  st_datatype_t type;
  uint8_t is_input;
  uint8_t is_output;
  uint8_t is_exported;        // EXPORT flag (v5.1.0 - map to IR 220-251 pool)
  // FEAT-003: Function scope tracking
  uint8_t is_func_param;      // 1 = function parameter (use LOAD_PARAM)
  uint8_t is_func_local;      // 1 = function local variable (use LOAD_LOCAL/STORE_LOCAL)
  uint8_t func_param_index;   // Parameter index within function (0-based)
  uint8_t func_local_index;   // Local variable index within function (0-based)
} st_symbol_t;

/* Symbol table */
typedef struct {
  st_symbol_t symbols[32];    // Max 32 variables
  uint8_t count;
} st_symbol_table_t;

/* Jump patch (forward jump - address not yet known) */
typedef struct {
  uint16_t bytecode_addr;     // Address of JMP instruction to patch
  uint32_t target_addr;       // Target address (resolved later)
} st_jump_patch_t;

/* Compiler state machine */
typedef struct {
  st_symbol_table_t symbol_table;
  st_bytecode_instr_t *bytecode;      // Pointer to output instructions buffer (no internal copy)
  uint16_t bytecode_ptr;              // Current bytecode pointer

  uint32_t error_count;
  char error_msg[256];
  uint32_t current_line;              // Current line being compiled (for error reporting - BUG-171)

  // Loop state tracking (for nested loops)
  uint16_t loop_stack[8];             // Stack of loop start addresses
  uint8_t loop_depth;

  // EXIT statement support (tracks jumps out of loops)
  uint16_t exit_patch_stack[32];      // Stack of EXIT jump addresses to backpatch
  uint8_t exit_patch_count[8];        // Number of EXIT patches per loop depth
  uint8_t exit_patch_total;           // Total EXIT patches pending

  // Forward jump patches (resolved in second pass if needed)
  st_jump_patch_t patches[32];
  uint8_t patch_count;

  // Instance allocation counters (v4.7+: stateful functions)
  uint8_t edge_instance_count;        // R_TRIG/F_TRIG instances allocated
  uint8_t timer_instance_count;       // TON/TOF/TP instances allocated
  uint8_t counter_instance_count;     // CTU/CTD/CTUD instances allocated
  uint8_t latch_instance_count;       // SR/RS latch instances allocated (v4.7.3)
  uint8_t hysteresis_instance_count;  // HYSTERESIS instances allocated (v4.8)
  uint8_t blink_instance_count;       // BLINK instances allocated (v4.8)
  uint8_t filter_instance_count;      // FILTER instances allocated (v4.8)

  // FEAT-003: User-defined function support
  uint8_t function_depth;             // Current function nesting depth (0 = main program)
  st_function_registry_t *func_registry;  // Function registry for user-defined functions
  uint16_t return_patch_stack[16];    // RETURN jump addresses to backpatch
  uint8_t return_patch_count;         // Number of RETURN patches pending
  uint8_t fb_instance_count;          // Phase 5: FUNCTION_BLOCK instances allocated
} st_compiler_t;

/**
 * @brief Initialize compiler
 * @param compiler Compiler state
 */
void st_compiler_init(st_compiler_t *compiler);

/**
 * @brief Compile ST program (AST) to bytecode
 * @param compiler Compiler state
 * @param program Parsed ST program (AST)
 * @param output Pre-allocated output buffer (if NULL, allocates on heap — caller must free)
 * @return Compiled bytecode program pointer (output if provided, else heap-allocated), NULL on error
 */
st_bytecode_program_t *st_compiler_compile(st_compiler_t *compiler, st_program_t *program,
                                           st_bytecode_program_t *output = nullptr);

/**
 * @brief Compile single AST node to bytecode (recursive)
 * @param compiler Compiler state
 * @param node AST node
 * @return true if successful
 */
bool st_compiler_compile_node(st_compiler_t *compiler, st_ast_node_t *node);

/**
 * @brief Compile expression (leaves result on stack)
 * @param compiler Compiler state
 * @param node Expression AST node
 * @return true if successful
 */
bool st_compiler_compile_expr(st_compiler_t *compiler, st_ast_node_t *node);

/**
 * @brief Add symbol to symbol table
 * @param compiler Compiler state
 * @param name Variable name
 * @param type Data type
 * @param is_input Is VAR_INPUT?
 * @param is_output Is VAR_OUTPUT?
 * @param is_exported Is EXPORT? (v5.1.0)
 * @return Variable index, or 0xFF on error
 */
uint8_t st_compiler_add_symbol(st_compiler_t *compiler, const char *name,
                                st_datatype_t type, uint8_t is_input, uint8_t is_output, uint8_t is_exported);

/**
 * @brief Look up symbol in table
 * @param compiler Compiler state
 * @param name Variable name
 * @return Variable index, or 0xFF if not found
 */
uint8_t st_compiler_lookup_symbol(st_compiler_t *compiler, const char *name);

/**
 * @brief Emit bytecode instruction
 * @param compiler Compiler state
 * @param opcode Instruction opcode
 * @param arg Optional argument (int_arg, etc.)
 * @return true if successful
 */
bool st_compiler_emit(st_compiler_t *compiler, st_opcode_t opcode);

/**
 * @brief Emit instruction with integer argument
 * @param compiler Compiler state
 * @param opcode Instruction opcode
 * @param arg Integer argument
 * @return true if successful
 */
bool st_compiler_emit_int(st_compiler_t *compiler, st_opcode_t opcode, int32_t arg);

/**
 * @brief Emit instruction with variable index argument
 * @param compiler Compiler state
 * @param opcode Instruction opcode
 * @param var_index Variable index
 * @return true if successful
 */
bool st_compiler_emit_var(st_compiler_t *compiler, st_opcode_t opcode, uint8_t var_index);

/**
 * @brief Emit builtin function call with instance ID (v4.7+)
 * @param compiler Compiler state
 * @param func_id Builtin function ID
 * @param instance_id Instance ID for stateful functions (0 for stateless)
 * @return true if successful
 */
bool st_compiler_emit_builtin_call(st_compiler_t *compiler, int32_t func_id, uint8_t instance_id);

/**
 * @brief Get current bytecode address (for jump targets)
 * @param compiler Compiler state
 * @return Current bytecode pointer
 */
uint16_t st_compiler_current_addr(st_compiler_t *compiler);

/**
 * @brief Emit jump instruction, return address for patching
 * @param compiler Compiler state
 * @param opcode JMP, JMP_IF_FALSE, or JMP_IF_TRUE
 * @return Address of jump instruction (for later patching)
 */
uint16_t st_compiler_emit_jump(st_compiler_t *compiler, st_opcode_t opcode);

/**
 * @brief Patch jump target address (backpatch)
 * @param compiler Compiler state
 * @param jump_addr Address of JMP instruction to patch
 * @param target_addr Target address
 */
void st_compiler_patch_jump(st_compiler_t *compiler, uint16_t jump_addr, uint16_t target_addr);

/**
 * @brief Report compilation error
 * @param compiler Compiler state
 * @param msg Error message
 */
void st_compiler_error(st_compiler_t *compiler, const char *msg);

/**
 * @brief Print generated bytecode (for debugging)
 * @param bytecode Compiled bytecode program
 */
void st_bytecode_print(st_bytecode_program_t *bytecode);

/**
 * @brief Convert opcode to string (for debugging)
 * @param opcode Opcode
 * @return String representation
 */
const char *st_opcode_to_string(st_opcode_t opcode);

/* ============================================================================
 * CHUNKED COMPILATION (Fase 2: per-function compilation)
 * ============================================================================ */

/**
 * @brief Compile AST nodes to a bytecode segment (addresses start from 0)
 *
 * Used for chunked compilation: compile one function or main body at a time.
 * The caller is responsible for jump relocation when assembling segments.
 *
 * @param compiler Compiler state (symbol table must be pre-populated)
 * @param nodes AST node list to compile
 * @param emit_halt If true, emit HALT at end of segment
 * @param out_count Output: number of instructions in segment
 * @return Malloc'd instruction array (caller must free), NULL on error
 */
st_bytecode_instr_t *st_compiler_compile_segment(st_compiler_t *compiler,
                                                  st_ast_node_t *nodes,
                                                  bool emit_halt,
                                                  uint16_t *out_count);

/* ============================================================================
 * LINE MAP (for source-level debugging breakpoints)
 * Generated during compilation, used by debugger.
 * ============================================================================ */

#define ST_LINE_MAP_MAX 128  // Max source lines tracked

/**
 * @brief Line-to-PC mapping (generated during compilation)
 * Allows setting breakpoints by source line number instead of PC.
 */
typedef struct {
  uint8_t program_id;                    // Which program this map belongs to
  uint16_t pc_for_line[ST_LINE_MAP_MAX]; // PC for each source line (0xFFFF = no code)
  uint16_t max_line;                     // Highest line number with code
  bool valid;                            // Is this map current?
} st_line_map_t;

// Global line map (shared between programs, regenerated on each compile)
extern st_line_map_t g_line_map;

/**
 * @brief Get PC address for a source line number
 * @param line Source line number (1-based)
 * @return PC address, or 0xFFFF if line has no code
 */
uint16_t st_line_map_get_pc(uint16_t line);

/**
 * @brief Get source line number for a PC address
 * @param pc PC address
 * @return Source line number, or 0 if not found
 */
uint16_t st_line_map_get_line(uint16_t pc);

#endif // ST_COMPILER_H
