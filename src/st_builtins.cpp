/**
 * @file st_builtins.cpp
 * @brief Structured Text Built-in Functions Implementation
 *
 * IEC 61131-3 standard mathematical and conversion functions.
 */

#include "st_builtins.h"
#include "st_builtin_persist.h"
#include "st_builtin_modbus.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * MATHEMATICAL FUNCTIONS
 * ============================================================================ */

st_value_t st_builtin_abs(st_value_t x) {
  st_value_t result;
  // BUG-088 & BUG-105: Handle INT16_MIN overflow (-INT16_MIN = INT16_MIN, not positive)
  if (x.int_val == INT16_MIN) {
    result.int_val = INT16_MAX;  // Clamp to max positive value (32767)
  } else {
    result.int_val = (x.int_val < 0) ? -x.int_val : x.int_val;
  }
  return result;
}

st_value_t st_builtin_min(st_value_t a, st_value_t b) {
  st_value_t result;
  result.int_val = (a.int_val < b.int_val) ? a.int_val : b.int_val;
  return result;
}

st_value_t st_builtin_max(st_value_t a, st_value_t b) {
  st_value_t result;
  result.int_val = (a.int_val > b.int_val) ? a.int_val : b.int_val;
  return result;
}

st_value_t st_builtin_sqrt(st_value_t x) {
  st_value_t result;
  // BUG-065: Check for negative input (sqrtf(negative) returns NaN)
  if (x.real_val < 0.0f) {
    result.real_val = 0.0f;  // Return 0 for negative input
  } else {
    result.real_val = (float)sqrtf((float)x.real_val);
  }
  return result;
}

st_value_t st_builtin_round(st_value_t x) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  int32_t temp = (int32_t)roundf(x.real_val);
  if (temp > INT16_MAX) temp = INT16_MAX;
  if (temp < INT16_MIN) temp = INT16_MIN;
  result.int_val = (int16_t)temp;
  return result;
}

st_value_t st_builtin_trunc(st_value_t x) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  int32_t temp = (int32_t)truncf(x.real_val);
  if (temp > INT16_MAX) temp = INT16_MAX;
  if (temp < INT16_MIN) temp = INT16_MIN;
  result.int_val = (int16_t)temp;
  return result;
}

st_value_t st_builtin_floor(st_value_t x) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  int32_t temp = (int32_t)floorf(x.real_val);
  if (temp > INT16_MAX) temp = INT16_MAX;
  if (temp < INT16_MIN) temp = INT16_MIN;
  result.int_val = (int16_t)temp;
  return result;
}

st_value_t st_builtin_ceil(st_value_t x) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  int32_t temp = (int32_t)ceilf(x.real_val);
  if (temp > INT16_MAX) temp = INT16_MAX;
  if (temp < INT16_MIN) temp = INT16_MIN;
  result.int_val = (int16_t)temp;
  return result;
}

/* ============================================================================
 * CLAMPING & SELECTION FUNCTIONS (v4.4+)
 * ============================================================================ */

st_value_t st_builtin_limit(st_value_t min_val, st_value_t value, st_value_t max_val) {
  st_value_t result;

  // Clamp value between min and max
  if (value.int_val < min_val.int_val) {
    result.int_val = min_val.int_val;
  } else if (value.int_val > max_val.int_val) {
    result.int_val = max_val.int_val;
  } else {
    result.int_val = value.int_val;
  }

  return result;
}

st_value_t st_builtin_sel(st_value_t g, st_value_t in0, st_value_t in1) {
  // SEL: Boolean selector
  // g=false → return in0
  // g=true  → return in1
  return (g.bool_val) ? in1 : in0;
}

st_value_t st_builtin_mux(st_value_t k, st_value_t in0, st_value_t in1, st_value_t in2) {
  // MUX: Integer selector (4-way multiplexer)
  // K=0 → return IN0
  // K=1 → return IN1
  // K=2 → return IN2
  // K out of range → return IN0 (default)

  int16_t selector = k.int_val;

  if (selector == 1) {
    return in1;
  } else if (selector == 2) {
    return in2;
  } else {
    return in0;  // Default: selector 0 or out of range
  }
}

