/**
 * @file st_vm.cpp
 * @brief Structured Text Virtual Machine Implementation
 *
 * Stack-based interpreter for bytecode execution.
 */

#include "st_vm.h"
#include "st_logic_config.h"   // FEAT-007: GLOBAL_VAR storage (st_logic_get_state())
#include "st_logic_engine.h"   // FEAT-007: st_logic_lock_variables()/_unlock_variables()
#include "st_builtins.h"
#include "st_builtin_modbus.h"
#include "st_builtin_modbus_expansion.h"  // FEAT-410
#include "st_stateful.h"  // For st_stateful_storage_t cast
#include "st_builtin_edge.h"
#include "st_builtin_timers.h"
#include "st_builtin_counters.h"
#include "st_builtin_latch.h"  // v4.7.3: SR/RS latches
#include "st_builtin_signal.h"  // v4.8: Signal processing
#include "counter_engine.h"     // v7.7.2: HW counter access
#include "counter_config.h"     // v7.7.2: Counter config get/set
#include "counter_frequency.h"  // v7.7.2: Frequency read
#include "registers.h"          // v7.7.2: Register read/write for CNT_CTRL/STATUS
#include "constants.h"          // v7.7.2: COUNTER_COUNT, HOLDING_REGS_SIZE
#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

/* ============================================================================
 * FEAT-121: TIME type helper — TIME is semantically identical to DINT
 * Normalize TIME to DINT so all arithmetic/comparison logic works unchanged.
 * ============================================================================ */
static inline st_datatype_t st_normalize_type(st_datatype_t t) {
  return (t == ST_TYPE_TIME) ? ST_TYPE_DINT : t;
}

/* ============================================================================
 * INITIALIZATION & RESET
 * ============================================================================ */

void st_vm_init(st_vm_t *vm, const st_bytecode_program_t *program) {
  memset(vm, 0, sizeof(*vm));
  vm->program = program;
  vm->pc = 0;
  vm->sp = 0;
  vm->halted = 0;
  vm->error = 0;
  vm->var_count = program ? program->var_count : 0;

  // Initialize variables from program
  if (program && program->var_count > 0) {
    memcpy(vm->variables, program->variables, program->var_count * sizeof(st_value_t));
  }

  // FEAT-005: STRING variable content (string_scratch stays zeroed — pure
  // per-execution temp storage, memset(0) above already cleared it).
  if (program) {
    memcpy(vm->string_vars, program->string_vars, sizeof(vm->string_vars));
  }

  // FEAT-003: Initialize call stack for user-defined functions
  vm->call_depth = 0;
  vm->local_base = 0;
  vm->func_registry = NULL;  // Set externally if user functions are used
}

void st_vm_reset(st_vm_t *vm) {
  if (!vm->program) return;

  vm->pc = 0;
  vm->sp = 0;
  vm->halted = 0;
  vm->error = 0;
  vm->step_count = 0;
  memset(vm->stack, 0, sizeof(vm->stack));
  memcpy(vm->variables, vm->program->variables, vm->var_count * sizeof(st_value_t));
  memcpy(vm->string_vars, vm->program->string_vars, sizeof(vm->string_vars));  // FEAT-005
  memset(vm->string_scratch, 0, sizeof(vm->string_scratch));
  vm->string_scratch_cursor = 0;
}

/* ============================================================================
 * STACK OPERATIONS
 * ============================================================================ */

// BUG-050: Type-aware push (internal helper)
static bool st_vm_push_typed(st_vm_t *vm, st_value_t value, st_datatype_t type) {
  if (vm->sp >= 64) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack overflow (max 64)");
    vm->error = 1;
    return false;
  }

  vm->stack[vm->sp] = value;
  vm->type_stack[vm->sp] = type;
  vm->sp++;

  if (vm->sp > vm->max_stack_depth) {
    vm->max_stack_depth = vm->sp;
  }

  return true;
}

bool st_vm_push(st_vm_t *vm, st_value_t value) {
  // Legacy: assume INT type
  return st_vm_push_typed(vm, value, ST_TYPE_INT);
}

// BUG-050: Type-aware pop (internal helper)
static bool st_vm_pop_typed(st_vm_t *vm, st_value_t *out_value, st_datatype_t *out_type) {
  if (vm->sp == 0) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack underflow");
    vm->error = 1;
    return false;
  }

  vm->sp--;
  *out_value = vm->stack[vm->sp];
  *out_type = vm->type_stack[vm->sp];
  return true;
}

bool st_vm_pop(st_vm_t *vm, st_value_t *out_value) {
  st_datatype_t dummy_type;
  return st_vm_pop_typed(vm, out_value, &dummy_type);
}

st_value_t st_vm_peek(st_vm_t *vm) {
  if (vm->sp == 0) {
    st_value_t empty = {0};
    return empty;
  }
  return vm->stack[vm->sp - 1];
}

/* ============================================================================
 * FEAT-005: STRING SUPPORT
 * ============================================================================ */

const char *st_vm_string_resolve(st_vm_t *vm, st_value_t value) {
  static const char empty_str[1] = "";
  uint8_t kind = ST_STR_REF_KIND(value.str_ref);
  uint8_t idx = ST_STR_REF_INDEX(value.str_ref);

  switch (kind) {
    case ST_STR_REF_KIND_VAR:
      if (idx >= ST_MAX_STRING_VARS) return empty_str;
      return vm->string_vars[idx];
    case ST_STR_REF_KIND_LITERAL:
      if (!vm->program || idx >= vm->program->string_literal_count) return empty_str;
      return vm->program->string_literals[idx];
    case ST_STR_REF_KIND_SCRATCH:
      if (idx >= ST_MAX_STRING_SCRATCH) return empty_str;
      return vm->string_scratch[idx];
    default:
      return empty_str;
  }
}

st_value_t st_vm_string_scratch_alloc(st_vm_t *vm, const char *text) {
  uint8_t idx = vm->string_scratch_cursor;
  vm->string_scratch_cursor = (uint8_t)((vm->string_scratch_cursor + 1) % ST_MAX_STRING_SCRATCH);

  strncpy(vm->string_scratch[idx], text ? text : "", ST_MAX_STRING_LEN);
  vm->string_scratch[idx][ST_MAX_STRING_LEN] = '\0';

  st_value_t ref;
  memset(&ref, 0, sizeof(ref));
  ref.str_ref = ST_STR_REF_MAKE(ST_STR_REF_KIND_SCRATCH, idx);
  return ref;
}

/* ============================================================================
 * VARIABLE OPERATIONS
 * ============================================================================ */

st_value_t st_vm_get_variable(st_vm_t *vm, uint8_t var_index) {
  st_value_t empty = {0};
  if (var_index >= vm->var_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Variable index out of bounds: %d", var_index);
    vm->error = 1;
    return empty;
  }
  return vm->variables[var_index];
}

void st_vm_set_variable(st_vm_t *vm, uint8_t var_index, st_value_t value) {
  if (var_index >= vm->var_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Variable index out of bounds: %d", var_index);
    vm->error = 1;
    return;
  }
  vm->variables[var_index] = value;
}

/* ============================================================================
 * INSTRUCTION EXECUTION
 * ============================================================================ */

static bool st_vm_exec_push_bool(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  val.bool_val = (instr->arg.int_arg != 0);
  return st_vm_push_typed(vm, val, ST_TYPE_BOOL);  // BUG-050
}

static bool st_vm_exec_push_int(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  memset(&val, 0, sizeof(val));  // Initialize union to all zeros
  val.int_val = instr->arg.int_arg;
  return st_vm_push_typed(vm, val, ST_TYPE_INT);  // BUG-050
}

static bool st_vm_exec_push_dword(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  val.dword_val = (uint32_t)instr->arg.int_arg;
  return st_vm_push_typed(vm, val, ST_TYPE_DWORD);  // BUG-050
}

static bool st_vm_exec_push_real(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  memset(&val, 0, sizeof(val));  // BUG-050: Clear union
  // Retrieve float from int bits (hack from compiler)
  memcpy(&val.real_val, &instr->arg.int_arg, sizeof(float));
  return st_vm_push_typed(vm, val, ST_TYPE_REAL);  // BUG-050
}

// FEAT-005: push a compile-time STRING literal (int_arg = literal table index)
static bool st_vm_exec_push_string_lit(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  memset(&val, 0, sizeof(val));
  uint8_t idx = (uint8_t)instr->arg.int_arg;
  if (!vm->program || idx >= vm->program->string_literal_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Invalid string literal index: %d", idx);
    vm->error = 1;
    return false;
  }
  val.str_ref = ST_STR_REF_MAKE(ST_STR_REF_KIND_LITERAL, idx);
  return st_vm_push_typed(vm, val, ST_TYPE_STRING);
}

// FEAT-007: IEC 61131-3 implicit-conversion rules on assignment, factored
// out of STORE_VAR so STORE_GLOBAL can apply the identical rules — the two
// paths must never silently drift apart on what "assign INT to a REAL"
// (etc.) means.
static st_value_t st_vm_convert_value(st_value_t val, st_datatype_t val_type, st_datatype_t target_type) {
  st_value_t converted_val = val;

  // FEAT-121: Normalize TIME to DINT for conversion logic (same representation)
  if (val_type == ST_TYPE_TIME) val_type = ST_TYPE_DINT;
  st_datatype_t norm_target = (target_type == ST_TYPE_TIME) ? ST_TYPE_DINT : target_type;

  if (val_type == norm_target) return val;

  // REAL -> INT: Truncate to 16-bit
  if (val_type == ST_TYPE_REAL && norm_target == ST_TYPE_INT) {
    int32_t temp = (int32_t)val.real_val;
    if (temp > INT16_MAX) temp = INT16_MAX;
    if (temp < INT16_MIN) temp = INT16_MIN;
    converted_val.int_val = (int16_t)temp;
  }
  // REAL -> DINT: Truncate to 32-bit
  else if (val_type == ST_TYPE_REAL && norm_target == ST_TYPE_DINT) {
    converted_val.dint_val = (int32_t)val.real_val;
  }
  // REAL -> BOOL: Non-zero = TRUE
  else if (val_type == ST_TYPE_REAL && norm_target == ST_TYPE_BOOL) {
    converted_val.bool_val = (val.real_val != 0.0f);
  }
  // DINT -> INT: Clamp to INT16 range
  else if (val_type == ST_TYPE_DINT && norm_target == ST_TYPE_INT) {
    int32_t temp = val.dint_val;
    if (temp > INT16_MAX) temp = INT16_MAX;
    if (temp < INT16_MIN) temp = INT16_MIN;
    converted_val.int_val = (int16_t)temp;
  }
  // DINT -> REAL: Convert to float
  else if (val_type == ST_TYPE_DINT && norm_target == ST_TYPE_REAL) {
    converted_val.real_val = (float)val.dint_val;
  }
  // INT -> REAL: Convert to float
  else if (val_type == ST_TYPE_INT && norm_target == ST_TYPE_REAL) {
    converted_val.real_val = (float)val.int_val;
  }
  // INT -> DINT: Sign-extend to 32-bit
  else if (val_type == ST_TYPE_INT && norm_target == ST_TYPE_DINT) {
    converted_val.dint_val = (int32_t)val.int_val;
  }
  // INT -> BOOL: Non-zero = TRUE
  else if (val_type == ST_TYPE_INT && norm_target == ST_TYPE_BOOL) {
    converted_val.bool_val = (val.int_val != 0);
  }
  // BOOL -> INT: TRUE=1, FALSE=0
  else if (val_type == ST_TYPE_BOOL && norm_target == ST_TYPE_INT) {
    converted_val.int_val = val.bool_val ? 1 : 0;
  }
  // BOOL -> REAL: TRUE=1.0, FALSE=0.0
  else if (val_type == ST_TYPE_BOOL && norm_target == ST_TYPE_REAL) {
    converted_val.real_val = val.bool_val ? 1.0f : 0.0f;
  }
  // BUG-381: DWORD conversions — previously absent from this table, silently
  // falling through to a raw union-bit-copy. That happens to be correct for
  // DWORD<->DINT (same 32-bit width, same union slot), but corrupts
  // DWORD<->BOOL (bool_val only reads the first byte — a DWORD like 0x100
  // would wrongly read as FALSE) and DWORD<->REAL (reinterprets bits instead
  // of converting the numeric value).
  // DWORD -> INT: clamp to INT16 range (DWORD is unsigned, no negative clamp needed)
  else if (val_type == ST_TYPE_DWORD && norm_target == ST_TYPE_INT) {
    uint32_t temp = val.dword_val;
    converted_val.int_val = (temp > (uint32_t)INT16_MAX) ? INT16_MAX : (int16_t)temp;
  }
  // INT -> DWORD: sign-extend to 32-bit, then reinterpret as unsigned (e.g. -1 -> 0xFFFFFFFF)
  else if (val_type == ST_TYPE_INT && norm_target == ST_TYPE_DWORD) {
    converted_val.dword_val = (uint32_t)(int32_t)val.int_val;
  }
  // DWORD -> DINT / DINT -> DWORD: same 32-bit width, reinterpret bit pattern (matches
  // the project's established tolerance for silent-wrapping arithmetic, BUG-172)
  else if (val_type == ST_TYPE_DWORD && norm_target == ST_TYPE_DINT) {
    converted_val.dint_val = (int32_t)val.dword_val;
  }
  else if (val_type == ST_TYPE_DINT && norm_target == ST_TYPE_DWORD) {
    converted_val.dword_val = (uint32_t)val.dint_val;
  }
  // DWORD -> REAL: convert numeric value (unsigned) to float
  else if (val_type == ST_TYPE_DWORD && norm_target == ST_TYPE_REAL) {
    converted_val.real_val = (float)val.dword_val;
  }
  // REAL -> DWORD: truncate; negative values clamp to 0 (DWORD is unsigned)
  else if (val_type == ST_TYPE_REAL && norm_target == ST_TYPE_DWORD) {
    converted_val.dword_val = (val.real_val < 0.0f) ? 0u : (uint32_t)val.real_val;
  }
  // DWORD -> BOOL: non-zero = TRUE
  else if (val_type == ST_TYPE_DWORD && norm_target == ST_TYPE_BOOL) {
    converted_val.bool_val = (val.dword_val != 0);
  }
  // BOOL -> DWORD: TRUE=1, FALSE=0
  else if (val_type == ST_TYPE_BOOL && norm_target == ST_TYPE_DWORD) {
    converted_val.dword_val = val.bool_val ? 1u : 0u;
  }
  else {
    // No conversion needed or unsupported conversion (use value as-is)
    converted_val = val;
  }

  return converted_val;
}

