/**
 * @file st_vm.h
 * @brief Structured Text Stack-based Virtual Machine
 *
 * Executes compiled bytecode. Stack-based architecture (similar to Java bytecode).
 *
 * Usage:
 *   st_vm_t vm;
 *   st_vm_init(&vm, bytecode_program);
 *
 *   // Execute step-by-step (for async integration)
 *   while (!vm.halted && !vm.error) {
 *     st_vm_step(&vm);
 *   }
 *
 *   // Or execute all at once
 *   st_vm_run(&vm, MAX_STEPS);
 *
 *   // Access results
 *   int result = vm.variables[0].int_val;
 */

#ifndef ST_VM_H
#define ST_VM_H

#include "st_types.h"

/* VM execution state */
typedef struct {
  // Bytecode being executed
  const st_bytecode_program_t *program;

  // Execution state
  uint16_t pc;                // Program counter
  uint8_t halted;             // Execution halted (HALT instruction)
  uint8_t error;              // Error flag
  char error_msg[256];        // Error message

  // Stack (for expression evaluation)
  st_value_t stack[64];       // Value stack (max 64 depth)
  st_datatype_t type_stack[64]; // Type stack (BUG-050: track value types for arithmetic)
  uint8_t sp;                 // Stack pointer (index of next free slot)

  // Variable storage (local to this execution)
  st_value_t variables[32];   // Local variables (mirrors bytecode->variables)
  uint8_t var_count;

  // FEAT-005: STRING storage. string_vars mirrors program->string_vars (copied
  // in st_vm_init(), copied back to the program after execution — same
  // pattern as `variables[]` above). string_scratch is purely transient
  // (intermediate CONCAT/LEFT/RIGHT/MID results), reset each execution and
  // never copied back anywhere; string_scratch_cursor round-robins through
  // it as expressions are evaluated (see st_vm_string_scratch_alloc()).
  char string_vars[ST_MAX_STRING_VARS][ST_MAX_STRING_LEN + 1];
  char string_scratch[ST_MAX_STRING_SCRATCH][ST_MAX_STRING_LEN + 1];
  uint8_t string_scratch_cursor;

  // FEAT-003: Call stack for user-defined functions
  st_call_frame_t call_stack[8];  // Max ST_MAX_CALL_DEPTH nested calls
  uint8_t call_depth;             // Current call depth (0 = main program)
  st_value_t local_vars[64];      // Local variable storage for functions
  st_datatype_t local_types[64];  // Types for local variables
  uint8_t local_base;             // Current local variable base index
  const st_function_registry_t *func_registry;  // Function registry (NULL if no user functions)

  // Execution statistics (optional)
  uint32_t step_count;        // Total steps executed
  uint32_t max_stack_depth;   // Peak stack usage
} st_vm_t;

/**
 * @brief Initialize VM with bytecode program
 * @param vm VM state
 * @param program Compiled bytecode program
 */
void st_vm_init(st_vm_t *vm, const st_bytecode_program_t *program);

/**
 * @brief Execute one instruction and advance PC
 * @param vm VM state
 * @return true if continue, false if halted or error
 */
bool st_vm_step(st_vm_t *vm);

/**
 * @brief Execute all instructions until halt or error
 * @param vm VM state
 * @param max_steps Maximum steps to execute (0 = unlimited)
 * @return true if completed successfully, false if error
 */
bool st_vm_run(st_vm_t *vm, uint32_t max_steps);

/**
 * @brief Reset VM to initial state (keeps program reference)
 * @param vm VM state
 */
void st_vm_reset(st_vm_t *vm);

/**
 * @brief Get variable value by name
 * @param vm VM state
 * @param var_index Variable index
 * @return Variable value
 */
st_value_t st_vm_get_variable(st_vm_t *vm, uint8_t var_index);

/**
 * @brief Set variable value by index
 * @param vm VM state
 * @param var_index Variable index
 * @param value New value
 */
void st_vm_set_variable(st_vm_t *vm, uint8_t var_index, st_value_t value);

/**
 * @brief Push value onto stack
 * @param vm VM state
 * @param value Value to push
 * @return true if successful, false if stack overflow
 */
bool st_vm_push(st_vm_t *vm, st_value_t value);

/**
 * @brief Pop value from stack
 * @param vm VM state
 * @param out_value Pointer to store popped value
 * @return true if successful, false if stack underflow
 */
bool st_vm_pop(st_vm_t *vm, st_value_t *out_value);

/**
 * @brief Peek at top of stack without popping
 * @param vm VM state
 * @return Top stack value (undefined if stack empty)
 */
st_value_t st_vm_peek(st_vm_t *vm);

/**
 * @brief FEAT-005: Resolve a STRING value's str_ref to its actual C-string
 * content, regardless of kind (variable/literal/scratch).
 * @param vm VM state (for variable/scratch lookup)
 * @param value A st_value_t whose type is ST_TYPE_STRING
 * @return Pointer to a NUL-terminated string (never NULL — points into vm/
 *   program storage, valid only as long as the VM/program outlive it)
 */
const char *st_vm_string_resolve(st_vm_t *vm, st_value_t value);

/**
 * @brief FEAT-005: Allocate the next scratch slot (round-robin) and copy
 * `text` into it (truncated to ST_MAX_STRING_LEN). Used by builtins (CONCAT/
 * LEFT/RIGHT/MID) to produce a new, temporary STRING result.
 * @return A st_value_t with type-appropriate str_ref pointing at the new slot
 */
st_value_t st_vm_string_scratch_alloc(st_vm_t *vm, const char *text);

/**
 * @brief Print VM state (for debugging)
 * @param vm VM state
 */
void st_vm_print_state(st_vm_t *vm);

/**
 * @brief Print stack contents
 * @param vm VM state
 */
void st_vm_print_stack(st_vm_t *vm);

/**
 * @brief Print variables
 * @param vm VM state
 */
void st_vm_print_variables(st_vm_t *vm);

#endif // ST_VM_H