/* ============================================================================
 * BIT ROTATION FUNCTIONS (v4.8.4)
 * ============================================================================ */

st_value_t st_builtin_rol(st_value_t in, st_value_t n, st_datatype_t in_type) {
  // ROL: Rotate Left
  // Shifts bits to the left, wrapping MSB to LSB
  st_value_t result;

  int16_t shift = n.int_val;

  if (in_type == ST_TYPE_DINT || in_type == ST_TYPE_DWORD) {
    // 32-bit rotation
    shift = shift % 32;  // Normalize shift amount
    if (shift < 0) shift += 32;  // Handle negative shifts

    uint32_t val = (in_type == ST_TYPE_DINT) ? (uint32_t)in.dint_val : in.dword_val;
    uint32_t rotated = (val << shift) | (val >> (32 - shift));

    if (in_type == ST_TYPE_DINT) {
      result.dint_val = (int32_t)rotated;
    } else {
      result.dword_val = rotated;
    }
  } else {
    // 16-bit rotation (INT)
    shift = shift % 16;  // Normalize shift amount
    if (shift < 0) shift += 16;  // Handle negative shifts

    uint16_t val = (uint16_t)in.int_val;
    uint16_t rotated = (val << shift) | (val >> (16 - shift));
    result.int_val = (int16_t)rotated;
  }

  return result;
}

st_value_t st_builtin_ror(st_value_t in, st_value_t n, st_datatype_t in_type) {
  // ROR: Rotate Right
  // Shifts bits to the right, wrapping LSB to MSB
  st_value_t result;

  int16_t shift = n.int_val;

  if (in_type == ST_TYPE_DINT || in_type == ST_TYPE_DWORD) {
    // 32-bit rotation
    shift = shift % 32;  // Normalize shift amount
    if (shift < 0) shift += 32;  // Handle negative shifts

    uint32_t val = (in_type == ST_TYPE_DINT) ? (uint32_t)in.dint_val : in.dword_val;
    uint32_t rotated = (val >> shift) | (val << (32 - shift));

    if (in_type == ST_TYPE_DINT) {
      result.dint_val = (int32_t)rotated;
    } else {
      result.dword_val = rotated;
    }
  } else {
    // 16-bit rotation (INT)
    shift = shift % 16;  // Normalize shift amount
    if (shift < 0) shift += 16;  // Handle negative shifts

    uint16_t val = (uint16_t)in.int_val;
    uint16_t rotated = (val >> shift) | (val << (16 - shift));
    result.int_val = (int16_t)rotated;
  }

  return result;
}

/* ============================================================================
 * TRIGONOMETRIC FUNCTIONS (v4.4+)
 * ============================================================================ */

st_value_t st_builtin_sin(st_value_t x) {
  st_value_t result;
  result.real_val = sinf(x.real_val);  // Input in radians
  return result;
}

st_value_t st_builtin_cos(st_value_t x) {
  st_value_t result;
  result.real_val = cosf(x.real_val);  // Input in radians
  return result;
}

st_value_t st_builtin_tan(st_value_t x) {
  st_value_t result;
  result.real_val = tanf(x.real_val);  // Input in radians
  return result;
}

/* ============================================================================
 * EXPONENTIAL FUNCTIONS (v4.7+)
 * ============================================================================ */

st_value_t st_builtin_exp(st_value_t x) {
  st_value_t result;
  float exp_result = expf(x.real_val);  // e^x

  // v4.7+: Overflow check - expf(>88.7) → INF
  if (isinf(exp_result)) {
    result.real_val = 0.0f;  // Return 0 for overflow (consistent with other math functions)
  } else {
    result.real_val = exp_result;
  }
  return result;
}

st_value_t st_builtin_ln(st_value_t x) {
  st_value_t result;
  // Check for invalid input (log of non-positive number)
  if (x.real_val <= 0.0f) {
    result.real_val = 0.0f;  // Return 0 for invalid input (similar to BUG-065 SQRT handling)
  } else {
    result.real_val = logf(x.real_val);  // Natural logarithm (base e)
  }
  return result;
}