static bool st_vm_exec_load_var(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val = st_vm_get_variable(vm, instr->arg.var_index);
  if (vm->error) return false;
  // BUG-050: Push with correct type from program
  st_datatype_t var_type = vm->program->var_types[instr->arg.var_index];
  return st_vm_push_typed(vm, val, var_type);
}

// FEAT-007: GLOBAL_VAR — load a variable shared across Logic1-4 from
// st_logic_engine_state_t.globals[] (NOT this program's own variables[]).
static bool st_vm_exec_load_global(st_vm_t *vm, st_bytecode_instr_t *instr) {
  uint8_t idx = (uint8_t)instr->arg.var_index;
  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state || idx >= state->global_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Invalid GLOBAL_VAR index: %d", idx);
    vm->error = 1;
    return false;
  }

  st_logic_lock_variables();
  st_value_t val = state->globals[idx].value;
  st_datatype_t type = state->globals[idx].type;
  st_logic_unlock_variables();

  return st_vm_push_typed(vm, val, type);
}

// FEAT-007: GLOBAL_VAR — store to a variable shared across Logic1-4.
// Applies the same implicit type conversion as STORE_VAR (st_vm_convert_value).
static bool st_vm_exec_store_global(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  st_datatype_t val_type;
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  uint8_t idx = (uint8_t)instr->arg.var_index;
  st_logic_engine_state_t *state = st_logic_get_state();
  if (!state || idx >= state->global_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Invalid GLOBAL_VAR index: %d", idx);
    vm->error = 1;
    return false;
  }

  st_logic_lock_variables();
  st_datatype_t target_type = state->globals[idx].type;
  state->globals[idx].value = st_vm_convert_value(val, val_type, target_type);
  st_logic_unlock_variables();

  return true;
}

static bool st_vm_exec_store_var(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  st_datatype_t val_type;

  // BUG-105: Pop with type information for automatic type conversion
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  // Get target variable type
  st_datatype_t var_type = vm->program->var_types[instr->arg.var_index];

  // FEAT-005: STRING assignment — copy the actual CHARACTERS into this
  // variable's OWN slot (vm->string_vars[idx]), never just alias another
  // slot's str_ref — the source (a literal, a scratch temp, or another
  // variable) could change independently afterwards, which would otherwise
  // silently corrupt what this variable "sees" on its next read.
  if (var_type == ST_TYPE_STRING) {
    if (val_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Cannot assign non-STRING value to STRING variable");
      vm->error = 1;
      return false;
    }
    uint8_t idx = (uint8_t)instr->arg.var_index;
    const char *src = st_vm_string_resolve(vm, val);
    strncpy(vm->string_vars[idx], src, ST_MAX_STRING_LEN);
    vm->string_vars[idx][ST_MAX_STRING_LEN] = '\0';

    st_value_t self_ref;
    memset(&self_ref, 0, sizeof(self_ref));
    self_ref.str_ref = ST_STR_REF_MAKE(ST_STR_REF_KIND_VAR, idx);
    st_vm_set_variable(vm, instr->arg.var_index, self_ref);
    return !vm->error;
  }

  // Automatic type conversion on assignment (IEC 61131-3 implicit conversion)
  st_value_t converted_val = st_vm_convert_value(val, val_type, var_type);

  st_vm_set_variable(vm, instr->arg.var_index, converted_val);
  return !vm->error;
}

// FEAT-004: Load array element
static bool st_vm_exec_load_array(st_vm_t *vm, st_bytecode_instr_t *instr) {
  // Pop index from stack
  st_value_t idx_val;
  st_datatype_t idx_type;
  if (!st_vm_pop_typed(vm, &idx_val, &idx_type)) return false;

  // Convert index to integer
  int32_t index;
  if (idx_type == ST_TYPE_INT) index = idx_val.int_val;
  else if (idx_type == ST_TYPE_DINT) index = idx_val.dint_val;
  else if (idx_type == ST_TYPE_BOOL) index = idx_val.bool_val ? 1 : 0;
  else {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Array index must be integer");
    vm->error = 1;
    return false;
  }

  uint8_t base = instr->arg.array_op.base_index;
  uint8_t size = instr->arg.array_op.array_size;
  int8_t lower = instr->arg.array_op.lower_bound;

  int32_t offset = index - lower;
  if (offset < 0 || offset >= size) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Array index %ld out of bounds [%d..%d]", (long)index, lower, lower + size - 1);
    vm->error = 1;
    return false;
  }

  uint8_t var_idx = base + (uint8_t)offset;
  st_value_t val = st_vm_get_variable(vm, var_idx);
  if (vm->error) return false;
  st_datatype_t var_type = vm->program->var_types[var_idx];
  return st_vm_push_typed(vm, val, var_type);
}

// FEAT-004: Store array element
static bool st_vm_exec_store_array(st_vm_t *vm, st_bytecode_instr_t *instr) {
  // Pop index from stack
  st_value_t idx_val;
  st_datatype_t idx_type;
  if (!st_vm_pop_typed(vm, &idx_val, &idx_type)) return false;

  // Convert index to integer
  int32_t index;
  if (idx_type == ST_TYPE_INT) index = idx_val.int_val;
  else if (idx_type == ST_TYPE_DINT) index = idx_val.dint_val;
  else if (idx_type == ST_TYPE_BOOL) index = idx_val.bool_val ? 1 : 0;
  else {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Array index must be integer");
    vm->error = 1;
    return false;
  }

  // Pop value from stack
  st_value_t val;
  st_datatype_t val_type;
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  uint8_t base = instr->arg.array_op.base_index;
  uint8_t size = instr->arg.array_op.array_size;
  int8_t lower = instr->arg.array_op.lower_bound;

  int32_t offset = index - lower;
  if (offset < 0 || offset >= size) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Array index %ld out of bounds [%d..%d]", (long)index, lower, lower + size - 1);
    vm->error = 1;
    return false;
  }

  uint8_t var_idx = base + (uint8_t)offset;

  // BUG-381: Reuse the shared conversion helper (same one STORE_VAR/STORE_GLOBAL
  // use) instead of a hand-duplicated, incomplete copy of the same rules — the
  // old inline copy silently skipped several conversion pairs (e.g. BOOL/DWORD,
  // REAL<->DINT, anything DWORD-related) when storing into an array element.
  st_datatype_t var_type = vm->program->var_types[var_idx];
  st_value_t converted_val = st_vm_convert_value(val, val_type, var_type);

  st_vm_set_variable(vm, var_idx, converted_val);
  return !vm->error;
}

static bool st_vm_exec_dup(st_vm_t *vm, st_bytecode_instr_t *instr) {
  // Duplicate the top stack value
  if (vm->sp == 0) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack underflow (DUP)");
    vm->error = 1;
    return false;
  }
  st_value_t top = vm->stack[vm->sp - 1];
  st_datatype_t top_type = vm->type_stack[vm->sp - 1];  // BUG-072: Preserve type
  return st_vm_push_typed(vm, top, top_type);
}

static bool st_vm_exec_pop(st_vm_t *vm, st_bytecode_instr_t *instr) {
  // Pop and discard top stack value
  st_value_t discard;
  return st_vm_pop(vm, &discard);
}

/* ============================================================================
 * ARITHMETIC OPERATIONS
 * ============================================================================ */

static bool st_vm_exec_add(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-174 FIX: Validate that operands are not BOOL (arithmetic on BOOL not allowed)
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type (use BOOL_TO_INT for conversion)");
    return false;
  }

  // BUG-172 NOTE: Integer overflow behavior
  // This implementation uses C standard wrapping behavior (two's complement wrap-around).
  // IEC 61131-3 allows implementations to choose between:
  //   1. Wrapping (what we use - fastest, standard C behavior)
  //   2. Saturation/clamping (slower, requires checks)
  //   3. Exception/error (slowest, interrupts execution)
  // Design choice: Wrapping for performance on embedded systems.
  // Examples: INT: 32767 + 1 = -32768, DINT: 2147483647 + 1 = -2147483648

  // REAL type promotion
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    // Convert operands to REAL
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.real_val = left_f + right_f;

    // BUG-160 FIX: Validate NaN/INF
    if (isnan(result.real_val) || isinf(result.real_val)) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Arithmetic overflow (NaN/INF in ADD)");
      return false;
    }

    return st_vm_push_typed(vm, result, ST_TYPE_REAL);
  }

  // BUG-397c FIX: DWORD used to have no branch here at all and silently fell
  // through to the 16-bit INT path below, losing bits 16-31 entirely (same
  // bug class as BUG-397's SHL/SHR gap). 32-bit arithmetic is done in
  // uint32_t throughout (addition's bit pattern is identical whether the
  // result is interpreted as signed DINT or unsigned DWORD afterwards) --
  // the result is only reported as DINT if a DINT operand was actually
  // involved (preserving existing DINT+INT/DINT+DINT behavior exactly),
  // otherwise as DWORD.
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT ||
      left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val :
                      (left_type == ST_TYPE_DINT) ? (uint32_t)left.dint_val :
                      (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val :
                       (right_type == ST_TYPE_DINT) ? (uint32_t)right.dint_val :
                       (uint32_t)(int32_t)right.int_val;
    uint32_t sum = left_u + right_u;  // Wraps on overflow (well-defined for unsigned)
    if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
      result.dint_val = (int32_t)sum;
      return st_vm_push_typed(vm, result, ST_TYPE_DINT);
    }
    result.dword_val = sum;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT + INT = INT (16-bit arithmetic with natural wrapping)
  // BUG-105 FIX: INT is now 16-bit, overflow wraps: 32767 + 1 = -32768
  result.int_val = (int16_t)(left.int_val + right.int_val);  // Cast ensures 16-bit wrap
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

// BUG-159 FIX: Checked addition for FOR loops - detects overflow
static bool st_vm_exec_add_checked(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BOOL not allowed in arithmetic
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type");
    return false;
  }

  // REAL type - use regular ADD (NaN/INF already checked there)
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.real_val = left_f + right_f;
    if (isnan(result.real_val) || isinf(result.real_val)) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "FOR loop overflow (NaN/INF)");
      return false;
    }
    return st_vm_push_typed(vm, result, ST_TYPE_REAL);
  }

  // DINT overflow check (32-bit)
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;

    // Check for signed overflow: (a > 0 && b > 0 && a > MAX - b) || (a < 0 && b < 0 && a < MIN - b)
    if ((right_d > 0 && left_d > INT32_MAX - right_d) ||
        (right_d < 0 && left_d < INT32_MIN - right_d)) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "FOR loop overflow: %ld + %ld exceeds DINT range", (long)left_d, (long)right_d);
      return false;
    }
    result.dint_val = left_d + right_d;
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
  }

  // BUG-397c FIX: DWORD overflow check (32-bit unsigned) -- a DWORD-typed
  // FOR-loop variable is unusual but previously fell through to the 16-bit
  // INT branch below with no overflow detection at all for the upper 16
  // bits. Unsigned overflow only happens "upward" (no negative operand
  // case), so the check is simpler than DINT's.
  if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;

    if (right_u > 0 && left_u > UINT32_MAX - right_u) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "FOR loop overflow: %lu + %lu exceeds DWORD range", (unsigned long)left_u, (unsigned long)right_u);
      return false;
    }
    result.dword_val = left_u + right_u;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT overflow check (16-bit) - primary use case for BUG-159
  int32_t sum = (int32_t)left.int_val + (int32_t)right.int_val;
  if (sum > INT16_MAX || sum < INT16_MIN) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "FOR loop overflow: %d + %d = %ld exceeds INT range [-32768, 32767]",
             left.int_val, right.int_val, (long)sum);
    return false;
  }
  result.int_val = (int16_t)sum;
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

