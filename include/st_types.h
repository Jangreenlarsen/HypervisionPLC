/**
 * @file st_types.h
 * @brief Structured Text (IEC 61131-3) type definitions
 *
 * Defines all types, enums, and structures for ST language support:
 * - Lexer tokens
 * - Data types (BOOL, INT, DWORD, REAL)
 * - Variable declarations
 * - AST (Abstract Syntax Tree) nodes
 * - Bytecode instructions
 *
 * Compliance: IEC 61131-3:2013 "ST-Light" profile (embedded subset)
 */

#ifndef ST_TYPES_H
#define ST_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * LEXER TOKEN TYPES (IEC 61131-3 6.3.1)
 * ============================================================================ */

typedef enum {
  // Literals
  ST_TOK_BOOL_TRUE,         // TRUE
  ST_TOK_BOOL_FALSE,        // FALSE
  ST_TOK_INT,               // 123, -456, 0x1A2B, 2#1010
  ST_TOK_REAL,              // 1.23, 4.56e-10
  ST_TOK_STRING,            // 'hello'
  ST_TOK_TIME,              // T#5s, T#100ms, T#1h30m (IEC 61131-3 TIME literal)

  // Variables/Identifiers
  ST_TOK_IDENT,             // variable_name
  ST_TOK_CONST,             // CONST keyword

  // Keywords - Data types
  ST_TOK_BOOL,              // BOOL (keyword)
  ST_TOK_INT_KW,            // INT (keyword - 16-bit signed)
  ST_TOK_DINT_KW,           // DINT (keyword - 32-bit signed, Double INT)
  ST_TOK_DWORD,             // DWORD (or UINT32, ULINT - 32-bit unsigned)
  ST_TOK_REAL_KW,           // REAL (keyword - different from literal ST_TOK_REAL)

  // Keywords - Variable declarators (IEC 6.2.3)
  ST_TOK_VAR,               // VAR
  ST_TOK_VAR_INPUT,         // VAR_INPUT
  ST_TOK_VAR_OUTPUT,        // VAR_OUTPUT
  ST_TOK_VAR_IN_OUT,        // VAR_IN_OUT (future)
  ST_TOK_END_VAR,           // END_VAR
  ST_TOK_EXPORT,            // EXPORT (v5.1.0 - mark variable for IR pool export)

  // Keywords - Control structures (IEC 6.3.2)
  ST_TOK_IF,                // IF
  ST_TOK_THEN,              // THEN
  ST_TOK_ELSE,              // ELSE
  ST_TOK_ELSIF,             // ELSIF
  ST_TOK_END_IF,            // END_IF

  ST_TOK_CASE,              // CASE
  ST_TOK_OF,                // OF
  ST_TOK_END_CASE,          // END_CASE

  ST_TOK_FOR,               // FOR
  ST_TOK_TO,                // TO
  ST_TOK_BY,                // BY (step)
  ST_TOK_DO,                // DO
  ST_TOK_END_FOR,           // END_FOR

  ST_TOK_WHILE,             // WHILE
  ST_TOK_END_WHILE,         // END_WHILE

  ST_TOK_REPEAT,            // REPEAT
  ST_TOK_UNTIL,             // UNTIL
  ST_TOK_END_REPEAT,        // END_REPEAT

  ST_TOK_EXIT,              // EXIT
  ST_TOK_RETURN,            // RETURN (future)

  // Keywords - Program structure
  ST_TOK_PROGRAM,           // PROGRAM (future, for now just statements)
  ST_TOK_END_PROGRAM,       // END_PROGRAM
  ST_TOK_BEGIN,             // BEGIN (v4.3.0 - IEC 61131-3 program body start)
  ST_TOK_END,               // END (v4.3.0 - IEC 61131-3 program end)

  // Keywords - Function definitions (FEAT-003: User-defined functions)
  ST_TOK_FUNCTION,          // FUNCTION
  ST_TOK_END_FUNCTION,      // END_FUNCTION
  ST_TOK_FUNCTION_BLOCK,    // FUNCTION_BLOCK (stateful)
  ST_TOK_END_FUNCTION_BLOCK,// END_FUNCTION_BLOCK

  // Operators
  ST_TOK_ASSIGN,            // :=
  ST_TOK_EQ,                // =
  ST_TOK_NE,                // <>
  ST_TOK_LT,                // <
  ST_TOK_GT,                // >
  ST_TOK_LE,                // <=
  ST_TOK_GE,                // >=

  ST_TOK_PLUS,              // +
  ST_TOK_MINUS,             // -
  ST_TOK_MUL,               // *
  ST_TOK_DIV,               // /
  ST_TOK_MOD,               // MOD
  ST_TOK_POWER,             // ** (future)

  ST_TOK_AND,               // AND
  ST_TOK_OR,                // OR
  ST_TOK_NOT,               // NOT
  ST_TOK_XOR,               // XOR (future)

  ST_TOK_SHL,               // SHL (shift left)
  ST_TOK_SHR,               // SHR (shift right)

  // Delimiters
  ST_TOK_LPAREN,            // (
  ST_TOK_RPAREN,            // )
  ST_TOK_LBRACKET,          // [ (array index, future)
  ST_TOK_RBRACKET,          // ]
  ST_TOK_SEMICOLON,         // ;
  ST_TOK_COMMA,             // ,
  ST_TOK_COLON,             // :

  // Special
  ST_TOK_EOF,               // End of input
  ST_TOK_ERROR,             // Lexer error

  // Comments (not returned as tokens, filtered by lexer)
  // (* comment *)

} st_token_type_t;