st_value_t st_builtin_log(st_value_t x) {
  st_value_t result;
  // Check for invalid input (log of non-positive number)
  if (x.real_val <= 0.0f) {
    result.real_val = 0.0f;  // Return 0 for invalid input
  } else {
    result.real_val = log10f(x.real_val);  // Base-10 logarithm
  }
  return result;
}

st_value_t st_builtin_pow(st_value_t x, st_value_t y) {
  st_value_t result;
  float pow_result = powf(x.real_val, y.real_val);  // x^y

  // v4.7+: Input validation - check for NaN/INF (negative base with fractional exponent, etc.)
  if (isnan(pow_result) || isinf(pow_result)) {
    result.real_val = 0.0f;  // Return 0 for invalid results (consistent with LN/LOG/SQRT)
  } else {
    result.real_val = pow_result;
  }
  return result;
}

/* ============================================================================
 * TYPE CONVERSION FUNCTIONS
 * ============================================================================ */

st_value_t st_builtin_int_to_real(st_value_t i) {
  st_value_t result;
  result.real_val = (float)i.int_val;
  return result;
}

st_value_t st_builtin_real_to_int(st_value_t r) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  int32_t temp = (int32_t)r.real_val;
  if (temp > INT16_MAX) temp = INT16_MAX;
  if (temp < INT16_MIN) temp = INT16_MIN;
  result.int_val = (int16_t)temp;
  return result;
}

st_value_t st_builtin_bool_to_int(st_value_t b) {
  st_value_t result;
  result.int_val = (b.bool_val != 0) ? 1 : 0;
  return result;
}

st_value_t st_builtin_int_to_bool(st_value_t i) {
  st_value_t result;
  result.bool_val = (i.int_val != 0);
  return result;
}

st_value_t st_builtin_dword_to_int(st_value_t d) {
  st_value_t result;
  // BUG-105: Clamp to INT16 range (-32768 to 32767)
  if (d.dword_val > (uint32_t)INT16_MAX) {
    result.int_val = INT16_MAX;  // 32767
  } else {
    result.int_val = (int16_t)d.dword_val;
  }
  return result;
}

st_value_t st_builtin_int_to_dword(st_value_t i) {
  st_value_t result;
  // Unsigned conversion: negative → wrap around
  result.dword_val = (uint32_t)i.int_val;
  return result;
}

/* ============================================================================
 * DISPATCHER FUNCTION
 * ============================================================================ */