static bool st_vm_exec_sub(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-174 FIX: Validate that operands are not BOOL
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type (use BOOL_TO_INT for conversion)");
    return false;
  }

  // REAL type promotion
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    // Convert operands to REAL
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.real_val = left_f - right_f;

    // BUG-160 FIX: Validate NaN/INF
    if (isnan(result.real_val) || isinf(result.real_val)) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Arithmetic overflow (NaN/INF in SUB)");
      return false;
    }

    return st_vm_push_typed(vm, result, ST_TYPE_REAL);
  }

  // BUG-397c FIX: see st_vm_exec_add() above for the full rationale -- DWORD
  // needs the same 32-bit branch here (unsigned subtraction wraps
  // identically to signed wraparound at the bit level).
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT ||
      left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val :
                      (left_type == ST_TYPE_DINT) ? (uint32_t)left.dint_val :
                      (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val :
                       (right_type == ST_TYPE_DINT) ? (uint32_t)right.dint_val :
                       (uint32_t)(int32_t)right.int_val;
    uint32_t diff = left_u - right_u;  // Wraps on underflow (well-defined for unsigned)
    if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
      result.dint_val = (int32_t)diff;
      return st_vm_push_typed(vm, result, ST_TYPE_DINT);
    }
    result.dword_val = diff;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT - INT = INT (16-bit arithmetic with natural wrapping)
  // BUG-105 FIX: INT is now 16-bit, overflow wraps
  result.int_val = (int16_t)(left.int_val - right.int_val);  // Cast ensures 16-bit wrap
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

static bool st_vm_exec_mul(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-174 FIX: Validate that operands are not BOOL
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type (use BOOL_TO_INT for conversion)");
    return false;
  }

  // REAL type promotion
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    // Convert operands to REAL
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.real_val = left_f * right_f;

    // BUG-160 FIX: Validate NaN/INF
    if (isnan(result.real_val) || isinf(result.real_val)) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Arithmetic overflow (NaN/INF in MUL)");
      return false;
    }

    return st_vm_push_typed(vm, result, ST_TYPE_REAL);
  }

  // BUG-397c FIX: see st_vm_exec_add() above for the full rationale -- DWORD
  // needs the same 32-bit branch here (unsigned multiplication wraps
  // identically to signed wraparound at the bit level).
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT ||
      left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val :
                      (left_type == ST_TYPE_DINT) ? (uint32_t)left.dint_val :
                      (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val :
                       (right_type == ST_TYPE_DINT) ? (uint32_t)right.dint_val :
                       (uint32_t)(int32_t)right.int_val;
    uint32_t prod = left_u * right_u;  // Wraps on overflow (well-defined for unsigned)
    if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
      result.dint_val = (int32_t)prod;
      return st_vm_push_typed(vm, result, ST_TYPE_DINT);
    }
    result.dword_val = prod;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT * INT = INT (16-bit arithmetic with natural wrapping)
  // BUG-105 FIX: INT is now 16-bit, overflow wraps
  result.int_val = (int16_t)(left.int_val * right.int_val);  // Cast ensures 16-bit wrap
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

static bool st_vm_exec_div(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-174 FIX: Validate that operands are not BOOL
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type (use BOOL_TO_INT for conversion)");
    return false;
  }

  // Division always returns REAL (to preserve precision)
  // Convert all types to REAL before division
  float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                 (left_type == ST_TYPE_INT) ? (float)left.int_val :
                 (left_type == ST_TYPE_DINT) ? (float)left.dint_val :
                 (float)left.dword_val;
  float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                  (right_type == ST_TYPE_INT) ? (float)right.int_val :
                  (right_type == ST_TYPE_DINT) ? (float)right.dint_val :
                  (float)right.dword_val;

  if (right_f == 0.0f) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Division by zero");
    vm->error = 1;
    return false;
  }

  result.real_val = left_f / right_f;

  // BUG-160 FIX: Validate NaN/INF
  if (isnan(result.real_val) || isinf(result.real_val)) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Arithmetic overflow (NaN/INF in DIV)");
    return false;
  }

  return st_vm_push_typed(vm, result, ST_TYPE_REAL);
}

static bool st_vm_exec_mod(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-174 FIX: Validate that operands are not BOOL
  if (left_type == ST_TYPE_BOOL || right_type == ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: Arithmetic operation on BOOL type (use BOOL_TO_INT for conversion)");
    return false;
  }

  // BUG-173 NOTE: MOD operation uses C remainder semantics, not mathematical modulo
  // C remainder: sign follows dividend (e.g., -7 % 3 = -1)
  // Math modulo: always positive (e.g., -7 mod 3 = 2)
  // This behavior is standard across most programming languages (C, C++, Java, etc.)

  // DINT % DINT = DINT (32-bit modulo). BUG-397c FIX: a DWORD operand mixed
  // with a DINT is now read via its bit pattern reinterpreted as int32_t
  // (dword_val, not int_val) -- previously this branch only checked
  // left/right_type == DINT and silently read a DWORD operand's low 16 bits
  // via int_val, same class of bug as ADD/SUB/MUL above.
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;

    if (right_d == 0) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Modulo by zero");
      vm->error = 1;
      return false;
    }

    // BUG-083: Handle INT32_MIN % -1 overflow (undefined behavior in C/C++)
    if (left_d == INT32_MIN && right_d == -1) {
      result.dint_val = 0;  // Mathematically correct (INT_MIN % -1 = 0)
      return st_vm_push_typed(vm, result, ST_TYPE_DINT);
    }

    result.dint_val = left_d % right_d;
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
  }

  // BUG-397c FIX: DWORD % DWORD (or DWORD % INT) -- genuinely unsigned
  // modulo, no INT32_MIN/-1 trap exists for unsigned types. Previously fell
  // through to the 16-bit INT branch below and lost bits 16-31.
  if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;

    if (right_u == 0) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Modulo by zero");
      vm->error = 1;
      return false;
    }

    result.dword_val = left_u % right_u;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT % INT = INT (16-bit modulo)
  // BUG-105 FIX: INT is now 16-bit
  if (right.int_val == 0) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Modulo by zero");
    vm->error = 1;
    return false;
  }

  // BUG-083: Handle INT16_MIN % -1 overflow (undefined behavior in C/C++)
  if (left.int_val == INT16_MIN && right.int_val == -1) {
    result.int_val = 0;  // Mathematically correct (INT16_MIN % -1 = 0)
    return st_vm_push_typed(vm, result, ST_TYPE_INT);
  }

  result.int_val = (int16_t)(left.int_val % right.int_val);  // Cast to 16-bit
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

static bool st_vm_exec_neg(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val, result;
  st_datatype_t val_type;

  // BUG-060: Pop with type information
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  // Negate based on type
  if (val_type == ST_TYPE_REAL) {
    result.real_val = -val.real_val;
    return st_vm_push_typed(vm, result, ST_TYPE_REAL);
  } else if (val_type == ST_TYPE_DINT) {
    // BUG-087: Handle INT32_MIN negation (undefined behavior in C/C++)
    if (val.dint_val == INT32_MIN) {
      // -INT32_MIN overflows to INT32_MAX+1, convert to REAL for safe negation
      result.real_val = -(float)val.dint_val;  // 2147483648.0
      return st_vm_push_typed(vm, result, ST_TYPE_REAL);
    }
    result.dint_val = -val.dint_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
  } else if (val_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: DWORD used to fall through to the 16-bit INT branch
    // below. Unlike signed INT/DINT, unsigned negation has no MIN-value
    // overflow trap (unsigned underflow/overflow wraparound is well-defined
    // by the C standard, so this needs no REAL-promotion special case).
    result.dword_val = -val.dword_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  } else {
    // INT type (16-bit)
    // BUG-087 & BUG-105: Handle INT16_MIN negation (undefined behavior in C/C++)
    if (val.int_val == INT16_MIN) {
      // -INT16_MIN overflows to INT16_MAX+1, convert to REAL for safe negation
      result.real_val = -(float)val.int_val;  // 32768.0
      return st_vm_push_typed(vm, result, ST_TYPE_REAL);
    }
    result.int_val = (int16_t)(-val.int_val);  // Cast to 16-bit
    return st_vm_push_typed(vm, result, ST_TYPE_INT);
  }
}

/* ============================================================================
 * LOGICAL OPERATIONS
 * ============================================================================ */

