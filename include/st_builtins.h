/**
 * @file st_builtins.h
 * @brief Structured Text Built-in Functions (IEC 61131-3 Standard Library)
 *
 * Standard mathematical, conversion, and bitwise functions.
 * All functions follow IEC 61131-3 semantics.
 *
 * Usage from ST code:
 *   result := ABS(-5);           (* → 5 *)
 *   min_val := MIN(a, b);        (* → minimum of a, b *)
 *   max_val := MAX(a, b);        (* → maximum of a, b *)
 *   sqrt_val := SQRT(16.0);      (* → 4.0 *)
 *   real_val := INT_TO_REAL(10); (* → 10.0 *)
 */

#ifndef ST_BUILTINS_H
#define ST_BUILTINS_H

#include "st_types.h"

/* ============================================================================
 * BUILTIN FUNCTION ENUMERATION
 * ============================================================================ */

typedef enum {
  // Mathematical
  ST_BUILTIN_ABS,          // ABS(x) → |x|
  ST_BUILTIN_MIN,          // MIN(a, b) → minimum
  ST_BUILTIN_MAX,          // MAX(a, b) → maximum
  ST_BUILTIN_SUM,          // SUM(a, b) → a + b (alias for addition)
  ST_BUILTIN_SQRT,         // SQRT(x) → √x (REAL only)
  ST_BUILTIN_ROUND,        // ROUND(x) → rounded INT
  ST_BUILTIN_TRUNC,        // TRUNC(x) → truncated INT
  ST_BUILTIN_FLOOR,        // FLOOR(x) → floor INT
  ST_BUILTIN_CEIL,         // CEIL(x) → ceiling INT

  // Clamping & Selection (v4.4+)
  ST_BUILTIN_LIMIT,        // LIMIT(min, val, max) → clamped value
  ST_BUILTIN_SEL,          // SEL(g, in0, in1) → g ? in1 : in0

  // Trigonometric (v4.4+)
  ST_BUILTIN_SIN,          // SIN(x) → sine (radians)
  ST_BUILTIN_COS,          // COS(x) → cosine (radians)
  ST_BUILTIN_TAN,          // TAN(x) → tangent (radians)

  // Exponential Functions (v4.7+)
  ST_BUILTIN_EXP,          // EXP(x) → e^x
  ST_BUILTIN_LN,           // LN(x) → natural logarithm
  ST_BUILTIN_LOG,          // LOG(x) → base-10 logarithm
  ST_BUILTIN_POW,          // POW(x, y) → x^y

  // Type Conversion
  ST_BUILTIN_INT_TO_REAL,  // INT_TO_REAL(i) → REAL
  ST_BUILTIN_REAL_TO_INT,  // REAL_TO_INT(r) → INT
  ST_BUILTIN_BOOL_TO_INT,  // BOOL_TO_INT(b) → INT
  ST_BUILTIN_INT_TO_BOOL,  // INT_TO_BOOL(i) → BOOL
  ST_BUILTIN_DWORD_TO_INT, // DWORD_TO_INT(d) → INT
  ST_BUILTIN_INT_TO_DWORD, // INT_TO_DWORD(i) → DWORD

  // String (future: currently placeholder)
  // ST_BUILTIN_LEN,          // LEN(s) → length
  // ST_BUILTIN_CONCAT,       // CONCAT(s1, s2) → concatenation

  // Persistence (v4.0+)
  ST_BUILTIN_PERSIST_SAVE,  // SAVE() → save all persistent register groups to NVS
  ST_BUILTIN_PERSIST_LOAD,  // LOAD() → restore all groups from NVS

  // Modbus Master (v4.4+)
  ST_BUILTIN_MB_READ_COIL,      // MB_READ_COIL(slave_id, address) → BOOL
  ST_BUILTIN_MB_READ_INPUT,     // MB_READ_INPUT(slave_id, address) → BOOL
  ST_BUILTIN_MB_READ_HOLDING,   // MB_READ_HOLDING(slave_id, address) → INT
  ST_BUILTIN_MB_READ_INPUT_REG, // MB_READ_INPUT_REG(slave_id, address) → INT
  ST_BUILTIN_MB_WRITE_COIL,     // MB_WRITE_COIL(slave_id, address, value) → BOOL
  ST_BUILTIN_MB_WRITE_HOLDING,  // MB_WRITE_HOLDING(slave_id, address, value) → BOOL

  // Stateful Functions (v4.7+) - Require instance storage
  ST_BUILTIN_R_TRIG,        // R_TRIG(CLK) → BOOL (rising edge)
  ST_BUILTIN_F_TRIG,        // F_TRIG(CLK) → BOOL (falling edge)
  ST_BUILTIN_TON,           // TON(IN, PT) → BOOL (on-delay timer)
  ST_BUILTIN_TOF,           // TOF(IN, PT) → BOOL (off-delay timer)
  ST_BUILTIN_TP,            // TP(IN, PT) → BOOL (pulse timer)
  ST_BUILTIN_CTU,           // CTU(CU, RESET, PV) → BOOL (count up)
  ST_BUILTIN_CTD,           // CTD(CD, LOAD, PV) → BOOL (count down)
  ST_BUILTIN_CTUD,          // CTUD(CU, CD, RESET, LOAD, PV) → BOOL (count up/down)
  ST_BUILTIN_SR,            // SR(S1, R) → BOOL (set-reset latch, reset priority) v4.7.3
  ST_BUILTIN_RS,            // RS(S, R1) → BOOL (reset-set latch, set priority) v4.7.3

  // Signal Processing (v4.8)
  ST_BUILTIN_SCALE,         // SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX) → REAL (linear scaling)
  ST_BUILTIN_HYSTERESIS,    // HYSTERESIS(IN, HIGH, LOW) → BOOL (Schmitt trigger)
  ST_BUILTIN_BLINK,         // BLINK(ENABLE, ON_TIME, OFF_TIME) → BOOL (pulse generator)
  ST_BUILTIN_FILTER,        // FILTER(IN, TIME_CONSTANT) → REAL (low-pass filter)
  ST_BUILTIN_MUX,           // MUX(K, IN0, IN1, IN2) → ANY (4-way multiplexer, select by index)

  // Bit Rotation (v4.8.4)
  ST_BUILTIN_ROL,           // ROL(IN, N) → INT/DINT/DWORD (rotate left by N bits)
  ST_BUILTIN_ROR,           // ROR(IN, N) → INT/DINT/DWORD (rotate right by N bits)

  // Bit manipulation (v7.3.1)
  ST_BUILTIN_BIT_SET,       // BIT_SET(value, bit_pos) → INT (set bit N)
  ST_BUILTIN_BIT_CLR,       // BIT_CLR(value, bit_pos) → INT (clear bit N)
  ST_BUILTIN_BIT_TST,       // BIT_TST(value, bit_pos) → BOOL (test bit N)

  // Async Modbus Status (v7.7.0)
  ST_BUILTIN_MB_SUCCESS,    // MB_SUCCESS() → BOOL (last cache read was valid)
  ST_BUILTIN_MB_BUSY,       // MB_BUSY() → BOOL (requests pending in queue)
  ST_BUILTIN_MB_ERROR,      // MB_ERROR() → INT (last error code)

  ST_BUILTIN_COUNT          // Total number of built-ins
} st_builtin_func_t;

