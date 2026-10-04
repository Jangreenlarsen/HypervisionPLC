# Test Plan - v4.6.0 Implementation

**Version:** v4.6.0
**Build:** #917
**Date:** 2026-01-01
**Status:** Ready for Testing

---

## 📋 Overview

This test plan covers validation of two major changes implemented in v4.6.0:

1. **BUG-133 FIX**: Reset g_mb_request_count in ST execution cycle
2. **FEATURE**: New MB_WRITE assignment syntax with variable support

---

## 🎯 Test Objectives

- ✅ Verify BUG-133 fix allows >10 Modbus requests per cycle
- ✅ Verify new MB_WRITE syntax compiles correctly
- ✅ Verify variable support for slave_id, address, and value arguments
- ✅ Verify expression support in all arguments
- ✅ Verify error handling for syntax errors
- ✅ Verify backward compatibility (if applicable)
- ✅ Verify memory management (no leaks in AST)

---

## 🔧 Test Environment

**Hardware:**
- ESP32-WROOM-32 DevKit
- RS-485 transceiver on UART1
- At least 2 Modbus RTU slave devices (for multi-request testing)

**Software:**
- Firmware: v4.6.0 Build #917
- PlatformIO build verified: ✅ SUCCESS

**Configuration:**
```bash
# Enable Modbus Master on UART1
set_modbus_master uart 1
set_modbus_master baud 9600
set_modbus_master enable

# Configure at least 2 remote slaves
# (Use real devices or Modbus simulators)
```

---

## 🧪 Test Cases

### 1. BUG-133 Fix - Request Counter Reset

**Test ID:** TC-001
**Priority:** 🔴 CRITICAL
**Description:** Verify system can process more than 10 Modbus requests in single ST cycle

**Test Case 1.1: Loop with >10 Requests**

**ST Program:**
```structured-text
(* Test: More than 10 requests in single cycle *)
VAR
  i: INT;
  values: ARRAY[0..14] OF INT;
END_VAR

(* Read 15 holding registers from slave 1 *)
FOR i := 0 TO 14 DO
  values[i] := MB_READ_HOLDING(1, 100 + i);
END_FOR;

(* If BUG-133 is fixed, all 15 reads should succeed *)
(* If BUG-133 exists, only first 10 succeed, rest fail *)
```

**Expected Result:**
- ✅ All 15 read operations complete successfully
- ✅ No `MB_MAX_REQUESTS_EXCEEDED` errors in `mb_last_error`
- ✅ Variable `values[14]` contains valid data (not default 0)

**How to Verify:**
```bash
# Upload program
set_st_program 1 source <paste ST code>
set_st_program 1 enable

# Monitor variables
show_st_vars 1

# Check mb_last_error (should be 0 = MB_OK)
# Check values array - all 15 elements should have data
```

**Pass Criteria:**
- All 15 requests execute without errors
- `mb_success` remains TRUE throughout loop

---

**Test Case 1.2: Mixed Read/Write Operations**

**ST Program:**
```structured-text
VAR
  i: INT;
  temp: INT;
END_VAR

(* 5 reads + 5 writes + 5 reads = 15 operations *)
FOR i := 0 TO 4 DO
  temp := MB_READ_HOLDING(1, 100 + i);
END_FOR;

FOR i := 0 TO 4 DO
  MB_WRITE_HOLDING(2, 200 + i) := i * 10;
END_FOR;

FOR i := 0 TO 4 DO
  temp := MB_READ_HOLDING(1, 110 + i);
END_FOR;
```

**Expected Result:**
- ✅ All 15 operations complete successfully
- ✅ No errors after 10th request

**Pass Criteria:**
- All read/write operations succeed
- No `MB_MAX_REQUESTS_EXCEEDED` error

---

### 2. New MB_WRITE Syntax - Basic Usage

**Test ID:** TC-002
**Priority:** 🔴 CRITICAL
**Description:** Verify new assignment-based syntax compiles and executes

**Test Case 2.1: MB_WRITE_COIL with Literal Arguments**

**ST Program:**
```structured-text
(* Test: Basic MB_WRITE_COIL syntax *)
MB_WRITE_COIL(3, 20) := TRUE;

IF mb_success THEN
  (* Coil write succeeded *)
ELSE
  (* Check mb_last_error *)
END_IF;
```