static bool st_vm_exec_and(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-397 FIX: AND only ever read .bool_val regardless of operand type, so
  // e.g. "status AND 16#0020" silently compared truthiness instead of doing
  // a bitwise mask -- wrong result, no error, no warning. Reject non-BOOL
  // operands instead: force BIT_SET/BIT_CLR/BIT_TST for bit-masking, which
  // are the type-aware equivalents provided for exactly this purpose.
  if (left_type != ST_TYPE_BOOL || right_type != ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: AND requires BOOL operands (use BIT_SET/BIT_CLR/BIT_TST for integer bit-masking)");
    return false;
  }

  result.bool_val = (left.bool_val != 0) && (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_or(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-397 FIX: see st_vm_exec_and() above for the full rationale.
  if (left_type != ST_TYPE_BOOL || right_type != ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: OR requires BOOL operands (use BIT_SET/BIT_CLR/BIT_TST for integer bit-masking)");
    return false;
  }

  result.bool_val = (left.bool_val != 0) || (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_xor(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // BUG-397 FIX: see st_vm_exec_and() above for the full rationale.
  if (left_type != ST_TYPE_BOOL || right_type != ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: XOR requires BOOL operands (use BIT_SET/BIT_CLR/BIT_TST for integer bit-masking)");
    return false;
  }

  result.bool_val = (left.bool_val != 0) != (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_not(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val, result;
  st_datatype_t val_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  // BUG-397 FIX: see st_vm_exec_and() above for the full rationale.
  if (val_type != ST_TYPE_BOOL) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Type error: NOT requires a BOOL operand (use BIT_TST for integer bit-testing)");
    return false;
  }

  result.bool_val = (val.bool_val == 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

/* ============================================================================
 * COMPARISON OPERATIONS
 * ============================================================================ */

static bool st_vm_exec_eq(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // FEAT-005: STRING comparison (IEC 61131-3 permits = / <> on STRING)
  if (left_type == ST_TYPE_STRING || right_type == ST_TYPE_STRING) {
    if (left_type != ST_TYPE_STRING || right_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Cannot compare STRING with non-STRING");
      vm->error = 1;
      return false;
    }
    result.bool_val = (strcmp(st_vm_string_resolve(vm, left), st_vm_string_resolve(vm, right)) == 0);
    return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
  }

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f == right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d == right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u == right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val == right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_ne(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // FEAT-005: STRING comparison (IEC 61131-3 permits = / <> on STRING)
  if (left_type == ST_TYPE_STRING || right_type == ST_TYPE_STRING) {
    if (left_type != ST_TYPE_STRING || right_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Cannot compare STRING with non-STRING");
      vm->error = 1;
      return false;
    }
    result.bool_val = (strcmp(st_vm_string_resolve(vm, left), st_vm_string_resolve(vm, right)) != 0);
    return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
  }

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f != right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d != right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u != right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val != right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_lt(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f < right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d < right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u < right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val < right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_gt(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f > right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d > right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u > right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val > right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_le(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f <= right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d <= right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u <= right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val <= right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_ge(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-059: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // If either operand is REAL, compare as REAL
  if (left_type == ST_TYPE_REAL || right_type == ST_TYPE_REAL) {
    float left_f = (left_type == ST_TYPE_REAL) ? left.real_val :
                   (left_type == ST_TYPE_INT) ? (float)left.int_val :
                   (float)left.dint_val;
    float right_f = (right_type == ST_TYPE_REAL) ? right.real_val :
                    (right_type == ST_TYPE_INT) ? (float)right.int_val :
                    (float)right.dint_val;
    result.bool_val = (left_f >= right_f);
  } else if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    // DINT comparison (promote INT to DINT). BUG-397c FIX: a DWORD operand
    // is now read via dword_val reinterpreted as int32_t, not int_val.
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val :
                     (left_type == ST_TYPE_DWORD) ? (int32_t)left.dword_val :
                     (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val :
                      (right_type == ST_TYPE_DWORD) ? (int32_t)right.dword_val :
                      (int32_t)right.int_val;
    result.bool_val = (left_d >= right_d);
  } else if (left_type == ST_TYPE_DWORD || right_type == ST_TYPE_DWORD) {
    // BUG-397c FIX: genuinely unsigned DWORD comparison -- previously fell
    // through to the 16-bit INT branch below and lost bits 16-31.
    uint32_t left_u = (left_type == ST_TYPE_DWORD) ? left.dword_val : (uint32_t)(int32_t)left.int_val;
    uint32_t right_u = (right_type == ST_TYPE_DWORD) ? right.dword_val : (uint32_t)(int32_t)right.int_val;
    result.bool_val = (left_u >= right_u);
  } else {
    // INT comparison (16-bit)
    result.bool_val = (left.int_val >= right.int_val);
  }
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

/* ============================================================================
 * BITWISE OPERATIONS
 * ============================================================================ */

static bool st_vm_exec_shl(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // DINT shift left (32-bit)
  if (left_type == ST_TYPE_DINT) {
    // BUG-073: Check shift amount (undefined behavior if >= 32)
    if (right.int_val < 0 || right.int_val >= 32) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for DINT (0-31)");
      vm->error = 1;
      return false;
    }
    result.dint_val = left.dint_val << right.int_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
  }

  // BUG-397 FIX: DWORD used to silently fall into the INT (16-bit) branch
  // below, which operates on the union's .int_val field -- losing bits
  // 16-31 entirely and silently re-typing the result as INT instead of
  // DWORD. DWORD is a 32-bit unsigned type; give it its own 32-bit branch
  // (unsigned shift, matching its bit-pattern semantics) same as DINT above.
  if (left_type == ST_TYPE_DWORD) {
    if (right.int_val < 0 || right.int_val >= 32) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for DWORD (0-31)");
      vm->error = 1;
      return false;
    }
    result.dword_val = left.dword_val << right.int_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT shift left (16-bit)
  // BUG-073 & BUG-105: Check shift amount (undefined behavior if >= 16)
  if (right.int_val < 0 || right.int_val >= 16) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for INT (0-15)");
    vm->error = 1;
    return false;
  }

  result.int_val = (int16_t)(left.int_val << right.int_val);  // Cast to 16-bit
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

static bool st_vm_exec_shr(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-050: Pop with type information
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  // DINT shift right (32-bit)
  if (left_type == ST_TYPE_DINT) {
    // BUG-073: Check shift amount (undefined behavior if >= 32)
    if (right.int_val < 0 || right.int_val >= 32) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for DINT (0-31)");
      vm->error = 1;
      return false;
    }
    result.dint_val = left.dint_val >> right.int_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
  }

  // BUG-397 FIX: see st_vm_exec_shl() above for the full rationale -- DWORD
  // needs its own 32-bit (unsigned, logical) shift branch instead of
  // silently falling into the 16-bit INT one.
  if (left_type == ST_TYPE_DWORD) {
    if (right.int_val < 0 || right.int_val >= 32) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for DWORD (0-31)");
      vm->error = 1;
      return false;
    }
    result.dword_val = left.dword_val >> right.int_val;
    return st_vm_push_typed(vm, result, ST_TYPE_DWORD);
  }

  // INT shift right (16-bit)
  // BUG-073 & BUG-105: Check shift amount (undefined behavior if >= 16)
  if (right.int_val < 0 || right.int_val >= 16) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "Shift amount out of range for INT (0-15)");
    vm->error = 1;
    return false;
  }

  result.int_val = (int16_t)(left.int_val >> right.int_val);  // Cast to 16-bit
  return st_vm_push_typed(vm, result, ST_TYPE_INT);
}

/* ============================================================================
 * FUNCTION CALLS
 * ============================================================================ */

static bool st_vm_exec_call_builtin(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_builtin_func_t func_id = (st_builtin_func_t)instr->arg.builtin_call.func_id_low;
  uint8_t arg_count = st_builtin_arg_count(func_id);

  st_value_t arg1 = {0}, arg2 = {0}, arg3 = {0}, arg4 = {0}, arg5 = {0}, arg6 = {0};
  st_datatype_t arg1_type = ST_TYPE_INT;
  st_datatype_t arg2_type = ST_TYPE_INT;
  st_datatype_t arg3_type = ST_TYPE_INT;
  st_datatype_t arg4_type = ST_TYPE_INT;
  st_datatype_t arg5_type = ST_TYPE_INT;
  st_datatype_t arg6_type = ST_TYPE_INT;

  // Pop arguments with type information (in reverse order: arg6..arg1)
  // Stack layout: [arg1, arg2, arg3, arg4, arg5, arg6] (top)
  if (arg_count >= 6) {
    if (!st_vm_pop_typed(vm, &arg6, &arg6_type)) return false;
  }
  if (arg_count >= 5) {
    if (!st_vm_pop_typed(vm, &arg5, &arg5_type)) return false;
  }
  if (arg_count >= 4) {
    if (!st_vm_pop_typed(vm, &arg4, &arg4_type)) return false;
  }
  if (arg_count >= 3) {
    if (!st_vm_pop_typed(vm, &arg3, &arg3_type)) return false;
  }
  if (arg_count >= 2) {
    if (!st_vm_pop_typed(vm, &arg2, &arg2_type)) return false;
  }
  if (arg_count >= 1) {
    if (!st_vm_pop_typed(vm, &arg1, &arg1_type)) return false;
  }

  // Call the function (handle 3-arg functions specially)
  st_value_t result;
  if (arg_count == 3) {
    // Special handling for 3-arg functions
    if (func_id == ST_BUILTIN_LIMIT) {
      // BUG-119 FIX: LIMIT is type-polymorphic
      // arg1 = min, arg2 = value, arg3 = max
      if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL || arg3_type == ST_TYPE_REAL) {
        float min_f = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                      (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val : (float)arg1.dint_val;
        float val_f = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                      (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val : (float)arg2.dint_val;
        float max_f = (arg3_type == ST_TYPE_REAL) ? arg3.real_val :
                      (arg3_type == ST_TYPE_INT) ? (float)arg3.int_val : (float)arg3.dint_val;
        result.real_val = (val_f < min_f) ? min_f : ((val_f > max_f) ? max_f : val_f);
      } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT || arg3_type == ST_TYPE_DINT) {
        int32_t min_d = (arg1_type == ST_TYPE_DINT) ? arg1.dint_val : (int32_t)arg1.int_val;
        int32_t val_d = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : (int32_t)arg2.int_val;
        int32_t max_d = (arg3_type == ST_TYPE_DINT) ? arg3.dint_val : (int32_t)arg3.int_val;
        result.dint_val = (val_d < min_d) ? min_d : ((val_d > max_d) ? max_d : val_d);
      } else {
        result.int_val = (arg2.int_val < arg1.int_val) ? arg1.int_val :
                         ((arg2.int_val > arg3.int_val) ? arg3.int_val : arg2.int_val);
      }
    } else if (func_id == ST_BUILTIN_SEL) {
      result = st_builtin_sel(arg1, arg2, arg3);
    } else if (func_id == ST_BUILTIN_MID) {
      // FEAT-005: MID(s, start, len) — 1-baseret start (IEC 61131-3-stil,
      // matcher LEFT/RIGHT's 1-baserede tegn-taelling nedenfor)
      if (arg1_type != ST_TYPE_STRING) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "MID() requires a STRING first argument");
        vm->error = 1;
        return false;
      }
      const char *src = st_vm_string_resolve(vm, arg1);
      int32_t start = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : arg2.int_val;
      int32_t len   = (arg3_type == ST_TYPE_DINT) ? arg3.dint_val : arg3.int_val;
      size_t src_len = strlen(src);
      char buf[ST_MAX_STRING_LEN + 1];
      buf[0] = '\0';
      if (start >= 1 && (size_t)(start - 1) < src_len && len > 0) {
        size_t off = (size_t)(start - 1);
        size_t avail = src_len - off;
        size_t n = ((size_t)len < avail) ? (size_t)len : avail;
        if (n > ST_MAX_STRING_LEN) n = ST_MAX_STRING_LEN;
        memcpy(buf, src + off, n);
        buf[n] = '\0';
      }
      result = st_vm_string_scratch_alloc(vm, buf);
    } else if (func_id == ST_BUILTIN_MB_WRITE_COIL) {
      // BUG-134/136 FIX: Type promotion for all arguments
      // arg1 = slave_id (INT), arg2 = address (INT), arg3 = value (BOOL)
      st_value_t slave_int, addr_int, value_bool;

      // Slave ID: DINT/DWORD → INT with clamping
      if (arg1_type == ST_TYPE_DINT) {
        slave_int.int_val = (arg1.dint_val > 32767) ? 32767 :
                            (arg1.dint_val < -32768) ? -32768 :
                            arg1.dint_val;
      } else if (arg1_type == ST_TYPE_DWORD) {
        slave_int.int_val = (arg1.dword_val > 32767) ? 32767 : arg1.dword_val;
      } else {
        slave_int.int_val = arg1.int_val;  // INT or BOOL → use int_val
      }

      // Address: DINT/DWORD → INT with clamping
      if (arg2_type == ST_TYPE_DINT) {
        addr_int.int_val = (arg2.dint_val > 32767) ? 32767 :
                           (arg2.dint_val < -32768) ? -32768 :
                           arg2.dint_val;
      } else if (arg2_type == ST_TYPE_DWORD) {
        addr_int.int_val = (arg2.dword_val > 32767) ? 32767 : arg2.dword_val;
      } else {
        addr_int.int_val = arg2.int_val;  // INT or BOOL → use int_val
      }

      // Value: INT/REAL/DINT/DWORD → BOOL (non-zero = TRUE)
      if (arg3_type == ST_TYPE_BOOL) {
        value_bool.bool_val = arg3.bool_val;
      } else if (arg3_type == ST_TYPE_INT) {
        value_bool.bool_val = (arg3.int_val != 0);
      } else if (arg3_type == ST_TYPE_DINT) {
        value_bool.bool_val = (arg3.dint_val != 0);
      } else if (arg3_type == ST_TYPE_DWORD) {
        value_bool.bool_val = (arg3.dword_val != 0);
      } else if (arg3_type == ST_TYPE_REAL) {
        value_bool.bool_val = (fabs(arg3.real_val) > 0.001f);
      } else {
        value_bool.bool_val = false;  // Fallback
      }

      result = st_builtin_mb_write_coil(slave_int, addr_int, value_bool);
    } else if (func_id == ST_BUILTIN_MB_WRITE_HOLDING) {
      // BUG-134/135 FIX: Type promotion for all arguments
      // arg1 = slave_id (INT), arg2 = address (INT), arg3 = value (INT)
      st_value_t slave_int, addr_int, value_int;

      // Slave ID: DINT/DWORD → INT with clamping
      if (arg1_type == ST_TYPE_DINT) {
        slave_int.int_val = (arg1.dint_val > 32767) ? 32767 :
                            (arg1.dint_val < -32768) ? -32768 :
                            arg1.dint_val;
      } else if (arg1_type == ST_TYPE_DWORD) {
        slave_int.int_val = (arg1.dword_val > 32767) ? 32767 : arg1.dword_val;
      } else {
        slave_int.int_val = arg1.int_val;  // INT or BOOL → use int_val
      }

      // Address: DINT/DWORD → INT with clamping
      if (arg2_type == ST_TYPE_DINT) {
        addr_int.int_val = (arg2.dint_val > 32767) ? 32767 :
                           (arg2.dint_val < -32768) ? -32768 :
                           arg2.dint_val;
      } else if (arg2_type == ST_TYPE_DWORD) {
        addr_int.int_val = (arg2.dword_val > 32767) ? 32767 : arg2.dword_val;
      } else {
        addr_int.int_val = arg2.int_val;  // INT or BOOL → use int_val
      }

      // Value: REAL/DINT/DWORD/BOOL → INT with conversion
      if (arg3_type == ST_TYPE_REAL) {
        value_int.int_val = (int16_t)arg3.real_val;  // Truncate REAL → INT
      } else if (arg3_type == ST_TYPE_DINT) {
        value_int.int_val = (arg3.dint_val > 32767) ? 32767 :
                            (arg3.dint_val < -32768) ? -32768 :
                            arg3.dint_val;
      } else if (arg3_type == ST_TYPE_DWORD) {
        value_int.int_val = (int16_t)(arg3.dword_val & 0xFFFF);  // Lower 16 bits
      } else if (arg3_type == ST_TYPE_BOOL) {
        value_int.int_val = arg3.bool_val ? 1 : 0;
      } else {
        value_int.int_val = arg3.int_val;  // INT → use directly
      }

      result = st_builtin_mb_write_holding(slave_int, addr_int, value_int);
    } else {
      result = st_builtin_call(func_id, arg1, arg2);
    }
  } else if (func_id == ST_BUILTIN_SUM && arg_count == 2) {
    // BUG-110 FIX: SUM is type-polymorphic like ADD operator
    // REAL type promotion
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL) {
      float left_f = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                     (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                     (float)arg1.dint_val;
      float right_f = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                      (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val :
                      (float)arg2.dint_val;
      result.real_val = left_f + right_f;
    }
    // DINT + DINT = DINT (32-bit arithmetic)
    else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT) {
      int32_t left_d = (arg1_type == ST_TYPE_DINT) ? arg1.dint_val : (int32_t)arg1.int_val;
      int32_t right_d = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : (int32_t)arg2.int_val;
      result.dint_val = left_d + right_d;
    }
    // INT + INT = INT (16-bit arithmetic)
    else {
      result.int_val = (int16_t)(arg1.int_val + arg2.int_val);
    }
  } else if (func_id == ST_BUILTIN_MIN && arg_count == 2) {
    // BUG-117 FIX: MIN is type-polymorphic
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL) {
      float left_f = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                     (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                     (float)arg1.dint_val;
      float right_f = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                      (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val :
                      (float)arg2.dint_val;
      result.real_val = (left_f < right_f) ? left_f : right_f;
    } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT) {
      int32_t left_d = (arg1_type == ST_TYPE_DINT) ? arg1.dint_val : (int32_t)arg1.int_val;
      int32_t right_d = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : (int32_t)arg2.int_val;
      result.dint_val = (left_d < right_d) ? left_d : right_d;
    } else {
      result.int_val = (arg1.int_val < arg2.int_val) ? arg1.int_val : arg2.int_val;
    }
  } else if (func_id == ST_BUILTIN_MAX && arg_count == 2) {
    // BUG-117 FIX: MAX is type-polymorphic
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL) {
      float left_f = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                     (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                     (float)arg1.dint_val;
      float right_f = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                      (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val :
                      (float)arg2.dint_val;
      result.real_val = (left_f > right_f) ? left_f : right_f;
    } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT) {
      int32_t left_d = (arg1_type == ST_TYPE_DINT) ? arg1.dint_val : (int32_t)arg1.int_val;
      int32_t right_d = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : (int32_t)arg2.int_val;
      result.dint_val = (left_d > right_d) ? left_d : right_d;
    } else {
      result.int_val = (arg1.int_val > arg2.int_val) ? arg1.int_val : arg2.int_val;
    }
  } else if (arg_count == 4) {
    // Special handling for 4-arg functions
    if (func_id == ST_BUILTIN_MUX) {
      // MUX(K, IN0, IN1, IN2) - type-polymorphic multiplexer
      // arg1 = K (selector), arg2 = IN0, arg3 = IN1, arg4 = IN2
      result = st_builtin_mux(arg1, arg2, arg3, arg4);
    }
    else if (func_id == ST_BUILTIN_MB_READ_HOLDINGS || func_id == ST_BUILTIN_MB_WRITE_HOLDINGS) {
      // v7.9.2: Multi-register Modbus with array — arg1=slave, arg2=addr, arg3=count, arg4=array_base_index
      st_value_t slave_int, addr_int, count_int;

      // Slave ID: type promotion
      if (arg1_type == ST_TYPE_DINT) {
        slave_int.int_val = (arg1.dint_val > 247) ? 247 : arg1.dint_val;
      } else if (arg1_type == ST_TYPE_DWORD) {
        slave_int.int_val = (arg1.dword_val > 247) ? 247 : arg1.dword_val;
      } else {
        slave_int.int_val = arg1.int_val;
      }

      // Address: type promotion
      if (arg2_type == ST_TYPE_DINT) {
        addr_int.int_val = (arg2.dint_val > 65535) ? 65535 : arg2.dint_val;
      } else if (arg2_type == ST_TYPE_DWORD) {
        addr_int.int_val = (arg2.dword_val > 65535) ? 65535 : arg2.dword_val;
      } else {
        addr_int.int_val = arg2.int_val;
      }

      // Count: clamp to 1-16 (all types, both bounds).
      // SECURITY FIX: previously only DINT/DWORD were clamped, and only on
      // the upper bound — a plain INT count (the default literal/variable
      // type in ST) passed through unclamped entirely, and even the
      // DINT/DWORD "clamp" let a negative count survive the cast to
      // uint8_t below and wrap to up to 255. Both allowed the gather/
      // scatter loop to walk past the 16-entry g_mb_multi_reg_buf global.
      if (arg3_type == ST_TYPE_DINT) {
        count_int.int_val = (arg3.dint_val > 16) ? 16 : (arg3.dint_val < 1) ? 1 : arg3.dint_val;
      } else if (arg3_type == ST_TYPE_DWORD) {
        count_int.int_val = (arg3.dword_val > 16) ? 16 : (arg3.dword_val < 1) ? 1 : arg3.dword_val;
      } else {
        count_int.int_val = (arg3.int_val > 16) ? 16 : (arg3.int_val < 1) ? 1 : arg3.int_val;
      }

      // arg4 = array base variable index (injected by compiler)
      uint8_t arr_base = (uint8_t)arg4.int_val;
      uint8_t cnt = (uint8_t)count_int.int_val;

      if (func_id == ST_BUILTIN_MB_WRITE_HOLDINGS) {
        // Gather values from array variable slots → g_mb_multi_reg_buf
        for (uint8_t i = 0; i < cnt && (arr_base + i) < vm->var_count; i++) {
          g_mb_multi_reg_buf[i] = (uint16_t)vm->variables[arr_base + i].int_val;
        }
        result = st_builtin_mb_write_holdings(slave_int, addr_int, count_int);
      } else {
        // MB_READ_HOLDINGS: queue async read, results will populate array on next cycle
        result = st_builtin_mb_read_holdings(slave_int, addr_int, count_int);
        // Copy current buffer values to array slots (from previous completed read)
        for (uint8_t i = 0; i < cnt && (arr_base + i) < vm->var_count; i++) {
          vm->variables[arr_base + i].int_val = (int16_t)g_mb_multi_reg_buf[i];
        }
      }
    }
    else if (func_id == ST_BUILTIN_MBX_READ_COIL || func_id == ST_BUILTIN_MBX_READ_INPUT ||
             func_id == ST_BUILTIN_MBX_READ_HOLDING || func_id == ST_BUILTIN_MBX_READ_INPUT_REG) {
      // FEAT-410: MBX_READ_*(board, kanal, slave, addr) — arg1=board, arg2=kanal, arg3=slave, arg4=addr.
      // Samme type-promotion-disciplin som MB_WRITE_COIL/HOLDING ovenfor (BUG-134/135/136-lektionen):
      // enhver DINT/DWORD-vaerdi klemmes til INT-range FOER den naar C++-laget, saa et program der (fejlagtigt
      // eller med vilje) sender en DINT/DWORD-literal aldrig kan sende en vaerdi udenfor det tilsigtede omraade.
      st_value_t board_i, ch_i, slave_i, addr_i;

      auto clamp_to_int = [](st_value_t v, st_datatype_t t, int32_t lo, int32_t hi) -> st_value_t {
        st_value_t out;
        int32_t raw = (t == ST_TYPE_DINT) ? v.dint_val : (t == ST_TYPE_DWORD) ? (int32_t)v.dword_val : v.int_val;
        if (raw < lo) raw = lo;
        if (raw > hi) raw = hi;
        out.int_val = (int16_t)raw;
        return out;
      };

      board_i = clamp_to_int(arg1, arg1_type, 0, 255);
      ch_i    = clamp_to_int(arg2, arg2_type, 0, 255);
      slave_i = clamp_to_int(arg3, arg3_type, 0, 255);
      addr_i  = clamp_to_int(arg4, arg4_type, 0, 65535);

      if (func_id == ST_BUILTIN_MBX_READ_COIL) result = st_builtin_mbx_read_coil(board_i, ch_i, slave_i, addr_i);
      else if (func_id == ST_BUILTIN_MBX_READ_INPUT) result = st_builtin_mbx_read_input(board_i, ch_i, slave_i, addr_i);
      else if (func_id == ST_BUILTIN_MBX_READ_HOLDING) result = st_builtin_mbx_read_holding(board_i, ch_i, slave_i, addr_i);
      else result = st_builtin_mbx_read_input_reg(board_i, ch_i, slave_i, addr_i);
    }
  } else if (arg_count == 5 &&
             (func_id == ST_BUILTIN_MBX_WRITE_COIL || func_id == ST_BUILTIN_MBX_WRITE_HOLDING)) {
    // FEAT-410: MBX_WRITE_COIL/HOLDING(board, kanal, slave, addr, value) —
    // arg1=board, arg2=kanal, arg3=slave, arg4=addr, arg5=value.
    st_value_t board_i, ch_i, slave_i, addr_i, value_out;

    auto clamp_to_int5 = [](st_value_t v, st_datatype_t t, int32_t lo, int32_t hi) -> st_value_t {
      st_value_t out;
      int32_t raw = (t == ST_TYPE_DINT) ? v.dint_val : (t == ST_TYPE_DWORD) ? (int32_t)v.dword_val : v.int_val;
      if (raw < lo) raw = lo;
      if (raw > hi) raw = hi;
      out.int_val = (int16_t)raw;
      return out;
    };

    board_i = clamp_to_int5(arg1, arg1_type, 0, 255);
    ch_i    = clamp_to_int5(arg2, arg2_type, 0, 255);
    slave_i = clamp_to_int5(arg3, arg3_type, 0, 255);
    addr_i  = clamp_to_int5(arg4, arg4_type, 0, 65535);

    if (func_id == ST_BUILTIN_MBX_WRITE_COIL) {
      // Value → BOOL (non-zero = TRUE), samme konvertering som MB_WRITE_COIL ovenfor
      if (arg5_type == ST_TYPE_BOOL) value_out.bool_val = arg5.bool_val;
      else if (arg5_type == ST_TYPE_DINT) value_out.bool_val = (arg5.dint_val != 0);
      else if (arg5_type == ST_TYPE_DWORD) value_out.bool_val = (arg5.dword_val != 0);
      else if (arg5_type == ST_TYPE_REAL) value_out.bool_val = (fabs(arg5.real_val) > 0.001f);
      else value_out.bool_val = (arg5.int_val != 0);
      result = st_builtin_mbx_write_coil(board_i, ch_i, slave_i, addr_i, value_out);
    } else {
      // Value → INT, samme konvertering som MB_WRITE_HOLDING ovenfor
      if (arg5_type == ST_TYPE_REAL) value_out.int_val = (int16_t)arg5.real_val;
      else if (arg5_type == ST_TYPE_DINT) value_out.int_val = (arg5.dint_val > 32767) ? 32767 : (arg5.dint_val < -32768) ? -32768 : arg5.dint_val;
      else if (arg5_type == ST_TYPE_DWORD) value_out.int_val = (int16_t)(arg5.dword_val & 0xFFFF);
      else if (arg5_type == ST_TYPE_BOOL) value_out.int_val = arg5.bool_val ? 1 : 0;
      else value_out.int_val = arg5.int_val;
      result = st_builtin_mbx_write_holding(board_i, ch_i, slave_i, addr_i, value_out);
    }
  } else if (func_id == ST_BUILTIN_ROL && arg_count == 2) {
    // ROL: Rotate left (type-dependent)
    result = st_builtin_rol(arg1, arg2, arg1_type);
  } else if (func_id == ST_BUILTIN_ROR && arg_count == 2) {
    // ROR: Rotate right (type-dependent)
    result = st_builtin_ror(arg1, arg2, arg1_type);
  } else if (func_id == ST_BUILTIN_ABS && arg_count == 1) {
    // BUG-118 FIX: ABS is type-polymorphic
    if (arg1_type == ST_TYPE_REAL) {
      result.real_val = (arg1.real_val < 0.0f) ? -arg1.real_val : arg1.real_val;
    } else if (arg1_type == ST_TYPE_DINT) {
      if (arg1.dint_val == INT32_MIN) {
        result.dint_val = INT32_MAX;  // Clamp overflow
      } else {
        result.dint_val = (arg1.dint_val < 0) ? -arg1.dint_val : arg1.dint_val;
      }
    } else {
      if (arg1.int_val == INT16_MIN) {
        result.int_val = INT16_MAX;  // Clamp overflow
      } else {
        result.int_val = (arg1.int_val < 0) ? -arg1.int_val : arg1.int_val;
      }
    }
  }
  // v4.7+: Stateful functions (edge detection, timers, counters)
  else if (func_id == ST_BUILTIN_R_TRIG || func_id == ST_BUILTIN_F_TRIG) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // Edge detection functions
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->edge_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid edge detector instance ID: %d", instance_id);
      return false;
    }
    st_edge_instance_t *instance = &stateful->edges[instance_id];
    if (func_id == ST_BUILTIN_R_TRIG) {
      result = st_builtin_r_trig(arg1, instance);
    } else {
      result = st_builtin_f_trig(arg1, instance);
    }
  }
  else if (func_id == ST_BUILTIN_TON || func_id == ST_BUILTIN_TOF || func_id == ST_BUILTIN_TP) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // Timer functions
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->timer_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid timer instance ID: %d", instance_id);
      return false;
    }
    st_timer_instance_t *instance = &stateful->timers[instance_id];
    if (func_id == ST_BUILTIN_TON) {
      result = st_builtin_ton(arg1, arg2, instance);
    } else if (func_id == ST_BUILTIN_TOF) {
      result = st_builtin_tof(arg1, arg2, instance);
    } else {
      result = st_builtin_tp(arg1, arg2, instance);
    }
  }
  else if (func_id == ST_BUILTIN_CTU || func_id == ST_BUILTIN_CTD || func_id == ST_BUILTIN_CTUD) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // Counter functions
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->counter_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid counter instance ID: %d", instance_id);
      return false;
    }
    st_counter_instance_t *instance = &stateful->counters[instance_id];
    if (func_id == ST_BUILTIN_CTU) {
      result = st_builtin_ctu(arg1, arg2, arg3, instance);
    } else if (func_id == ST_BUILTIN_CTD) {
      result = st_builtin_ctd(arg1, arg2, arg3, instance);
    } else if (func_id == ST_BUILTIN_CTUD) {
      // BUG-150 FIX: CTUD with 5 arguments (CU, CD, RESET, LOAD, PV)
      if (arg_count != 5) {
        snprintf(vm->error_msg, sizeof(vm->error_msg),
                 "CTUD requires 5 arguments, got %d", arg_count);
        return false;
      }
      result = st_builtin_ctud(arg1, arg2, arg3, arg4, arg5, instance);
    } else {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Unknown counter function: %d", func_id);
      return false;
    }
  }
  else if (func_id == ST_BUILTIN_SR || func_id == ST_BUILTIN_RS) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // Latch functions (v4.7.3)
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->latch_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid latch instance ID: %d", instance_id);
      return false;
    }
    st_latch_instance_t *instance = &stateful->latches[instance_id];
    if (func_id == ST_BUILTIN_SR) {
      result = st_builtin_sr(arg1, arg2, instance);
    } else {
      result = st_builtin_rs(arg1, arg2, instance);
    }
  }
  else if (func_id == ST_BUILTIN_SCALE) {
    // BUG-152 FIX: SCALE - type-aware conversion to REAL
    // arg1=IN, arg2=IN_MIN, arg3=IN_MAX, arg4=OUT_MIN, arg5=OUT_MAX
    st_value_t in_real, in_min_real, in_max_real, out_min_real, out_max_real;

    // Convert arg1 (IN) to REAL
    in_real.real_val = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                       (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                       (arg1_type == ST_TYPE_DINT) ? (float)arg1.dint_val :
                       (float)arg1.dword_val;

    // Convert arg2 (IN_MIN) to REAL
    in_min_real.real_val = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                           (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val :
                           (arg2_type == ST_TYPE_DINT) ? (float)arg2.dint_val :
                           (float)arg2.dword_val;

    // Convert arg3 (IN_MAX) to REAL
    in_max_real.real_val = (arg3_type == ST_TYPE_REAL) ? arg3.real_val :
                           (arg3_type == ST_TYPE_INT) ? (float)arg3.int_val :
                           (arg3_type == ST_TYPE_DINT) ? (float)arg3.dint_val :
                           (float)arg3.dword_val;

    // Convert arg4 (OUT_MIN) to REAL
    out_min_real.real_val = (arg4_type == ST_TYPE_REAL) ? arg4.real_val :
                            (arg4_type == ST_TYPE_INT) ? (float)arg4.int_val :
                            (arg4_type == ST_TYPE_DINT) ? (float)arg4.dint_val :
                            (float)arg4.dword_val;

    // Convert arg5 (OUT_MAX) to REAL
    out_max_real.real_val = (arg5_type == ST_TYPE_REAL) ? arg5.real_val :
                            (arg5_type == ST_TYPE_INT) ? (float)arg5.int_val :
                            (arg5_type == ST_TYPE_DINT) ? (float)arg5.dint_val :
                            (float)arg5.dword_val;

    result = st_builtin_scale(in_real, in_min_real, in_max_real, out_min_real, out_max_real);
  }
  else if (func_id == ST_BUILTIN_HYSTERESIS) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // BUG-152 FIX: HYSTERESIS - type-aware conversion to REAL
    // arg1=IN, arg2=HIGH, arg3=LOW
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->hysteresis_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid hysteresis instance ID: %d", instance_id);
      return false;
    }

    st_value_t in_real, high_real, low_real;

    // Convert arg1 (IN) to REAL
    in_real.real_val = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                       (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                       (arg1_type == ST_TYPE_DINT) ? (float)arg1.dint_val :
                       (float)arg1.dword_val;

    // Convert arg2 (HIGH) to REAL
    high_real.real_val = (arg2_type == ST_TYPE_REAL) ? arg2.real_val :
                         (arg2_type == ST_TYPE_INT) ? (float)arg2.int_val :
                         (arg2_type == ST_TYPE_DINT) ? (float)arg2.dint_val :
                         (float)arg2.dword_val;

    // Convert arg3 (LOW) to REAL
    low_real.real_val = (arg3_type == ST_TYPE_REAL) ? arg3.real_val :
                        (arg3_type == ST_TYPE_INT) ? (float)arg3.int_val :
                        (arg3_type == ST_TYPE_DINT) ? (float)arg3.dint_val :
                        (float)arg3.dword_val;

    st_hysteresis_instance_t *instance = &stateful->hysteresis[instance_id];
    result = st_builtin_hysteresis(in_real, high_real, low_real, instance);
  }
  else if (func_id == ST_BUILTIN_BLINK) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // BUG-152 FIX: BLINK - type-aware conversion
    // arg1=ENABLE (BOOL), arg2=ON_TIME (INT ms), arg3=OFF_TIME (INT ms)
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->blink_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid blink instance ID: %d", instance_id);
      return false;
    }

    st_value_t enable_bool, on_time_int, off_time_int;

    // Convert arg1 (ENABLE) to BOOL
    if (arg1_type == ST_TYPE_BOOL) {
      enable_bool.bool_val = arg1.bool_val;
    } else if (arg1_type == ST_TYPE_INT) {
      enable_bool.bool_val = (arg1.int_val != 0);
    } else if (arg1_type == ST_TYPE_DINT) {
      enable_bool.bool_val = (arg1.dint_val != 0);
    } else if (arg1_type == ST_TYPE_DWORD) {
      enable_bool.bool_val = (arg1.dword_val != 0);
    } else if (arg1_type == ST_TYPE_REAL) {
      enable_bool.bool_val = (fabs(arg1.real_val) > 0.001f);
    } else {
      enable_bool.bool_val = false;
    }

    // Convert arg2 (ON_TIME) to INT
    on_time_int.int_val = (arg2_type == ST_TYPE_INT) ? arg2.int_val :
                          (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val :
                          (arg2_type == ST_TYPE_DWORD) ? (int16_t)arg2.dword_val :
                          (arg2_type == ST_TYPE_REAL) ? (int16_t)arg2.real_val :
                          0;

    // Convert arg3 (OFF_TIME) to INT
    off_time_int.int_val = (arg3_type == ST_TYPE_INT) ? arg3.int_val :
                           (arg3_type == ST_TYPE_DINT) ? (int16_t)arg3.dint_val :
                           (arg3_type == ST_TYPE_DWORD) ? (int16_t)arg3.dword_val :
                           (arg3_type == ST_TYPE_REAL) ? (int16_t)arg3.real_val :
                           0;

    st_blink_instance_t *instance = &stateful->blinks[instance_id];
    result = st_builtin_blink(enable_bool, on_time_int, off_time_int, instance);
  }
  else if (func_id == ST_BUILTIN_FILTER) {
    // BUG-158 FIX: Check vm->program first
    if (!vm->program) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
      return false;
    }

    // BUG-152 FIX: FILTER - type-aware conversion
    // arg1=IN (REAL), arg2=TIME_CONSTANT (INT ms)
    uint8_t instance_id = instr->arg.builtin_call.instance_id;
    st_stateful_storage_t *stateful = (st_stateful_storage_t*)vm->program->stateful;
    if (!stateful) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage allocated");
      return false;
    }
    if (instance_id >= stateful->filter_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Invalid filter instance ID: %d", instance_id);
      return false;
    }

    st_value_t in_real, time_constant_int;

    // Convert arg1 (IN) to REAL
    in_real.real_val = (arg1_type == ST_TYPE_REAL) ? arg1.real_val :
                       (arg1_type == ST_TYPE_INT) ? (float)arg1.int_val :
                       (arg1_type == ST_TYPE_DINT) ? (float)arg1.dint_val :
                       (float)arg1.dword_val;

    // Convert arg2 (TIME_CONSTANT) to INT
    time_constant_int.int_val = (arg2_type == ST_TYPE_INT) ? arg2.int_val :
                                (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val :
                                (arg2_type == ST_TYPE_DWORD) ? (int16_t)arg2.dword_val :
                                (arg2_type == ST_TYPE_REAL) ? (int16_t)arg2.real_val :
                                0;

    st_filter_instance_t *instance = &stateful->filters[instance_id];

    // BUG-153 FIX: Pass actual cycle time to filter
    result = st_builtin_filter(in_real, time_constant_int, instance, stateful->cycle_time_ms);
  }
  // v7.7.2: Hardware Counter Access functions
  else if (func_id == ST_BUILTIN_CNT_SETUP) {
    // CNT_SETUP(id, hw_mode, edge, direction, prescaler, gpio) → BOOL
    // arg1=id, arg2=hw_mode, arg3=edge, arg4=direction, arg5=prescaler, arg6=gpio
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.bool_val = false;
    } else {
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);

      // hw_mode: 0=SW, 1=SW_ISR, 2=HW_PCNT
      int16_t hw_mode = (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val : arg2.int_val;
      if (hw_mode >= 0 && hw_mode <= 2) cfg.hw_mode = (CounterHWMode)hw_mode;

      // edge: 0=RISING, 1=FALLING, 2=BOTH
      int16_t edge = (arg3_type == ST_TYPE_DINT) ? (int16_t)arg3.dint_val : arg3.int_val;
      if (edge >= 0 && edge <= 2) cfg.edge_type = (CounterEdgeType)edge;

      // direction: 0=UP, 1=DOWN
      int16_t dir = (arg4_type == ST_TYPE_DINT) ? (int16_t)arg4.dint_val : arg4.int_val;
      if (dir >= 0 && dir <= 1) cfg.direction = (CounterDirection)dir;

      // prescaler
      int16_t prescaler = (arg5_type == ST_TYPE_DINT) ? (int16_t)arg5.dint_val : arg5.int_val;
      if (prescaler >= 1) cfg.prescaler = (uint16_t)prescaler;

      // gpio
      int16_t gpio = (arg6_type == ST_TYPE_DINT) ? (int16_t)arg6.dint_val : arg6.int_val;
      if (gpio >= 0) {
        if (hw_mode == COUNTER_HW_PCNT) {
          cfg.hw_gpio = (uint8_t)gpio;
        } else if (hw_mode == COUNTER_HW_SW_ISR) {
          cfg.interrupt_pin = (uint8_t)gpio;
        } else {
          cfg.input_dis = (uint8_t)gpio;
        }
      }

      result.bool_val = counter_engine_configure(cnt_id, &cfg);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_SETUP_ADV) {
    // CNT_SETUP_ADV(id, scale, bit_width, debounce_ms, start_value) → BOOL
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.bool_val = false;
    } else {
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);

      // scale factor
      if (arg2_type == ST_TYPE_REAL) {
        cfg.scale_factor = arg2.real_val;
      } else if (arg2_type == ST_TYPE_DINT) {
        cfg.scale_factor = (float)arg2.dint_val;
      } else {
        cfg.scale_factor = (float)arg2.int_val;
      }

      // bit_width: 8, 16, 32, 64
      int16_t bw = (arg3_type == ST_TYPE_DINT) ? (int16_t)arg3.dint_val : arg3.int_val;
      if (bw == 8 || bw == 16 || bw == 32 || bw == 64) cfg.bit_width = (uint8_t)bw;

      // debounce_ms
      int16_t db_ms = (arg4_type == ST_TYPE_DINT) ? (int16_t)arg4.dint_val : arg4.int_val;
      if (db_ms > 0) {
        cfg.debounce_enabled = 1;
        cfg.debounce_ms = (uint16_t)db_ms;
      } else {
        cfg.debounce_enabled = 0;
        cfg.debounce_ms = 0;
      }

      // start_value
      if (arg5_type == ST_TYPE_DINT) {
        cfg.start_value = (uint64_t)arg5.dint_val;
      } else {
        cfg.start_value = (uint64_t)arg5.int_val;
      }

      result.bool_val = counter_config_set(cnt_id, &cfg);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_SETUP_CMP) {
    // CNT_SETUP_CMP(id, cmp_mode, cmp_value, cmp_source, reset_on_read) → BOOL
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.bool_val = false;
    } else {
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);

      cfg.compare_enabled = 1;

      // compare_mode: 0=>=, 1=>, 2=exact
      int16_t cmp_mode = (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val : arg2.int_val;
      if (cmp_mode >= 0 && cmp_mode <= 2) cfg.compare_mode = (uint8_t)cmp_mode;

      // compare_value
      if (arg3_type == ST_TYPE_DINT) {
        cfg.compare_value = (uint64_t)arg3.dint_val;
      } else {
        cfg.compare_value = (uint64_t)arg3.int_val;
      }

      // compare_source: 0=raw, 1=prescaled, 2=scaled
      int16_t cmp_src = (arg4_type == ST_TYPE_DINT) ? (int16_t)arg4.dint_val : arg4.int_val;
      if (cmp_src >= 0 && cmp_src <= 2) cfg.compare_source = (uint8_t)cmp_src;

      // reset_on_read
      int16_t ror = (arg5_type == ST_TYPE_DINT) ? (int16_t)arg5.dint_val : arg5.int_val;
      cfg.reset_on_read = (ror != 0) ? 1 : 0;

      result.bool_val = counter_config_set(cnt_id, &cfg);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_ENABLE) {
    // CNT_ENABLE(id, on_off) → BOOL
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    int16_t on_off = (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val :
                     (arg2_type == ST_TYPE_BOOL) ? (arg2.bool_val ? 1 : 0) : arg2.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.bool_val = false;
    } else {
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);
      cfg.enabled = (on_off != 0) ? 1 : 0;
      cfg.mode_enable = (on_off != 0) ? COUNTER_MODE_ENABLED : COUNTER_MODE_DISABLED;
      result.bool_val = counter_engine_configure(cnt_id, &cfg);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_CTRL) {
    // CNT_CTRL(id, cmd) → BOOL  (0=reset, 1=start, 2=stop)
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    int16_t cmd = (arg2_type == ST_TYPE_DINT) ? (int16_t)arg2.dint_val : arg2.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.bool_val = false;
    } else {
      CounterConfig cfg;
      if (!counter_config_get(cnt_id, &cfg) || cfg.ctrl_reg >= HOLDING_REGS_SIZE) {
        result.bool_val = false;
      } else {
        uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
        switch (cmd) {
          case 0:  // Reset
            counter_engine_reset(cnt_id);
            result.bool_val = true;
            break;
          case 1:  // Start
            ctrl_val |= (1 << 7);   // Set running bit
            registers_set_holding_register(cfg.ctrl_reg, ctrl_val);
            result.bool_val = true;
            break;
          case 2:  // Stop
            ctrl_val &= ~(1 << 7);  // Clear running bit
            registers_set_holding_register(cfg.ctrl_reg, ctrl_val);
            result.bool_val = true;
            break;
          default:
            result.bool_val = false;
            break;
        }
      }
    }
  }
  else if (func_id == ST_BUILTIN_CNT_VALUE) {
    // CNT_VALUE(id) → DINT (scaled value from holding register)
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.dint_val = 0;
    } else {
      uint64_t raw = counter_engine_get_value(cnt_id);
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);
      double scale = (cfg.scale_factor > 0.0f) ? (double)cfg.scale_factor : 1.0;
      double scaled = (double)raw * scale;
      if (scaled > (double)INT32_MAX) scaled = (double)INT32_MAX;
      if (scaled < (double)INT32_MIN) scaled = (double)INT32_MIN;
      result.dint_val = (int32_t)(scaled + 0.5);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_RAW) {
    // CNT_RAW(id) → DINT (raw counter value / prescaler)
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.dint_val = 0;
    } else {
      uint64_t raw = counter_engine_get_value(cnt_id);
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);
      if (cfg.prescaler > 1) raw = raw / cfg.prescaler;
      if (raw > (uint64_t)INT32_MAX) raw = INT32_MAX;
      result.dint_val = (int32_t)raw;
    }
  }
  else if (func_id == ST_BUILTIN_CNT_FREQ) {
    // CNT_FREQ(id) → INT (frequency in Hz)
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.int_val = 0;
    } else {
      result.int_val = (int16_t)counter_frequency_get(cnt_id);
    }
  }
  else if (func_id == ST_BUILTIN_CNT_STATUS) {
    // CNT_STATUS(id) → INT (bitfield: bit0=running, bit1=overflow, bit2=compare_hit)
    int16_t cnt_id = (arg1_type == ST_TYPE_DINT) ? (int16_t)arg1.dint_val : arg1.int_val;
    if (cnt_id < 1 || cnt_id > COUNTER_COUNT) {
      result.int_val = 0;
    } else {
      CounterConfig cfg;
      counter_config_get(cnt_id, &cfg);
      int16_t status = 0;
      if (cfg.ctrl_reg < HOLDING_REGS_SIZE) {
        uint16_t ctrl_val = registers_get_holding_register(cfg.ctrl_reg);
        if (ctrl_val & (1 << 7)) status |= 0x01;  // bit0: running
        if (ctrl_val & (1 << 3)) status |= 0x02;  // bit1: overflow
        if (ctrl_val & (1 << 4)) status |= 0x04;  // bit2: compare_hit
      }
      result.int_val = status;
    }
  }
  else if (func_id == ST_BUILTIN_LEN && arg_count == 1) {
    // FEAT-005: LEN(s) -> INT (antal tegn, ekskl. NUL-terminator)
    if (arg1_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "LEN() requires a STRING argument");
      vm->error = 1;
      return false;
    }
    result.int_val = (int16_t)strlen(st_vm_string_resolve(vm, arg1));
  }
  else if (func_id == ST_BUILTIN_CONCAT && arg_count == 2) {
    // FEAT-005: CONCAT(s1, s2) -> STRING (afkortes stille ved overloeb af
    // ST_MAX_STRING_LEN — samme "clamp fremfor fejl"-stil som resten af
    // VM'ens type-konverteringer, fx BUG-105's INT/DINT-clamping)
    if (arg1_type != ST_TYPE_STRING || arg2_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "CONCAT() requires two STRING arguments");
      vm->error = 1;
      return false;
    }
    char buf[ST_MAX_STRING_LEN + 1];
    snprintf(buf, sizeof(buf), "%s%s", st_vm_string_resolve(vm, arg1), st_vm_string_resolve(vm, arg2));
    result = st_vm_string_scratch_alloc(vm, buf);
  }
  else if ((func_id == ST_BUILTIN_LEFT || func_id == ST_BUILTIN_RIGHT) && arg_count == 2) {
    // FEAT-005: LEFT(s, n) / RIGHT(s, n) -> STRING (de n foerste/sidste tegn)
    if (arg1_type != ST_TYPE_STRING) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "%s() requires a STRING first argument",
               (func_id == ST_BUILTIN_LEFT) ? "LEFT" : "RIGHT");
      vm->error = 1;
      return false;
    }
    const char *src = st_vm_string_resolve(vm, arg1);
    int32_t n = (arg2_type == ST_TYPE_DINT) ? arg2.dint_val : arg2.int_val;
    size_t src_len = strlen(src);
    if (n < 0) n = 0;
    if ((size_t)n > src_len) n = (int32_t)src_len;
    char buf[ST_MAX_STRING_LEN + 1];
    if (func_id == ST_BUILTIN_LEFT) {
      memcpy(buf, src, (size_t)n);
      buf[n] = '\0';
    } else {
      memcpy(buf, src + (src_len - (size_t)n), (size_t)n);
      buf[n] = '\0';
    }
    result = st_vm_string_scratch_alloc(vm, buf);
  }
  else {
    result = st_builtin_call(func_id, arg1, arg2);
  }

  // BUG-077: Infer return type for polymorphic functions (SEL, LIMIT, SUM)
  st_datatype_t return_type;
  if (func_id == ST_BUILTIN_SEL) {
    // BUG-120 FIX: SEL returns same type as in0/in1 (arg2 and arg3) with proper promotion
    // Type promotion: INT → DINT → REAL
    if (arg2_type == ST_TYPE_REAL || arg3_type == ST_TYPE_REAL) {
      return_type = ST_TYPE_REAL;
    } else if (arg2_type == ST_TYPE_DINT || arg3_type == ST_TYPE_DINT) {
      return_type = ST_TYPE_DINT;
    } else {
      return_type = arg2_type;  // Both are INT/BOOL → use first
    }
  } else if (func_id == ST_BUILTIN_MUX) {
    // MUX returns same type as IN0/IN1/IN2 (arg2, arg3, arg4) with proper promotion
    // Type promotion: INT → DINT → REAL
    if (arg2_type == ST_TYPE_REAL || arg3_type == ST_TYPE_REAL || arg4_type == ST_TYPE_REAL) {
      return_type = ST_TYPE_REAL;
    } else if (arg2_type == ST_TYPE_DINT || arg3_type == ST_TYPE_DINT || arg4_type == ST_TYPE_DINT) {
      return_type = ST_TYPE_DINT;
    } else {
      return_type = arg2_type;  // All are INT/BOOL → use first
    }
  } else if (func_id == ST_BUILTIN_ROL || func_id == ST_BUILTIN_ROR) {
    // ROL/ROR returns same type as input (arg1)
    // Preserves INT/DINT/DWORD type for bit rotation
    return_type = arg1_type;
  } else if (func_id == ST_BUILTIN_LIMIT) {
    // BUG-121 FIX: LIMIT returns same type as min/val/max with proper promotion
    // Type promotion: INT → DINT → REAL
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL || arg3_type == ST_TYPE_REAL) {
      return_type = ST_TYPE_REAL;
    } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT || arg3_type == ST_TYPE_DINT) {
      return_type = ST_TYPE_DINT;
    } else {
      return_type = arg1_type;  // All are INT/BOOL → use first
    }
  } else if (func_id == ST_BUILTIN_SUM) {
    // BUG-110 FIX: SUM returns same type as ADD operator
    // If either is REAL, return REAL (type promotion)
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL) {
      return_type = ST_TYPE_REAL;
    } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT) {
      return_type = ST_TYPE_DINT;
    } else {
      return_type = ST_TYPE_INT;
    }
  } else if (func_id == ST_BUILTIN_MIN || func_id == ST_BUILTIN_MAX) {
    // BUG-117 FIX: MIN/MAX return type based on operand types
    if (arg1_type == ST_TYPE_REAL || arg2_type == ST_TYPE_REAL) {
      return_type = ST_TYPE_REAL;
    } else if (arg1_type == ST_TYPE_DINT || arg2_type == ST_TYPE_DINT) {
      return_type = ST_TYPE_DINT;
    } else {
      return_type = ST_TYPE_INT;
    }
  } else if (func_id == ST_BUILTIN_ABS) {
    // BUG-118 FIX: ABS returns same type as input
    return_type = arg1_type;
  } else {
    // Non-polymorphic function: use static return type
    return_type = st_builtin_return_type(func_id);
  }

  return st_vm_push_typed(vm, result, return_type);
}

