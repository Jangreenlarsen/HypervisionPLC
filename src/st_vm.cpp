/**
 * @file st_vm.cpp
 * @brief Structured Text Virtual Machine Implementation
 *
 * Stack-based interpreter for bytecode execution.
 */

#include "st_vm.h"
#include "st_builtins.h"
#include "st_builtin_modbus.h"
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

static bool st_vm_exec_load_var(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val = st_vm_get_variable(vm, instr->arg.var_index);
  if (vm->error) return false;
  // BUG-050: Push with correct type from program
  st_datatype_t var_type = vm->program->var_types[instr->arg.var_index];
  return st_vm_push_typed(vm, val, var_type);
}

static bool st_vm_exec_store_var(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val;
  st_datatype_t val_type;

  // BUG-105: Pop with type information for automatic type conversion
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

  // Get target variable type
  st_datatype_t var_type = vm->program->var_types[instr->arg.var_index];

  // Automatic type conversion on assignment (IEC 61131-3 implicit conversion)
  st_value_t converted_val = val;

  // FEAT-121: Normalize TIME to DINT for conversion logic (same representation)
  if (val_type == ST_TYPE_TIME) val_type = ST_TYPE_DINT;
  st_datatype_t target_type = (var_type == ST_TYPE_TIME) ? ST_TYPE_DINT : var_type;

  if (val_type != target_type) {
    // REAL → INT: Truncate to 16-bit
    if (val_type == ST_TYPE_REAL && target_type == ST_TYPE_INT) {
      int32_t temp = (int32_t)val.real_val;
      if (temp > INT16_MAX) temp = INT16_MAX;
      if (temp < INT16_MIN) temp = INT16_MIN;
      converted_val.int_val = (int16_t)temp;
    }
    // REAL → DINT: Truncate to 32-bit
    else if (val_type == ST_TYPE_REAL && target_type == ST_TYPE_DINT) {
      converted_val.dint_val = (int32_t)val.real_val;
    }
    // REAL → BOOL: Non-zero = TRUE
    else if (val_type == ST_TYPE_REAL && target_type == ST_TYPE_BOOL) {
      converted_val.bool_val = (val.real_val != 0.0f);
    }
    // DINT → INT: Clamp to INT16 range
    else if (val_type == ST_TYPE_DINT && target_type == ST_TYPE_INT) {
      int32_t temp = val.dint_val;
      if (temp > INT16_MAX) temp = INT16_MAX;
      if (temp < INT16_MIN) temp = INT16_MIN;
      converted_val.int_val = (int16_t)temp;
    }
    // DINT → REAL: Convert to float
    else if (val_type == ST_TYPE_DINT && target_type == ST_TYPE_REAL) {
      converted_val.real_val = (float)val.dint_val;
    }
    // INT → REAL: Convert to float
    else if (val_type == ST_TYPE_INT && target_type == ST_TYPE_REAL) {
      converted_val.real_val = (float)val.int_val;
    }
    // INT → DINT: Sign-extend to 32-bit
    else if (val_type == ST_TYPE_INT && target_type == ST_TYPE_DINT) {
      converted_val.dint_val = (int32_t)val.int_val;
    }
    // INT → BOOL: Non-zero = TRUE
    else if (val_type == ST_TYPE_INT && target_type == ST_TYPE_BOOL) {
      converted_val.bool_val = (val.int_val != 0);
    }
    // BOOL → INT: TRUE=1, FALSE=0
    else if (val_type == ST_TYPE_BOOL && target_type == ST_TYPE_INT) {
      converted_val.int_val = val.bool_val ? 1 : 0;
    }
    // BOOL → REAL: TRUE=1.0, FALSE=0.0
    else if (val_type == ST_TYPE_BOOL && target_type == ST_TYPE_REAL) {
      converted_val.real_val = val.bool_val ? 1.0f : 0.0f;
    }
    // DWORD conversions (if needed, add more cases)
    else {
      // No conversion needed or unsupported conversion (use value as-is)
      converted_val = val;
    }
  }

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

  // Type conversion (same as store_var)
  st_datatype_t var_type = vm->program->var_types[var_idx];
  st_value_t converted_val = val;
  if (val_type != var_type) {
    if (val_type == ST_TYPE_INT && var_type == ST_TYPE_INT) {
      // Same type, no conversion
    } else if (val_type == ST_TYPE_DINT && var_type == ST_TYPE_INT) {
      int32_t temp = val.dint_val;
      if (temp > INT16_MAX) temp = INT16_MAX;
      if (temp < INT16_MIN) temp = INT16_MIN;
      converted_val.int_val = (int16_t)temp;
    } else if (val_type == ST_TYPE_INT && var_type == ST_TYPE_DINT) {
      converted_val.dint_val = (int32_t)val.int_val;
    } else if (val_type == ST_TYPE_REAL && var_type == ST_TYPE_INT) {
      int32_t temp = (int32_t)val.real_val;
      if (temp > INT16_MAX) temp = INT16_MAX;
      if (temp < INT16_MIN) temp = INT16_MIN;
      converted_val.int_val = (int16_t)temp;
    } else if (val_type == ST_TYPE_INT && var_type == ST_TYPE_REAL) {
      converted_val.real_val = (float)val.int_val;
    }
    // For other conversions, use value as-is
  }

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

  // DINT + DINT = DINT (32-bit arithmetic)
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.dint_val = left_d + right_d;  // Wraps on overflow
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
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

  // DINT - DINT = DINT (32-bit arithmetic)
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.dint_val = left_d - right_d;  // Wraps on overflow
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
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

  // DINT * DINT = DINT (32-bit arithmetic)
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.dint_val = left_d * right_d;  // Wraps on overflow
    return st_vm_push_typed(vm, result, ST_TYPE_DINT);
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

  // DINT % DINT = DINT (32-bit modulo)
  if (left_type == ST_TYPE_DINT || right_type == ST_TYPE_DINT) {
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;

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

  result.bool_val = (left.bool_val != 0) && (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_or(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  result.bool_val = (left.bool_val != 0) || (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_xor(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t right, left, result;
  st_datatype_t right_type, left_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &right, &right_type)) return false;
  if (!st_vm_pop_typed(vm, &left, &left_type)) return false;

  result.bool_val = (left.bool_val != 0) != (right.bool_val != 0);
  return st_vm_push_typed(vm, result, ST_TYPE_BOOL);
}

static bool st_vm_exec_not(st_vm_t *vm, st_bytecode_instr_t *instr) {
  st_value_t val, result;
  st_datatype_t val_type;

  // BUG-151 FIX: Use typed pop to maintain type stack consistency
  if (!st_vm_pop_typed(vm, &val, &val_type)) return false;

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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.bool_val = (left_d == right_d);
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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.bool_val = (left_d != right_d);
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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.dint_val;
    result.bool_val = (left_d < right_d);
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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.bool_val = (left_d > right_d);
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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.bool_val = (left_d <= right_d);
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
    // DINT comparison (promote INT to DINT)
    int32_t left_d = (left_type == ST_TYPE_DINT) ? left.dint_val : (int32_t)left.int_val;
    int32_t right_d = (right_type == ST_TYPE_DINT) ? right.dint_val : (int32_t)right.int_val;
    result.bool_val = (left_d >= right_d);
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

      // Count: clamp to 1-16
      if (arg3_type == ST_TYPE_DINT) {
        count_int.int_val = (arg3.dint_val > 16) ? 16 : arg3.dint_val;
      } else if (arg3_type == ST_TYPE_DWORD) {
        count_int.int_val = (arg3.dword_val > 16) ? 16 : arg3.dword_val;
      } else {
        count_int.int_val = arg3.int_val;
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
    case ST_OP_LOAD_VAR:        result = st_vm_exec_load_var(vm, instr); break;
    case ST_OP_STORE_VAR:       result = st_vm_exec_store_var(vm, instr); break;
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
      frame->local_count = 0;  // Will be set by function prologue
      frame->func_index = func_index;
      frame->fb_instance_id = fb_inst_id;  // Phase 5: Track FB instance

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