/* Lexer token */
typedef struct {
  st_token_type_t type;
  char value[256];          // Token text (identifier, number, string, etc.)
  uint32_t line;            // Line number (for error reporting)
  uint32_t column;          // Column number
} st_token_t;

/* ============================================================================
 * DATA TYPES (IEC 61131-3 5.1 - Elementary data types)
 * ============================================================================ */

typedef enum {
  ST_TYPE_BOOL,             // BOOL (0/1) - 1 bit/register
  ST_TYPE_INT,              // INT (-32768 to 32767) - 16-bit signed, 1 register
  ST_TYPE_DINT,             // DINT (-2^31 to 2^31-1) - 32-bit signed, 2 registers
  ST_TYPE_DWORD,            // DWORD (0 to 2^32-1) - 32-bit unsigned, 2 registers
  ST_TYPE_REAL,             // REAL (IEEE 754 32-bit float) - 2 registers
  ST_TYPE_NONE,             // Used for statements (not variables)
} st_datatype_t;

/* Union to hold any ST value */
typedef union {
  bool bool_val;            // BOOL: 1 byte
  int16_t int_val;          // INT: 16-bit signed (-32768 to 32767)
  int32_t dint_val;         // DINT: 32-bit signed (-2^31 to 2^31-1)
  uint32_t dword_val;       // DWORD: 32-bit unsigned (0 to 2^32-1)
  float real_val;           // REAL: 32-bit IEEE 754 float
} st_value_t;

/* ST Variable (in VAR declarations) */
typedef struct {
  char name[64];            // Variable name
  st_datatype_t type;       // Data type
  st_value_t initial_value; // Default value
  uint8_t is_input;         // VAR_INPUT flag
  uint8_t is_output;        // VAR_OUTPUT flag
  uint8_t is_exported;      // EXPORT flag (v5.1.0 - map to IR 220-251 pool)
} st_variable_decl_t;

/* ============================================================================
 * AST NODE TYPES (Abstract Syntax Tree)
 * ============================================================================ */