/* ============================================================================
 * CONTROL FLOW
 * ============================================================================ */

static bool st_vm_exec_jmp(st_vm_t *vm, st_bytecode_instr_t *instr) {
  uint16_t target = (uint16_t)instr->arg.int_arg;

  // BUG-154: Validate jump target is within bytecode bounds
  if (target >= vm->program->instr_count) {
    snprintf(vm->error_msg, sizeof(vm->error_msg),
             "Jump target %u out of bounds (max %u)", target, vm->program->instr_count - 1);
    return false;
  }

  vm->pc = target;
  return true;
}

static bool st_vm_exec_jmp_if_false(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t cond;
  if (!st_vm_pop(vm, &cond)) return false;

  if (cond.bool_val == 0) {
    uint16_t target = (uint16_t)instr->arg.int_arg;

    // BUG-154: Validate jump target is within bytecode bounds
    if (target >= vm->program->instr_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Jump target %u out of bounds (max %u)", target, vm->program->instr_count - 1);
      return false;
    }

    vm->pc = target;  // Jump to target
  } else {
    vm->pc = vm->pc + 1;  // Continue to next instruction
  }
  return true;
}

static bool st_vm_exec_jmp_if_true(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t cond;
  if (!st_vm_pop(vm, &cond)) return false;

  if (cond.bool_val != 0) {
    uint16_t target = (uint16_t)instr->arg.int_arg;

    // BUG-154: Validate jump target is within bytecode bounds
    if (target >= vm->program->instr_count) {
      snprintf(vm->error_msg, sizeof(vm->error_msg),
               "Jump target %u out of bounds (max %u)", target, vm->program->instr_count - 1);
      return false;
    }

    vm->pc = target;  // Jump to target
  } else {
    vm->pc = vm->pc + 1;  // Continue to next instruction
  }
  return true;
}