st_value_t st_builtin_call(st_builtin_func_t func_id, st_value_t arg1, st_value_t arg2) {
  st_value_t result = {0};

  switch (func_id) {
    // Mathematical
    case ST_BUILTIN_ABS:
      result = st_builtin_abs(arg1);
      break;

    case ST_BUILTIN_MIN:
      result = st_builtin_min(arg1, arg2);
      break;

    case ST_BUILTIN_MAX:
      result = st_builtin_max(arg1, arg2);
      break;

    case ST_BUILTIN_SUM:
      // Alias for addition
      result.int_val = arg1.int_val + arg2.int_val;
      break;

    case ST_BUILTIN_SQRT:
      result = st_builtin_sqrt(arg1);
      break;

    case ST_BUILTIN_ROUND:
      result = st_builtin_round(arg1);
      break;

    case ST_BUILTIN_TRUNC:
      result = st_builtin_trunc(arg1);
      break;

    case ST_BUILTIN_FLOOR:
      result = st_builtin_floor(arg1);
      break;

    case ST_BUILTIN_CEIL:
      result = st_builtin_ceil(arg1);
      break;

    // Clamping & Selection (v4.4+)
    case ST_BUILTIN_LIMIT:
      // NOTE: 3-arg functions are handled directly in VM (st_vm.cpp)
      // This case should not be reached
      result.int_val = 0;
      break;

    case ST_BUILTIN_SEL:
      // NOTE: 3-arg functions are handled directly in VM (st_vm.cpp)
      // This case should not be reached
      result.int_val = 0;
      break;

    case ST_BUILTIN_MUX:
      // NOTE: 4-arg functions are handled directly in VM (st_vm.cpp)
      // This case should not be reached
      result.int_val = 0;
      break;

    case ST_BUILTIN_ROL:
    case ST_BUILTIN_ROR:
      // NOTE: 2-arg type-dependent functions are handled directly in VM (st_vm.cpp)
      // This case should not be reached
      result.int_val = 0;
      break;

    // Bit manipulation (v7.3.1)
    case ST_BUILTIN_BIT_SET: {
      uint8_t bit = arg2.int_val & 0x0F;  // 0-15 for INT
      result.int_val = arg1.int_val | (int16_t)(1 << bit);
      break;
    }
    case ST_BUILTIN_BIT_CLR: {
      uint8_t bit = arg2.int_val & 0x0F;
      result.int_val = arg1.int_val & (int16_t)~(1 << bit);
      break;
    }
    case ST_BUILTIN_BIT_TST: {
      uint8_t bit = arg2.int_val & 0x0F;
      result.bool_val = (arg1.int_val & (1 << bit)) != 0;
      break;
    }

    // Trigonometric (v4.4+)
    case ST_BUILTIN_SIN:
      result = st_builtin_sin(arg1);
      break;

    case ST_BUILTIN_COS:
      result = st_builtin_cos(arg1);
      break;

    case ST_BUILTIN_TAN:
      result = st_builtin_tan(arg1);
      break;

    // Exponential (v4.7+)
    case ST_BUILTIN_EXP:
      result = st_builtin_exp(arg1);
      break;

    case ST_BUILTIN_LN:
      result = st_builtin_ln(arg1);
      break;

    case ST_BUILTIN_LOG:
      result = st_builtin_log(arg1);
      break;

    case ST_BUILTIN_POW:
      result = st_builtin_pow(arg1, arg2);
      break;

    // Type conversions
    case ST_BUILTIN_INT_TO_REAL:
      result = st_builtin_int_to_real(arg1);
      break;

    case ST_BUILTIN_REAL_TO_INT:
      result = st_builtin_real_to_int(arg1);
      break;

    case ST_BUILTIN_BOOL_TO_INT:
      result = st_builtin_bool_to_int(arg1);
      break;

    case ST_BUILTIN_INT_TO_BOOL:
      result = st_builtin_int_to_bool(arg1);
      break;

    case ST_BUILTIN_DWORD_TO_INT:
      result = st_builtin_dword_to_int(arg1);
      break;

    case ST_BUILTIN_INT_TO_DWORD:
      result = st_builtin_int_to_dword(arg1);
      break;

    // Persistence (v4.0+)
    case ST_BUILTIN_PERSIST_SAVE:
      result = st_builtin_persist_save(arg1);  // arg1 = group_id
      break;

    case ST_BUILTIN_PERSIST_LOAD:
      result = st_builtin_persist_load(arg1);  // arg1 = group_id
      break;

    // Modbus Master (v4.4+)
    case ST_BUILTIN_MB_READ_COIL:
      result = st_builtin_mb_read_coil(arg1, arg2);
      break;

    case ST_BUILTIN_MB_READ_INPUT:
      result = st_builtin_mb_read_input(arg1, arg2);
      break;

    case ST_BUILTIN_MB_READ_HOLDING:
      result = st_builtin_mb_read_holding(arg1, arg2);
      break;

    case ST_BUILTIN_MB_READ_INPUT_REG:
      result = st_builtin_mb_read_input_reg(arg1, arg2);
      break;

    case ST_BUILTIN_MB_WRITE_COIL:
      // 3-argument function - handled in VM
      result.int_val = 0;
      break;

    case ST_BUILTIN_MB_WRITE_HOLDING:
      // 3-argument function - handled in VM
      result.int_val = 0;
      break;

    // Async Modbus Status (v7.7.0 — 0-arg)
    case ST_BUILTIN_MB_SUCCESS:
      result = st_builtin_mb_success_func();
      break;

    case ST_BUILTIN_MB_BUSY:
      result = st_builtin_mb_busy_func();
      break;

    case ST_BUILTIN_MB_ERROR:
      result = st_builtin_mb_error_func();
      break;

    default:
      // Unknown function - return zero
      break;
  }

  return result;
}