typedef enum {
  // Statements
  ST_AST_ASSIGNMENT,        // var := expression
  ST_AST_IF,                // IF ... THEN ... ELSE ... END_IF
  ST_AST_CASE,              // CASE expr OF ... END_CASE
  ST_AST_FOR,               // FOR i := start TO end DO ... END_FOR
  ST_AST_WHILE,             // WHILE expr DO ... END_WHILE
  ST_AST_REPEAT,            // REPEAT ... UNTIL expr END_REPEAT
  ST_AST_EXIT,              // EXIT (break loop)
  ST_AST_CALL,              // Function call (future)
  ST_AST_REMOTE_WRITE,      // MB_WRITE_XXX(id, addr) := value (v4.6.0)
  ST_AST_RETURN,            // RETURN [expression] (FEAT-003)

  // FEAT-003: User-defined function definitions
  ST_AST_FUNCTION_DEF,      // FUNCTION name : type ... END_FUNCTION
  ST_AST_FUNCTION_BLOCK_DEF,// FUNCTION_BLOCK name ... END_FUNCTION_BLOCK

  // Expressions
  ST_AST_LITERAL,           // Constant (123, TRUE, 1.5, etc.)
  ST_AST_VARIABLE,          // Variable reference
  ST_AST_BINARY_OP,         // expr op expr (+, -, *, /, AND, OR, <, >, etc.)
  ST_AST_UNARY_OP,          // op expr (NOT, -)
  ST_AST_FUNCTION_CALL,     // func(arg1, arg2, ...) (future)
} st_ast_node_type_t;

/* Forward declarations for recursive AST structure */
typedef struct st_ast_node st_ast_node_t;

typedef struct {
  st_ast_node_t *left;      // Left operand
  st_ast_node_t *right;     // Right operand
  st_token_type_t op;       // Operator (ST_TOK_PLUS, ST_TOK_AND, etc.)
} st_binary_op_t;

typedef struct {
  st_ast_node_t *operand;   // Operand
  st_token_type_t op;       // Operator (ST_TOK_NOT, ST_TOK_MINUS)
} st_unary_op_t;

typedef struct {
  char var_name[64];        // Variable identifier
  st_datatype_t type;       // Type (inferred from context)
} st_variable_ref_t;

typedef struct {
  st_datatype_t type;       // Type of literal
  st_value_t value;         // Value
} st_literal_t;

typedef struct {
  char func_name[64];       // Function name (e.g., "SAVE", "LOAD", "ABS")
  st_ast_node_t *args[4];   // Function arguments (max 4 args)
  uint8_t arg_count;        // Number of arguments
} st_function_call_t;

typedef struct {
  st_ast_node_t *condition_expr; // Parsed condition expression
  st_ast_node_t *then_body; // Statements in THEN block
  st_ast_node_t *else_body; // Statements in ELSE block (NULL if no ELSE)
} st_if_stmt_t;

typedef struct {
  int32_t value;              // Case label value
  st_ast_node_t *body;        // Statements for this case
} st_case_branch_t;

typedef struct {
  st_ast_node_t *expr;        // Expression being tested
  st_case_branch_t branches[16];  // Up to 16 case branches
  uint8_t branch_count;       // Number of branches
  st_ast_node_t *else_body;   // ELSE block (NULL if none)
} st_case_stmt_t;

typedef struct {
  char var_name[64];        // Loop variable
  st_ast_node_t *start;     // Start expression
  st_ast_node_t *end;       // End expression
  st_ast_node_t *step;      // Step expression (NULL = default 1)
  st_ast_node_t *body;      // Loop body
} st_for_stmt_t;

typedef struct {
  st_ast_node_t *condition; // While condition
  st_ast_node_t *body;      // Loop body
} st_while_stmt_t;

typedef struct {
  st_ast_node_t *body;      // Loop body
  st_ast_node_t *condition; // Until condition
} st_repeat_stmt_t;

typedef struct {
  char var_name[64];        // Variable being assigned
  st_ast_node_t *expr;      // Expression
} st_assignment_t;

typedef struct {
  char func_name[64];        // "MB_WRITE_COIL" or "MB_WRITE_HOLDING"
  st_ast_node_t *slave_id;   // Slave ID expression
  st_ast_node_t *address;    // Address expression
  st_ast_node_t *value;      // Value expression (right side of :=)
  uint16_t func_id;          // ST_BUILTIN_MB_WRITE_COIL or ST_BUILTIN_MB_WRITE_HOLDING (enum value)
} st_remote_write_t;

/* FEAT-003: User-defined function definition (IEC 61131-3)
 *
 * Syntax:
 *   FUNCTION name : return_type
 *   VAR_INPUT
 *     param1 : INT;
 *   END_VAR
 *   VAR
 *     local1 : INT;
 *   END_VAR
 *     (* function body *)
 *     RETURN expression;
 *   END_FUNCTION
 */