/* ============================================================================
 * MAIN EXECUTION ENGINE
 * ============================================================================ */

bool st_vm_step(st_vm_t *vm) {
  if (!vm->program || !vm->program->instructions) {
    snprintf(vm->error_msg, sizeof(vm->error_msg), "No program loaded");
    vm->error = 1;
    return false;
  }

  if (vm->pc >= vm->program->instr_count) {
    vm->halted = 1;
    return false;
  }

  const st_bytecode_instr_t *const_instr = &vm->program->instructions[vm->pc];
  st_bytecode_instr_t *instr = const_cast<st_bytecode_instr_t *>(const_instr);

  // Execute instruction
  bool result = true;
  switch (instr->opcode) {
    case ST_OP_PUSH_BOOL:       result = st_vm_exec_push_bool(vm, instr); break;
    case ST_OP_PUSH_INT:        result = st_vm_exec_push_int(vm, instr); break;
    case ST_OP_PUSH_DWORD:      result = st_vm_exec_push_dword(vm, instr); break;
    case ST_OP_PUSH_REAL:       result = st_vm_exec_push_real(vm, instr); break;
    case ST_OP_PUSH_STRING_LIT: result = st_vm_exec_push_string_lit(vm, instr); break;
    case ST_OP_LOAD_VAR:        result = st_vm_exec_load_var(vm, instr); break;
    case ST_OP_STORE_VAR:       result = st_vm_exec_store_var(vm, instr); break;
    case ST_OP_LOAD_GLOBAL:     result = st_vm_exec_load_global(vm, instr); break;
    case ST_OP_STORE_GLOBAL:    result = st_vm_exec_store_global(vm, instr); break;
    case ST_OP_DUP:             result = st_vm_exec_dup(vm, instr); break;
    case ST_OP_POP:             result = st_vm_exec_pop(vm, instr); break;
    case ST_OP_ADD:             result = st_vm_exec_add(vm, instr); break;
    case ST_OP_ADD_CHECKED:     result = st_vm_exec_add_checked(vm, instr); break;  // BUG-159
    case ST_OP_SUB:             result = st_vm_exec_sub(vm, instr); break;
    case ST_OP_MUL:             result = st_vm_exec_mul(vm, instr); break;
    case ST_OP_DIV:             result = st_vm_exec_div(vm, instr); break;
    case ST_OP_MOD:             result = st_vm_exec_mod(vm, instr); break;
    case ST_OP_NEG:             result = st_vm_exec_neg(vm, instr); break;
    case ST_OP_AND:             result = st_vm_exec_and(vm, instr); break;
    case ST_OP_OR:              result = st_vm_exec_or(vm, instr); break;
    case ST_OP_XOR:             result = st_vm_exec_xor(vm, instr); break;
    case ST_OP_NOT:             result = st_vm_exec_not(vm, instr); break;
    case ST_OP_EQ:              result = st_vm_exec_eq(vm, instr); break;
    case ST_OP_NE:              result = st_vm_exec_ne(vm, instr); break;
    case ST_OP_LT:              result = st_vm_exec_lt(vm, instr); break;
    case ST_OP_GT:              result = st_vm_exec_gt(vm, instr); break;
    case ST_OP_LE:              result = st_vm_exec_le(vm, instr); break;
    case ST_OP_GE:              result = st_vm_exec_ge(vm, instr); break;
    case ST_OP_SHL:             result = st_vm_exec_shl(vm, instr); break;
    case ST_OP_SHR:             result = st_vm_exec_shr(vm, instr); break;
    case ST_OP_CALL_BUILTIN:    result = st_vm_exec_call_builtin(vm, instr); break;
    case ST_OP_JMP:             result = st_vm_exec_jmp(vm, instr); break;
    case ST_OP_JMP_IF_FALSE:    result = st_vm_exec_jmp_if_false(vm, instr); break;
    case ST_OP_JMP_IF_TRUE:     result = st_vm_exec_jmp_if_true(vm, instr); break;
    // FEAT-004: Array opcodes
    case ST_OP_LOAD_ARRAY:      result = st_vm_exec_load_array(vm, instr); break;
    case ST_OP_STORE_ARRAY:     result = st_vm_exec_store_array(vm, instr); break;
    // FEAT-122: Load FB instance field (timer Q/ET, counter Q/QU/QD/CV)
    case ST_OP_LOAD_FB_FIELD: {
      uint8_t fb_type = instr->arg.fb_field.fb_type;
      uint8_t inst_id = instr->arg.fb_field.instance_id;
      uint8_t field_id = instr->arg.fb_field.field_id;

      if (!vm->program || !vm->program->stateful) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "No stateful storage for LOAD_FB_FIELD");
        vm->error = 1;
        return false;
      }

      st_stateful_storage_t *stateful = (st_stateful_storage_t *)vm->program->stateful;
      st_value_t val;
      st_datatype_t val_type = ST_TYPE_BOOL;
      memset(&val, 0, sizeof(val));

      if (fb_type == 0) {
        // Timer: field 0=Q, 1=ET
        if (inst_id >= stateful->timer_count) {
          snprintf(vm->error_msg, sizeof(vm->error_msg), "Timer instance %d out of range", inst_id);
          vm->error = 1;
          return false;
        }
        st_timer_instance_t *ti = &stateful->timers[inst_id];
        if (field_id == 0) {
          val.bool_val = ti->Q;
          val_type = ST_TYPE_BOOL;
        } else if (field_id == 1) {
          val.dint_val = (int32_t)ti->ET;
          val_type = ST_TYPE_DINT;  // TIME represented as DINT in VM
        }
      } else if (fb_type == 1) {
        // Counter: field 0=Q/QU, 1=QD, 2=CV
        if (inst_id >= stateful->counter_count) {
          snprintf(vm->error_msg, sizeof(vm->error_msg), "Counter instance %d out of range", inst_id);
          vm->error = 1;
          return false;
        }
        st_counter_instance_t *ci = &stateful->counters[inst_id];
        if (field_id == 0) {
          val.bool_val = ci->Q;
          val_type = ST_TYPE_BOOL;
        } else if (field_id == 1) {
          val.bool_val = ci->QD;
          val_type = ST_TYPE_BOOL;
        } else if (field_id == 2) {
          val.dint_val = ci->CV;
          val_type = ST_TYPE_DINT;
        }
      } else {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Unknown FB type %d in LOAD_FB_FIELD", fb_type);
        vm->error = 1;
        return false;
      }

      // Push value onto stack
      if (vm->sp >= 64) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Stack overflow in LOAD_FB_FIELD");
        vm->error = 1;
        return false;
      }
      vm->stack[vm->sp] = val;
      vm->type_stack[vm->sp] = val_type;
      vm->sp++;
      break;
    }

    case ST_OP_NOP:             break;
    case ST_OP_HALT:
      vm->halted = 1;
      return false;

    // FEAT-003: User-defined function opcodes
    case ST_OP_CALL_USER: {
      // Get function index and FB instance ID from instruction
      uint8_t func_index = instr->arg.user_call.func_index;
      uint8_t fb_inst_id = instr->arg.user_call.instance_id;

      // Check if function registry is available
      if (!vm->func_registry) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "No function registry available");
        vm->error = 1;
        return false;
      }

      // Check function index bounds
      if (func_index >= vm->func_registry->builtin_count + vm->func_registry->user_count) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Invalid function index: %d", func_index);
        vm->error = 1;
        return false;
      }

      // Check call depth
      if (vm->call_depth >= 8) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Call stack overflow (max 8 nested calls)");
        vm->error = 1;
        return false;
      }

      // Get function entry
      const st_function_entry_t *func = &vm->func_registry->functions[func_index];

      // Push call frame
      st_call_frame_t *frame = &vm->call_stack[vm->call_depth];
      frame->return_pc = vm->pc;  // Return to next instruction
      frame->param_base = vm->sp - func->param_count;  // Parameters are on stack
      frame->param_count = func->param_count;
      frame->func_index = func_index;
      frame->fb_instance_id = fb_inst_id;  // Phase 5: Track FB instance

      // BUG-383 FIX: Give this call its own local_vars[] window so nested/
      // recursive calls can't silently clobber the caller's locals at the
      // same index. vm->local_base only matters while inside a function call
      // (call_depth>0) — a top-level call starts at 0, and each nested call
      // advances past however many local slots the CALLING function itself
      // occupies (func_registry's instance_size doubles as "local variable
      // count" for every user function, not just FBs — see
      // st_compiler_compile_function_def()).
      frame->saved_local_base = (uint8_t)vm->local_base;
      uint16_t new_local_base = vm->local_base;
      if (vm->call_depth > 0) {
        uint8_t caller_func_index = vm->call_stack[vm->call_depth - 1].func_index;
        new_local_base += vm->func_registry->functions[caller_func_index].instance_size;
      }
      if (new_local_base + func->instance_size > 64) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Local variable overflow (nested call too deep)");
        vm->error = 1;
        return false;
      }
      vm->local_base = new_local_base;

      // Phase 5: Load FB instance state into local_vars
      if (fb_inst_id != 0xFF && fb_inst_id < ST_MAX_FB_INSTANCES) {
        st_fb_instance_t *inst = &((st_function_registry_t *)vm->func_registry)->fb_instances[fb_inst_id];
        if (inst->initialized) {
          // Restore persistent local variables from instance storage
          uint8_t count = inst->local_count;
          if (count > ST_MAX_FB_LOCALS) count = ST_MAX_FB_LOCALS;
          for (uint8_t i = 0; i < count; i++) {
            if (vm->local_base + i < 64) {
              vm->local_vars[vm->local_base + i] = inst->local_vars[i];
              vm->local_types[vm->local_base + i] = inst->local_types[i];
            }
          }
        }
      }

      vm->call_depth++;

      // Jump to function code
      vm->pc = func->bytecode_addr;
      return true;  // Don't increment PC - we just set it
    }

    case ST_OP_RETURN: {
      // Check we're in a function
      if (vm->call_depth == 0) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "RETURN outside of function");
        vm->error = 1;
        return false;
      }

      // Get return value from stack (if any)
      st_value_t return_value;
      st_datatype_t return_type = ST_TYPE_NONE;
      if (vm->sp > 0) {
        st_vm_pop_typed(vm, &return_value, &return_type);
      }

      // Pop call frame
      vm->call_depth--;
      st_call_frame_t *frame = &vm->call_stack[vm->call_depth];

      // Phase 5: Save FB instance state (persist local variables)
      if (frame->fb_instance_id != 0xFF && frame->fb_instance_id < ST_MAX_FB_INSTANCES && vm->func_registry) {
        st_fb_instance_t *inst = &((st_function_registry_t *)vm->func_registry)->fb_instances[frame->fb_instance_id];
        // Count local variables used by this function (from function entry metadata)
        const st_function_entry_t *func = &vm->func_registry->functions[frame->func_index];
        // Store the local variable count based on the function's instance_size field
        // (we repurpose instance_size to count locals for FBs)
        uint8_t local_count = func->instance_size;
        if (local_count > ST_MAX_FB_LOCALS) local_count = ST_MAX_FB_LOCALS;
        for (uint8_t i = 0; i < local_count; i++) {
          if (vm->local_base + i < 64) {
            inst->local_vars[i] = vm->local_vars[vm->local_base + i];
            inst->local_types[i] = vm->local_types[vm->local_base + i];
          }
        }
        inst->local_count = local_count;
        inst->func_index = frame->func_index;
        inst->initialized = 1;
      }

      // BUG-383 FIX: Restore the caller's local_vars[] window (must happen
      // after the FB-save block above, which still needs THIS frame's own
      // local_base to read its locals).
      vm->local_base = frame->saved_local_base;

      // Restore PC
      vm->pc = frame->return_pc;

      // Pop parameters from stack
      vm->sp = frame->param_base;

      // Push return value (if any)
      if (return_type != ST_TYPE_NONE) {
        st_vm_push_typed(vm, return_value, return_type);
      }

      break;
    }

    case ST_OP_LOAD_PARAM: {
      // Load parameter from call frame
      if (vm->call_depth == 0) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "LOAD_PARAM outside of function");
        vm->error = 1;
        return false;
      }

      uint8_t param_index = (uint8_t)instr->arg.var_index;
      st_call_frame_t *frame = &vm->call_stack[vm->call_depth - 1];

      if (param_index >= frame->param_count) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Parameter index out of bounds: %d", param_index);
        vm->error = 1;
        return false;
      }

      // Parameters are stored on stack at param_base
      uint8_t stack_index = frame->param_base + param_index;
      st_vm_push_typed(vm, vm->stack[stack_index], vm->type_stack[stack_index]);
      break;
    }

    case ST_OP_STORE_PARAM: {
      // BUG-384 FIX: mirror LOAD_PARAM's addressing so a write to a
      // parameter is visible to subsequent LOAD_PARAM reads of the same
      // parameter within this call (previously misrouted to STORE_LOCAL,
      // landing in an unrelated local_vars[] slot instead).
      if (vm->call_depth == 0) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "STORE_PARAM outside of function");
        vm->error = 1;
        return false;
      }

      uint8_t param_index = (uint8_t)instr->arg.var_index;
      st_call_frame_t *frame = &vm->call_stack[vm->call_depth - 1];

      if (param_index >= frame->param_count) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Parameter index out of bounds: %d", param_index);
        vm->error = 1;
        return false;
      }

      st_value_t value;
      st_datatype_t type;
      if (!st_vm_pop_typed(vm, &value, &type)) {
        return false;
      }

      // Parameters are stored on stack at param_base (same slot LOAD_PARAM reads from)
      uint8_t stack_index = frame->param_base + param_index;
      vm->stack[stack_index] = value;
      vm->type_stack[stack_index] = type;
      break;
    }

    case ST_OP_STORE_LOCAL: {
      // Store to local variable
      if (vm->call_depth == 0) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "STORE_LOCAL outside of function");
        vm->error = 1;
        return false;
      }

      uint8_t local_index = (uint8_t)instr->arg.var_index;
      if (vm->local_base + local_index >= 64) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Local variable overflow");
        vm->error = 1;
        return false;
      }

      st_value_t value;
      st_datatype_t type;
      if (!st_vm_pop_typed(vm, &value, &type)) {
        return false;
      }

      vm->local_vars[vm->local_base + local_index] = value;
      vm->local_types[vm->local_base + local_index] = type;
      break;
    }

    case ST_OP_LOAD_LOCAL: {
      // Load local variable
      if (vm->call_depth == 0) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "LOAD_LOCAL outside of function");
        vm->error = 1;
        return false;
      }

      uint8_t local_index = (uint8_t)instr->arg.var_index;
      if (vm->local_base + local_index >= 64) {
        snprintf(vm->error_msg, sizeof(vm->error_msg), "Local variable overflow");
        vm->error = 1;
        return false;
      }

      st_value_t value = vm->local_vars[vm->local_base + local_index];
      st_datatype_t type = vm->local_types[vm->local_base + local_index];
      st_vm_push_typed(vm, value, type);
      break;
    }

    default:
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Unknown opcode: %d", instr->opcode);
      vm->error = 1;
      return false;
  }

  if (!result) {
    vm->error = 1;
    return false;
  }

  // Advance PC (unless instruction changed it)
  if (instr->opcode != ST_OP_JMP && instr->opcode != ST_OP_JMP_IF_FALSE && instr->opcode != ST_OP_JMP_IF_TRUE) {
    vm->pc++;
  } else {
    // PC was already set by jump instruction
  }

  vm->step_count++;
  return !vm->error;
}