**Expected Result:**
- ✅ Program compiles without errors
- ✅ Coil 20 on slave 3 is set to TRUE
- ✅ `mb_success` is TRUE (assuming slave responds)

**How to Verify:**
```bash
# Read back coil value from slave 3
# (Use Modbus master tool or another ST program)
MB_READ_COIL(3, 20)  (* Should return TRUE *)
```

**Pass Criteria:**
- Compilation succeeds
- Coil write executes successfully
- Remote coil state changes to TRUE

---

**Test Case 2.2: MB_WRITE_HOLDING with Literal Arguments**

**ST Program:**
```structured-text
(* Test: Basic MB_WRITE_HOLDING syntax *)
MB_WRITE_HOLDING(2, 100) := 500;

IF mb_success THEN
  (* Holding write succeeded *)
END_IF;
```

**Expected Result:**
- ✅ Program compiles without errors
- ✅ Holding register 100 on slave 2 is set to 500
- ✅ `mb_success` is TRUE

**How to Verify:**
```bash
# Read back holding register value
MB_READ_HOLDING(2, 100)  (* Should return 500 *)
```

**Pass Criteria:**
- Compilation succeeds
- Holding register write executes successfully
- Remote register value is 500

---

### 3. Variable Support in Arguments

**Test ID:** TC-003
**Priority:** 🟡 HIGH
**Description:** Verify variables can be used for slave_id, address, and value

**Test Case 3.1: Variable slave_id**

**ST Program:**
```structured-text
VAR
  REMOTE_IO: INT := 3;
  heating_on: BOOL := TRUE;
END_VAR

MB_WRITE_COIL(REMOTE_IO, 20) := heating_on;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Writes to slave ID 3 (value of REMOTE_IO)
- ✅ Coil 20 is set to TRUE

**Pass Criteria:**
- Variable substitution works correctly
- Correct slave device receives command

---

**Test Case 3.2: Variable address**

**ST Program:**
```structured-text
VAR
  COIL_ADDR: INT := 25;
  valve_state: BOOL := FALSE;
END_VAR

MB_WRITE_COIL(3, COIL_ADDR) := valve_state;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Writes to address 25 (value of COIL_ADDR)
- ✅ Coil 25 is set to FALSE

**Pass Criteria:**
- Variable address works correctly
- Correct coil address is written

---

**Test Case 3.3: Variable value**

**ST Program:**
```structured-text
VAR
  setpoint: INT := 750;
END_VAR

MB_WRITE_HOLDING(2, 100) := setpoint;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Writes value 750 to register 100
- ✅ Remote register contains 750

**Pass Criteria:**
- Variable value is transmitted correctly
- Remote register matches local variable

---

**Test Case 3.4: All Arguments as Variables**

**ST Program:**
```structured-text
VAR
  REMOTE_IO: INT := 3;
  COIL_ADDR: INT := 20;
  heating_on: BOOL := TRUE;
END_VAR

MB_WRITE_COIL(REMOTE_IO, COIL_ADDR) := heating_on;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ All three variables are evaluated correctly
- ✅ Correct slave, address, and value

**Pass Criteria:**
- All variable arguments work simultaneously
- Correct Modbus command is sent

---

### 4. Expression Support in Arguments

**Test ID:** TC-004
**Priority:** 🟡 HIGH
**Description:** Verify expressions can be used in all arguments

**Test Case 4.1: Expression in address**

**ST Program:**
```structured-text
VAR
  BASE_ADDR: INT := 200;
  OFFSET: INT := 5;
END_VAR

MB_WRITE_HOLDING(2, BASE_ADDR + OFFSET) := 999;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Writes to address 205 (200 + 5)
- ✅ Register 205 contains 999

**Pass Criteria:**
- Expression evaluation is correct (200 + 5 = 205)
- Correct address is written

---

**Test Case 4.2: Expression in value**

**ST Program:**
```structured-text
VAR
  temp: INT := 20;
  scaling: INT := 2;
END_VAR

MB_WRITE_HOLDING(1, 100) := temp * scaling;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Writes value 40 (20 * 2) to register 100
- ✅ Remote register contains 40