typedef struct {
  char func_name[64];                   // Function name
  st_datatype_t return_type;            // Return type (ST_TYPE_NONE for void/FUNCTION_BLOCK)

  // Parameters (VAR_INPUT, VAR_OUTPUT, VAR_IN_OUT)
  st_variable_decl_t params[8];         // Max ST_MAX_FUNCTION_PARAMS parameters
  uint8_t param_count;

  // Local variables (VAR)
  st_variable_decl_t locals[16];        // Max ST_MAX_FUNCTION_LOCALS local variables
  uint8_t local_count;

  // Function body (linked list of statements)
  st_ast_node_t *body;

  // Flags
  uint8_t is_function_block;            // 1 = FUNCTION_BLOCK (stateful), 0 = FUNCTION
} st_function_def_t;

/* FEAT-003: RETURN statement */
typedef struct {
  st_ast_node_t *expr;                  // Return expression (NULL for void return)
} st_return_stmt_t;

/* Main AST node */
typedef struct st_ast_node {
  st_ast_node_type_t type;
  uint32_t line;            // Line number for error reporting

  union {
    st_assignment_t assignment;
    st_if_stmt_t if_stmt;
    st_case_stmt_t case_stmt;
    st_for_stmt_t for_stmt;
    st_while_stmt_t while_stmt;
    st_repeat_stmt_t repeat_stmt;
    st_remote_write_t remote_write;  // v4.6.0: MB_WRITE_XXX(id, addr) := value
    st_return_stmt_t return_stmt;    // FEAT-003: RETURN statement
    // function_def moved out of union — heap-allocated via pointer below

    st_binary_op_t binary_op;
    st_unary_op_t unary_op;
    st_literal_t literal;
    st_variable_ref_t variable;
    st_function_call_t function_call;
  } data;

  // Heap-allocated only for ST_AST_FUNCTION_DEF / ST_AST_FUNCTION_BLOCK_DEF nodes.
  // Moved out of union to reduce st_ast_node_t from ~1920 to ~140 bytes (93% smaller).
  st_function_def_t *function_def;

  struct st_ast_node *next;  // Linked list of statements
} st_ast_node_t;

/* ============================================================================
 * FEAT-003: FUNCTION REGISTRY (for user-defined functions)
 * ============================================================================ */

/**
 * @brief Function registry entry
 *
 * Stores metadata about a user-defined function for lookup during
 * compilation and execution.
 */
typedef struct {
  char name[64];                        // Function name
  st_datatype_t return_type;            // Return type (ST_TYPE_NONE for FB/void)

  // Parameter info
  st_datatype_t param_types[8];         // Parameter types (max 8)
  uint8_t param_count;                  // Number of parameters

  // Bytecode location
  uint16_t bytecode_addr;               // Start address in bytecode array
  uint16_t bytecode_size;               // Number of instructions

  // Flags
  uint8_t is_builtin;                   // 1 = builtin C function, 0 = user-defined
  uint8_t is_function_block;            // 1 = FUNCTION_BLOCK (stateful)
  uint8_t instance_size;                // Bytes needed for state storage (FB only)
} st_function_entry_t;

/**
 * @brief FUNCTION_BLOCK instance storage (Phase 5)
 *
 * Stores persistent local variable state for each FUNCTION_BLOCK instance.
 * Each call site in the source code gets a unique instance, just like
 * builtin stateful functions (TON, CTU, etc.).
 *
 * Memory: 16 locals * 8 bytes + overhead = ~140 bytes per instance
 */
#define ST_MAX_FB_INSTANCES   16  // Max FUNCTION_BLOCK instances per program
#define ST_MAX_FB_LOCALS      16  // Max local variables per FB instance

typedef struct {
  st_value_t local_vars[ST_MAX_FB_LOCALS];    // Persistent local variable values
  st_datatype_t local_types[ST_MAX_FB_LOCALS]; // Types for each local variable
  uint8_t local_count;                         // Number of local variables
  uint8_t func_index;                          // Function registry index
  uint8_t initialized;                         // 1 = has been called at least once
} st_fb_instance_t;