/* ============================================================================
 * METADATA FUNCTIONS
 * ============================================================================ */

const char *st_builtin_name(st_builtin_func_t func_id) {
  switch (func_id) {
    case ST_BUILTIN_ABS:           return "ABS";
    case ST_BUILTIN_MIN:           return "MIN";
    case ST_BUILTIN_MAX:           return "MAX";
    case ST_BUILTIN_SUM:           return "SUM";
    case ST_BUILTIN_SQRT:          return "SQRT";
    case ST_BUILTIN_ROUND:         return "ROUND";
    case ST_BUILTIN_TRUNC:         return "TRUNC";
    case ST_BUILTIN_FLOOR:         return "FLOOR";
    case ST_BUILTIN_CEIL:          return "CEIL";
    case ST_BUILTIN_LIMIT:         return "LIMIT";
    case ST_BUILTIN_SEL:           return "SEL";
    case ST_BUILTIN_MUX:           return "MUX";
    case ST_BUILTIN_ROL:           return "ROL";
    case ST_BUILTIN_ROR:           return "ROR";
    case ST_BUILTIN_SIN:           return "SIN";
    case ST_BUILTIN_COS:           return "COS";
    case ST_BUILTIN_TAN:           return "TAN";
    case ST_BUILTIN_EXP:           return "EXP";
    case ST_BUILTIN_LN:            return "LN";
    case ST_BUILTIN_LOG:           return "LOG";
    case ST_BUILTIN_POW:           return "POW";
    case ST_BUILTIN_INT_TO_REAL:   return "INT_TO_REAL";
    case ST_BUILTIN_REAL_TO_INT:   return "REAL_TO_INT";
    case ST_BUILTIN_BOOL_TO_INT:   return "BOOL_TO_INT";
    case ST_BUILTIN_INT_TO_BOOL:   return "INT_TO_BOOL";
    case ST_BUILTIN_DWORD_TO_INT:  return "DWORD_TO_INT";
    case ST_BUILTIN_INT_TO_DWORD:  return "INT_TO_DWORD";
    case ST_BUILTIN_PERSIST_SAVE:  return "SAVE";
    case ST_BUILTIN_PERSIST_LOAD:  return "LOAD";
    case ST_BUILTIN_MB_READ_COIL:      return "MB_READ_COIL";
    case ST_BUILTIN_MB_READ_INPUT:     return "MB_READ_INPUT";
    case ST_BUILTIN_MB_READ_HOLDING:   return "MB_READ_HOLDING";
    case ST_BUILTIN_MB_READ_INPUT_REG: return "MB_READ_INPUT_REG";
    case ST_BUILTIN_MB_WRITE_COIL:     return "MB_WRITE_COIL";
    case ST_BUILTIN_MB_WRITE_HOLDING:  return "MB_WRITE_HOLDING";
    case ST_BUILTIN_R_TRIG:        return "R_TRIG";
    case ST_BUILTIN_F_TRIG:        return "F_TRIG";
    case ST_BUILTIN_TON:           return "TON";
    case ST_BUILTIN_TOF:           return "TOF";
    case ST_BUILTIN_TP:            return "TP";
    case ST_BUILTIN_CTU:           return "CTU";
    case ST_BUILTIN_CTD:           return "CTD";
    case ST_BUILTIN_CTUD:          return "CTUD";
    case ST_BUILTIN_BIT_SET:       return "BIT_SET";
    case ST_BUILTIN_BIT_CLR:       return "BIT_CLR";
    case ST_BUILTIN_BIT_TST:       return "BIT_TST";
    case ST_BUILTIN_MB_SUCCESS:    return "MB_SUCCESS";
    case ST_BUILTIN_MB_BUSY:       return "MB_BUSY";
    case ST_BUILTIN_MB_ERROR:      return "MB_ERROR";
    default:                       return "UNKNOWN";
  }
}