/* ============================================================================
 * FUNCTION CALL INSTRUCTION
 * ============================================================================ */

/**
 * @brief Call built-in function
 *
 * Functions pop operands from stack and push result.
 * Operand count depends on function:
 *   - ABS, SQRT, ROUND, etc.: 1 operand
 *   - MIN, MAX, SUM: 2 operands
 *
 * @param func_id Function ID (st_builtin_func_t)
 * @param arg1 First operand (or result for single-arg functions)
 * @param arg2 Second operand (for functions taking 2 args)
 * @return Result value
 */
st_value_t st_builtin_call(st_builtin_func_t func_id, st_value_t arg1, st_value_t arg2);

/**
 * @brief Get function name (for debugging)
 * @param func_id Function ID
 * @return Function name string
 */
const char *st_builtin_name(st_builtin_func_t func_id);

/**
 * @brief Get number of arguments for function
 * @param func_id Function ID
 * @return 1 or 2
 */
uint8_t st_builtin_arg_count(st_builtin_func_t func_id);

/**
 * @brief Get return type of builtin function
 * @param func_id Function ID
 * @return Return datatype (ST_TYPE_INT, ST_TYPE_REAL, ST_TYPE_BOOL, ST_TYPE_DWORD)
 */
st_datatype_t st_builtin_return_type(st_builtin_func_t func_id);

/* ============================================================================
 * MATHEMATICAL FUNCTIONS
 * ============================================================================ */

/**
 * @brief Absolute value
 * @param x Input value
 * @return |x|
 */
st_value_t st_builtin_abs(st_value_t x);

/**
 * @brief Minimum of two values
 * @param a First value
 * @param b Second value
 * @return min(a, b)
 */
st_value_t st_builtin_min(st_value_t a, st_value_t b);

/**
 * @brief Maximum of two values
 * @param a First value
 * @param b Second value
 * @return max(a, b)
 */
st_value_t st_builtin_max(st_value_t a, st_value_t b);

/**
 * @brief Square root
 * @param x Input value (REAL)
 * @return √x
 */
st_value_t st_builtin_sqrt(st_value_t x);

/**
 * @brief Round to nearest integer
 * @param x Input value (REAL)
 * @return Rounded INT
 */
st_value_t st_builtin_round(st_value_t x);