/**
 * @brief Function registry
 *
 * Contains all functions (builtin + user-defined) for a program.
 * Used during compilation for name resolution and during VM execution
 * for CALL_USER dispatch.
 */
typedef struct {
  st_function_entry_t functions[64];    // Max ST_MAX_TOTAL_FUNCTIONS
  uint8_t builtin_count;                // Number of builtin functions
  uint8_t user_count;                   // Number of user-defined functions

  // Phase 5: FUNCTION_BLOCK instance storage
  st_fb_instance_t fb_instances[ST_MAX_FB_INSTANCES];  // Persistent state for FB instances
  uint8_t fb_instance_count;                            // Number of allocated FB instances
} st_function_registry_t;

/**
 * @brief VM call frame (for function call stack)
 *
 * Stores return address and local variable info when calling
 * a user-defined function.
 */
typedef struct {
  uint16_t return_pc;                   // Return address (PC to jump back to)
  uint16_t param_base;                  // Base index for parameters on value stack
  uint8_t param_count;                  // Number of parameters passed
  uint8_t local_count;                  // Number of local variables
  uint8_t func_index;                   // Index in function registry (for debugging)
  uint8_t fb_instance_id;              // Phase 5: FB instance ID (0xFF = stateless)
} st_call_frame_t;

/* ============================================================================
 * PROGRAM STRUCTURE (IEC 61131-3 6.1 - Organization)
 * ============================================================================ */

typedef struct {
  // Variable declarations (VAR, VAR_INPUT, VAR_OUTPUT)
  st_variable_decl_t variables[32]; // Max 32 variables per program
  uint8_t var_count;

  // AST root (linked list of statements)
  st_ast_node_t *body;

  // Metadata
  char name[64];            // Program name
  uint32_t size_bytes;      // Size of source code
  uint8_t enabled;          // Enabled flag
} st_program_t;

/* ============================================================================
 * BYTECODE INSTRUCTIONS (Stack-based VM)
 * ============================================================================ */

typedef enum {
  // Stack operations
  ST_OP_PUSH_BOOL,          // Push boolean literal
  ST_OP_PUSH_INT,           // Push int literal
  ST_OP_PUSH_DWORD,         // Push dword literal
  ST_OP_PUSH_REAL,          // Push real literal
  ST_OP_PUSH_VAR,           // Push variable value onto stack
  ST_OP_DUP,                // Duplicate top stack value
  ST_OP_POP,                // Pop and discard top stack value

  // Arithmetic
  ST_OP_ADD,                // Pop 2, push sum
  ST_OP_ADD_CHECKED,        // Pop 2, push sum with overflow check (BUG-159: FOR loops)
  ST_OP_SUB,                // Pop 2, push difference
  ST_OP_MUL,                // Pop 2, push product
  ST_OP_DIV,                // Pop 2, push quotient
  ST_OP_MOD,                // Pop 2, push modulo
  ST_OP_NEG,                // Pop 1, push negation

  // Logical
  ST_OP_AND,                // Pop 2, push logical AND
  ST_OP_OR,                 // Pop 2, push logical OR
  ST_OP_NOT,                // Pop 1, push logical NOT
  ST_OP_XOR,                // Pop 2, push logical XOR

  // Bitwise
  ST_OP_SHL,                // Shift left
  ST_OP_SHR,                // Shift right

  // Comparison
  ST_OP_EQ,                 // Pop 2, push (a == b)
  ST_OP_NE,                 // Pop 2, push (a != b)
  ST_OP_LT,                 // Pop 2, push (a < b)
  ST_OP_GT,                 // Pop 2, push (a > b)
  ST_OP_LE,                 // Pop 2, push (a <= b)
  ST_OP_GE,                 // Pop 2, push (a >= b)

  // Control flow
  ST_OP_JMP,                // Unconditional jump
  ST_OP_JMP_IF_FALSE,       // Pop 1, jump if false
  ST_OP_JMP_IF_TRUE,        // Pop 1, jump if true

  // Variable operations
  ST_OP_STORE_VAR,          // Pop value, store to variable
  ST_OP_LOAD_VAR,           // Load variable to stack

  // Loop
  ST_OP_LOOP_INIT,          // Initialize loop counter
  ST_OP_LOOP_TEST,          // Test loop condition
  ST_OP_LOOP_NEXT,          // Increment loop counter

  // Function calls
  ST_OP_CALL_BUILTIN,       // Call built-in function (int_arg = function ID)

  // FEAT-003: User-defined function calls
  ST_OP_CALL_USER,          // Call user-defined function (int_arg = function index in registry)
  ST_OP_RETURN,             // Return from function (pops return value, restores PC)
  ST_OP_LOAD_PARAM,         // Load function parameter (var_index = param index)
  ST_OP_STORE_LOCAL,        // Store to local variable (var_index = local index)
  ST_OP_LOAD_LOCAL,         // Load local variable (var_index = local index)

  // Misc
  ST_OP_NOP,                // No operation
  ST_OP_HALT,               // Stop execution
} st_opcode_t;