**Pass Criteria:**
- Expression evaluation is correct (20 * 2 = 40)
- Correct value is written

---

**Test Case 4.3: Complex Expressions**

**ST Program:**
```structured-text
VAR
  i: INT := 3;
  base: INT := 100;
  offset: INT := 10;
END_VAR

MB_WRITE_HOLDING(2, base + (i * offset)) := (i + 1) * 100;
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ Address: 100 + (3 * 10) = 130
- ✅ Value: (3 + 1) * 100 = 400
- ✅ Register 130 contains 400

**Pass Criteria:**
- Complex expression evaluation is correct
- Correct address and value

---

**Test Case 4.4: Loop with Dynamic Addressing**

**ST Program:**
```structured-text
VAR
  i: INT;
END_VAR

(* Write sequence to registers 200-209 *)
FOR i := 0 TO 9 DO
  MB_WRITE_HOLDING(2, 200 + i) := i * 10;
END_FOR;

(* Expected:
   Reg 200 = 0
   Reg 201 = 10
   Reg 202 = 20
   ...
   Reg 209 = 90
*)
```

**Expected Result:**
- ✅ Compiles successfully
- ✅ All 10 registers are written with correct values
- ✅ Register 200 = 0, 201 = 10, ..., 209 = 90

**How to Verify:**
```bash
# Read back all 10 registers
FOR i := 0 TO 9 DO
  temp := MB_READ_HOLDING(2, 200 + i);
  (* temp should equal i * 10 *)
END_FOR;
```

**Pass Criteria:**
- Loop executes 10 times
- Each register contains correct calculated value

---

### 5. Error Handling

**Test ID:** TC-005
**Priority:** 🟡 HIGH
**Description:** Verify compiler rejects invalid syntax

**Test Case 5.1: Missing Arguments**

**ST Program:**
```structured-text
(* ERROR: Missing address argument *)
MB_WRITE_COIL(3) := TRUE;
```

**Expected Result:**
- ❌ Compilation FAILS with error message
- Error should mention "Expected comma" or similar

**Pass Criteria:**
- Compiler rejects invalid syntax
- Clear error message is shown

---

**Test Case 5.2: Missing Assignment Operator**

**ST Program:**
```structured-text
(* ERROR: Missing := *)
MB_WRITE_COIL(3, 20);
```

**Expected Result:**
- ❌ Compilation FAILS with error message
- Error should mention "Expected := after MB_WRITE function"

**Pass Criteria:**
- Compiler rejects syntax without :=
- Error message guides user to correct syntax

---

**Test Case 5.3: Old 3-Argument Syntax (Deprecated)**

**ST Program:**
```structured-text
(* ERROR: Old syntax no longer supported *)
success := MB_WRITE_COIL(3, 20, TRUE);
```

**Expected Result:**
- ❌ Compilation FAILS with error message
- Error should guide user to new syntax

**Pass Criteria:**
- Compiler rejects old syntax
- Error message suggests migration to new syntax

---

### 6. Memory Management

**Test ID:** TC-006
**Priority:** 🟠 MEDIUM
**Description:** Verify no memory leaks in AST handling

**Test Case 6.1: Multiple Compilations**

**Test Procedure:**
1. Upload ST program with new syntax
2. Compile program (set_st_program 1 source ...)
3. Repeat 10 times with different programs
4. Monitor free heap

**ST Program Example:**
```structured-text
VAR
  i: INT;
END_VAR

FOR i := 0 TO 5 DO
  MB_WRITE_HOLDING(2, 100 + i) := i;
END_FOR;
```

**Expected Result:**
- ✅ No memory leaks after multiple compilations
- ✅ Free heap remains stable
- ✅ AST nodes are properly freed (st_ast_node_free called)

**How to Verify:**
```bash
# Check free heap before/after
show_system_info