/**
 * @brief Truncate to integer (remove decimal)
 * @param x Input value (REAL)
 * @return Truncated INT
 */
st_value_t st_builtin_trunc(st_value_t x);

/**
 * @brief Floor function
 * @param x Input value (REAL)
 * @return Floor INT
 */
st_value_t st_builtin_floor(st_value_t x);

/**
 * @brief Ceiling function
 * @param x Input value (REAL)
 * @return Ceiling INT
 */
st_value_t st_builtin_ceil(st_value_t x);

/* ============================================================================
 * CLAMPING & SELECTION FUNCTIONS (v4.4+)
 * ============================================================================ */

/**
 * @brief Limit value to range [min, max]
 * @param min_val Minimum value
 * @param value Current value
 * @param max_val Maximum value
 * @return Clamped value
 */
st_value_t st_builtin_limit(st_value_t min_val, st_value_t value, st_value_t max_val);

/**
 * @brief Boolean selector
 * @param g Boolean selector (false=in0, true=in1)
 * @param in0 Value when g=false
 * @param in1 Value when g=true
 * @return Selected value
 */
st_value_t st_builtin_sel(st_value_t g, st_value_t in0, st_value_t in1);
st_value_t st_builtin_mux(st_value_t k, st_value_t in0, st_value_t in1, st_value_t in2);

/* ============================================================================
 * TRIGONOMETRIC FUNCTIONS (v4.4+)
 * ============================================================================ */

/**
 * @brief Sine function
 * @param x Angle in radians (REAL)
 * @return sin(x)
 */
st_value_t st_builtin_sin(st_value_t x);

/**
 * @brief Cosine function
 * @param x Angle in radians (REAL)
 * @return cos(x)
 */
st_value_t st_builtin_cos(st_value_t x);

/**
 * @brief Tangent function
 * @param x Angle in radians (REAL)
 * @return tan(x)
 */
st_value_t st_builtin_tan(st_value_t x);

/* ============================================================================
 * EXPONENTIAL FUNCTIONS (v4.7+)
 * ============================================================================ */

/**
 * @brief Exponential function
 * @param x Exponent value (REAL)
 * @return e^x
 */
st_value_t st_builtin_exp(st_value_t x);

/**
 * @brief Natural logarithm
 * @param x Input value (REAL, must be > 0)
 * @return ln(x) (base e)
 */
st_value_t st_builtin_ln(st_value_t x);

/**
 * @brief Base-10 logarithm
 * @param x Input value (REAL, must be > 0)
 * @return log10(x)
 */
st_value_t st_builtin_log(st_value_t x);

/**
 * @brief Power function
 * @param x Base value (REAL)
 * @param y Exponent value (REAL)
 * @return x^y
 */
st_value_t st_builtin_pow(st_value_t x, st_value_t y);

/* ============================================================================
 * TYPE CONVERSION FUNCTIONS
 * ============================================================================ */

/**
 * @brief Convert INT to REAL
 * @param i Integer value
 * @return Floating-point representation
 */
st_value_t st_builtin_int_to_real(st_value_t i);

/**
 * @brief Convert REAL to INT
 * @param r Floating-point value
 * @return Truncated integer
 */
st_value_t st_builtin_real_to_int(st_value_t r);

/**
 * @brief Convert BOOL to INT
 * @param b Boolean value
 * @return 1 if true, 0 if false
 */
st_value_t st_builtin_bool_to_int(st_value_t b);

/**
 * @brief Convert INT to BOOL
 * @param i Integer value
 * @return TRUE if non-zero, FALSE if zero
 */
st_value_t st_builtin_int_to_bool(st_value_t i);

/**
 * @brief Convert DWORD to INT
 * @param d DWORD value
 * @return INT (truncated if overflow)
 */
st_value_t st_builtin_dword_to_int(st_value_t d);

/**
 * @brief Convert INT to DWORD
 * @param i Integer value
 * @return DWORD (unsigned)
 */
st_value_t st_builtin_int_to_dword(st_value_t i);

/* ============================================================================
 * BIT ROTATION FUNCTIONS (v4.8.4)
 * ============================================================================ */

/**
 * @brief Rotate bits left
 * @param in Input value (INT/DINT/DWORD)
 * @param n Number of positions to rotate (INT)
 * @param in_type Data type of input value
 * @return Rotated value (same type as input)
 */
st_value_t st_builtin_rol(st_value_t in, st_value_t n, st_datatype_t in_type);

/**
 * @brief Rotate bits right
 * @param in Input value (INT/DINT/DWORD)
 * @param n Number of positions to rotate (INT)
 * @param in_type Data type of input value
 * @return Rotated value (same type as input)
 */
st_value_t st_builtin_ror(st_value_t in, st_value_t n, st_datatype_t in_type);

#endif // ST_BUILTINS_H