uint8_t st_builtin_arg_count(st_builtin_func_t func_id) {
  switch (func_id) {
    // 1-argument functions
    case ST_BUILTIN_ABS:
    case ST_BUILTIN_SQRT:
    case ST_BUILTIN_ROUND:
    case ST_BUILTIN_TRUNC:
    case ST_BUILTIN_FLOOR:
    case ST_BUILTIN_CEIL:
    case ST_BUILTIN_INT_TO_REAL:
    case ST_BUILTIN_REAL_TO_INT:
    case ST_BUILTIN_BOOL_TO_INT:
    case ST_BUILTIN_INT_TO_BOOL:
    case ST_BUILTIN_DWORD_TO_INT:
    case ST_BUILTIN_INT_TO_DWORD:
    case ST_BUILTIN_SIN:
    case ST_BUILTIN_COS:
    case ST_BUILTIN_TAN:
    case ST_BUILTIN_EXP:
    case ST_BUILTIN_LN:
    case ST_BUILTIN_LOG:
      return 1;

    // 2-argument functions
    case ST_BUILTIN_MIN:
    case ST_BUILTIN_MAX:
    case ST_BUILTIN_SUM:
    case ST_BUILTIN_POW:
    case ST_BUILTIN_ROL:               // ROL(IN, N)
    case ST_BUILTIN_ROR:               // ROR(IN, N)
    case ST_BUILTIN_MB_READ_COIL:      // MB_READ_COIL(slave_id, address)
    case ST_BUILTIN_MB_READ_INPUT:     // MB_READ_INPUT(slave_id, address)
    case ST_BUILTIN_MB_READ_HOLDING:   // MB_READ_HOLDING(slave_id, address)
    case ST_BUILTIN_MB_READ_INPUT_REG: // MB_READ_INPUT_REG(slave_id, address)
    case ST_BUILTIN_BIT_SET:           // BIT_SET(value, bit_pos)
    case ST_BUILTIN_BIT_CLR:           // BIT_CLR(value, bit_pos)
    case ST_BUILTIN_BIT_TST:           // BIT_TST(value, bit_pos)
      return 2;

    // 4-argument functions (v4.8.3+)
    case ST_BUILTIN_MUX:            // MUX(K, IN0, IN1, IN2)
      return 4;

    // 3-argument functions (v4.4+)
    case ST_BUILTIN_LIMIT:  // LIMIT(min, val, max)
    case ST_BUILTIN_SEL:     // SEL(g, in0, in1)
    case ST_BUILTIN_MB_WRITE_COIL:     // MB_WRITE_COIL(slave_id, address, value)
    case ST_BUILTIN_MB_WRITE_HOLDING:  // MB_WRITE_HOLDING(slave_id, address, value)
      return 3;

    // 1-argument functions (v4.0+)
    case ST_BUILTIN_PERSIST_SAVE:
    case ST_BUILTIN_PERSIST_LOAD:
      return 1;  // arg: group_id (0=all, 1-8=specific)

    // Stateful functions (v4.7+)
    case ST_BUILTIN_R_TRIG:        // R_TRIG(CLK)
    case ST_BUILTIN_F_TRIG:        // F_TRIG(CLK)
      return 1;

    case ST_BUILTIN_TON:           // TON(IN, PT)
    case ST_BUILTIN_TOF:           // TOF(IN, PT)
    case ST_BUILTIN_TP:            // TP(IN, PT)
      return 2;

    case ST_BUILTIN_CTU:           // CTU(CU, RESET, PV)
    case ST_BUILTIN_CTD:           // CTD(CD, LOAD, PV)
      return 3;

    case ST_BUILTIN_CTUD:          // CTUD(CU, CD, RESET, LOAD, PV)
      return 5;

    // Bistable latches (v4.7.3)
    case ST_BUILTIN_SR:            // SR(S1, R)
    case ST_BUILTIN_RS:            // RS(S, R1)
      return 2;

    // Signal processing (v4.8)
    case ST_BUILTIN_SCALE:         // SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX)
      return 5;

    case ST_BUILTIN_HYSTERESIS:    // HYSTERESIS(IN, HIGH, LOW)
    case ST_BUILTIN_BLINK:         // BLINK(ENABLE, ON_TIME, OFF_TIME)
      return 3;

    case ST_BUILTIN_FILTER:        // FILTER(IN, TIME_CONSTANT)
      return 2;

    // 0-argument functions (v7.7.0)
    case ST_BUILTIN_MB_SUCCESS:    // MB_SUCCESS()
    case ST_BUILTIN_MB_BUSY:       // MB_BUSY()
    case ST_BUILTIN_MB_ERROR:      // MB_ERROR()
      return 0;

    default:
      return 0;
  }
}