/* Bytecode instruction (8 bytes, optimized for DRAM) */
typedef struct {
  st_opcode_t opcode;
  union {
    int32_t int_arg;        // For PUSH_INT, JMP, etc.
    float float_arg;        // For PUSH_REAL
    uint32_t dword_arg;     // For PUSH_DWORD
    bool bool_arg;          // For PUSH_BOOL
    uint16_t var_index;     // For LOAD_VAR, STORE_VAR
    struct {                // For CALL_BUILTIN with stateful functions
      uint8_t func_id_low;  // Lower byte of function ID
      uint8_t instance_id;  // Instance storage index (0-7)
      uint16_t padding;     // Padding to 4 bytes
    } builtin_call;
    struct {                // FEAT-003: For CALL_USER with instance tracking
      uint8_t func_index;   // Function index in registry
      uint8_t instance_id;  // FB instance ID (0xFF = stateless FUNCTION)
      uint16_t padding;     // Padding to 4 bytes
    } user_call;
  } arg;
} st_bytecode_instr_t;

/* Forward declaration for stateful storage (defined in st_stateful.h) */
struct st_stateful_storage;

/* Bytecode program (compiled) */
typedef struct {
  st_bytecode_instr_t instructions[1024]; // Max 1024 instructions (doubled from 512)
  uint16_t instr_count;

  // Variable memory
  st_value_t variables[32];  // Max 32 variables
  char var_names[32][64];    // Variable names (for CLI binding by name)
  st_datatype_t var_types[32]; // Variable types (BOOL, INT, etc.) - for bindings display
  uint8_t var_count;

  // IR Pool Export (v5.1.0 - dynamic allocation of IR 220-251)
  uint8_t var_export_flags[32]; // 1 = EXPORT (visible in IR pool), 0 = private
  uint8_t exported_var_count;    // Number of exported variables

  // Stateful storage for timers, edges, counters (v4.7+)
  struct st_stateful_storage* stateful;  // Persistent state between cycles (opaque pointer)

  // FEAT-003: Function registry for user-defined functions
  st_function_registry_t* func_registry;  // NULL if no user functions (heap-allocated)

  char name[64];
  uint8_t enabled;
} st_bytecode_program_t;

/* ============================================================================
 * VIRTUAL MACHINE STATE
 * ============================================================================ */

typedef struct {
  // Execution stack
  st_value_t stack[64];     // Max 64 stack depth
  uint8_t stack_ptr;        // Current stack pointer

  // Program counter
  uint16_t pc;              // Program counter

  // Active program
  const st_bytecode_program_t *program;

  // Execution state
  uint8_t halted;           // Execution halted flag
  uint8_t error;            // Error flag
  char error_msg[128];      // Error message
} st_vm_state_t;

/* ============================================================================
 * CONFIGURATION (storage in NVS)
 * ============================================================================ */

typedef struct {
  uint8_t enabled;
  char program_data[5000];  // ST source code (max 5KB per program)
  uint32_t program_size;
  uint8_t compiled;         // Is program compiled to bytecode?
  st_bytecode_program_t bytecode; // Compiled bytecode
} st_logic_config_t;

#endif // ST_TYPES_H