# Upload and compile 10 different programs
# Check heap again - should be similar
```

**Pass Criteria:**
- Heap usage remains stable (±1KB variance acceptable)
- No crash or out-of-memory errors

---

## 📊 Test Results Summary

### BUG-133 Fix
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-001.1 (>10 requests in loop) | ⬜ PENDING | |
| TC-001.2 (Mixed read/write) | ⬜ PENDING | |

### New Syntax - Basic Usage
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-002.1 (MB_WRITE_COIL literal) | ⬜ PENDING | |
| TC-002.2 (MB_WRITE_HOLDING literal) | ⬜ PENDING | |

### Variable Support
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-003.1 (Variable slave_id) | ⬜ PENDING | |
| TC-003.2 (Variable address) | ⬜ PENDING | |
| TC-003.3 (Variable value) | ⬜ PENDING | |
| TC-003.4 (All variables) | ⬜ PENDING | |

### Expression Support
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-004.1 (Expression in address) | ⬜ PENDING | |
| TC-004.2 (Expression in value) | ⬜ PENDING | |
| TC-004.3 (Complex expressions) | ⬜ PENDING | |
| TC-004.4 (Loop with dynamic addressing) | ⬜ PENDING | |

### Error Handling
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-005.1 (Missing arguments) | ⬜ PENDING | |
| TC-005.2 (Missing :=) | ⬜ PENDING | |
| TC-005.3 (Old syntax deprecated) | ⬜ PENDING | |

### Memory Management
| Test Case | Status | Notes |
|-----------|--------|-------|
| TC-006.1 (Multiple compilations) | ⬜ PENDING | |

---

## 🚀 Quick Start Testing

**Minimal Test Sequence (10 minutes):**

1. **Flash Firmware:**
   ```bash
   pio run -t upload
   pio device monitor -b 115200
   ```

2. **Configure Modbus Master:**
   ```bash
   set_modbus_master uart 1
   set_modbus_master baud 9600
   set_modbus_master enable
   ```

3. **Test BUG-133 Fix (Critical):**
   ```structured-text
   VAR i: INT; temp: INT; END_VAR
   FOR i := 1 TO 20 DO
     temp := MB_READ_HOLDING(1, i);
   END_FOR;
   (* Should complete all 20 reads - NOT block after 10 *)
   ```

4. **Test New Syntax:**
   ```structured-text
   MB_WRITE_COIL(3, 20) := TRUE;
   IF mb_success THEN
     (* SUCCESS - new syntax works! *)
   END_IF;
   ```

5. **Test Variable Support:**
   ```structured-text
   VAR
     REMOTE_IO: INT := 3;
     COIL_ADDR: INT := 20;
     heating_on: BOOL := TRUE;
   END_VAR

   MB_WRITE_COIL(REMOTE_IO, COIL_ADDR) := heating_on;
   ```

6. **Test Expression Support:**
   ```structured-text
   VAR i: INT; END_VAR
   FOR i := 0 TO 9 DO
     MB_WRITE_HOLDING(2, 200 + i) := i * 10;
   END_FOR;
   ```

**Pass Criteria for Quick Start:**
- ✅ All 6 programs compile without errors
- ✅ BUG-133 test completes all 20 requests
- ✅ New syntax executes correctly
- ✅ Variables work in all positions
- ✅ Expressions evaluate correctly

---

## 📝 Test Notes

### Known Limitations
1. **No backward compatibility** - Old 3-argument syntax (`MB_WRITE_COIL(3, 20, TRUE)`) no longer compiles
2. **Migration required** - Existing ST programs using old syntax must be updated

### Testing Tips
1. Use serial monitor to view compilation errors: `pio device monitor -b 115200`
2. Check `mb_success` and `mb_last_error` after each Modbus operation
3. Use `show_st_vars <prog_id>` to inspect variable values
4. Enable debug logging if available for detailed trace

### Required Hardware
- Minimum 1 Modbus RTU slave device (for basic testing)
- Recommended 2+ slaves (for multi-device testing)
- RS-485 USB adapter (for monitoring traffic if needed)

---

## ✅ Sign-Off

**Test Plan Created By:** Claude Sonnet 4.5
**Date:** 2026-01-01
**Version:** v4.6.0 Build #917
**Status:** Ready for Execution

**Test Execution To Be Completed By:** [Name]
**Completion Date:** [Date]
**Overall Result:** ⬜ PASS / ⬜ FAIL

---

**Next Steps After Testing:**
1. Execute all test cases
2. Update "Test Results Summary" table with ✅/❌ status
3. Document any failures in "Notes" column
4. Create bug reports for any issues found
5. Sign off on test completion
6. Archive test results with firmware build

---

**End of Test Plan**