st_datatype_t st_builtin_return_type(st_builtin_func_t func_id) {
  switch (func_id) {
    // Returns REAL
    case ST_BUILTIN_SQRT:
    case ST_BUILTIN_SIN:
    case ST_BUILTIN_COS:
    case ST_BUILTIN_TAN:
    case ST_BUILTIN_EXP:
    case ST_BUILTIN_LN:
    case ST_BUILTIN_LOG:
    case ST_BUILTIN_POW:
    case ST_BUILTIN_INT_TO_REAL:
      return ST_TYPE_REAL;

    // Returns BOOL
    case ST_BUILTIN_INT_TO_BOOL:
    case ST_BUILTIN_MB_READ_COIL:      // MB_READ_COIL → BOOL
    case ST_BUILTIN_MB_READ_INPUT:     // MB_READ_INPUT → BOOL
    case ST_BUILTIN_MB_WRITE_COIL:     // MB_WRITE_COIL → BOOL (success flag)
    case ST_BUILTIN_MB_WRITE_HOLDING:  // MB_WRITE_HOLDING → BOOL (success flag)
    case ST_BUILTIN_R_TRIG:            // R_TRIG → BOOL
    case ST_BUILTIN_F_TRIG:            // F_TRIG → BOOL
    case ST_BUILTIN_TON:               // TON → BOOL
    case ST_BUILTIN_TOF:               // TOF → BOOL
    case ST_BUILTIN_TP:                // TP → BOOL
    case ST_BUILTIN_CTU:               // CTU → BOOL
    case ST_BUILTIN_CTD:               // CTD → BOOL
    case ST_BUILTIN_CTUD:              // CTUD → BOOL
    case ST_BUILTIN_BIT_TST:           // BIT_TST → BOOL
    case ST_BUILTIN_MB_SUCCESS:        // MB_SUCCESS → BOOL
    case ST_BUILTIN_MB_BUSY:           // MB_BUSY → BOOL
      return ST_TYPE_BOOL;

    // Returns DWORD
    case ST_BUILTIN_INT_TO_DWORD:
      return ST_TYPE_DWORD;

    // Returns INT (most functions)
    case ST_BUILTIN_ABS:
    case ST_BUILTIN_MIN:
    case ST_BUILTIN_MAX:
    case ST_BUILTIN_SUM:
    case ST_BUILTIN_ROUND:
    case ST_BUILTIN_TRUNC:
    case ST_BUILTIN_FLOOR:
    case ST_BUILTIN_CEIL:
    case ST_BUILTIN_LIMIT:
    case ST_BUILTIN_SEL:
    case ST_BUILTIN_MUX:          // MUX returns same type as inputs (defaults to INT)
    case ST_BUILTIN_ROL:          // ROL returns same type as input (INT/DINT/DWORD)
    case ST_BUILTIN_ROR:          // ROR returns same type as input (INT/DINT/DWORD)
    case ST_BUILTIN_REAL_TO_INT:
    case ST_BUILTIN_BOOL_TO_INT:
    case ST_BUILTIN_DWORD_TO_INT:
    case ST_BUILTIN_PERSIST_SAVE:
    case ST_BUILTIN_PERSIST_LOAD:
    case ST_BUILTIN_MB_READ_HOLDING:   // MB_READ_HOLDING → INT
    case ST_BUILTIN_MB_READ_INPUT_REG: // MB_READ_INPUT_REG → INT
    case ST_BUILTIN_BIT_SET:           // BIT_SET → INT
    case ST_BUILTIN_BIT_CLR:           // BIT_CLR → INT
    default:
      return ST_TYPE_INT;
  }
}