bool st_vm_run(st_vm_t *vm, uint32_t max_steps) {
  uint32_t steps = 0;

  while (!vm->halted && !vm->error) {
    if (max_steps > 0 && steps >= max_steps) {
      snprintf(vm->error_msg, sizeof(vm->error_msg), "Max steps exceeded (%u)", max_steps);
      vm->error = 1;
      return false;
    }

    if (!st_vm_step(vm)) {
      break;
    }

    steps++;
  }

  return !vm->error;
}

/* ============================================================================
 * DEBUGGING
 * ============================================================================ */

void st_vm_print_state(st_vm_t *vm) {
  if (!vm || !vm->program) {
    debug_printf("VM not initialized\n");
    return;
  }

  debug_printf("\n=== VM State ===\n");
  debug_printf("Program: %s\n", vm->program->name);
  debug_printf("PC: %d / %d\n", vm->pc, vm->program->instr_count);
  debug_printf("Stack pointer: %d / 64\n", vm->sp);
  debug_printf("Halted: %s\n", vm->halted ? "Yes" : "No");
  debug_printf("Error: %s\n", vm->error ? vm->error_msg : "None");
  debug_printf("Steps: %u\n", vm->step_count);
  debug_printf("Max stack: %u\n\n", vm->max_stack_depth);
}

void st_vm_print_stack(st_vm_t *vm) {
  debug_printf("\n=== Stack (depth %d) ===\n", vm->sp);
  for (int i = vm->sp - 1; i >= 0; i--) {
    debug_printf("  [%d] INT: %d\n", i, vm->stack[i].int_val);
  }
  debug_printf("\n");
}

void st_vm_print_variables(st_vm_t *vm) {
  debug_printf("\n=== Variables (%d) ===\n", vm->var_count);
  for (int i = 0; i < vm->var_count; i++) {
    debug_printf("  [%d] INT: %d\n", i, vm->variables[i].int_val);
  }
  debug_printf("\n");
}
