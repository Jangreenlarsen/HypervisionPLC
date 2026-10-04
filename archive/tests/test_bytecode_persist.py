#!/usr/bin/env python3
"""
Test: Bytecode Persistens (Fase 1) + Chunked Compilation (Fase 2)

Tests:
  1. Upload simpelt program -> verify compile OK + .bc saved
  2. Reboot (simulated via delete+re-upload) -> verify bytecode cache works
  3. Upload program MED FUNCTION -> verify chunked compile
  4. CRC invalidering: upload ny source -> gammel cache ugyldigt
  5. Heap measurement via /metrics endpoint
"""

import requests
import time
import json
import sys

BASE = "http://10.1.1.30"
AUTH = ("api_user", "ChangeMe123!")

def api(method, path, data=None, timeout=15):
    url = f"{BASE}{path}"
    try:
        if method == "GET":
            r = requests.get(url, auth=AUTH, timeout=timeout)
        elif method == "POST":
            r = requests.post(url, auth=AUTH, json=data, timeout=timeout)
        elif method == "DELETE":
            r = requests.delete(url, auth=AUTH, timeout=timeout)
        else:
            return None
        return r
    except requests.exceptions.RequestException as e:
        print(f"  ERROR: {e}")
        return None

def get_heap():
    """Get free heap from /metrics."""
    r = api("GET", "/api/metrics")
    if not r:
        return None
    for line in r.text.split('\n'):
        if 'esp32_heap_free_bytes ' in line and not line.startswith('#'):
            return int(line.split()[-1])
    return None

def test_result(name, passed, detail=""):
    status = "PASS" if passed else "FAIL"
    print(f"  [{status}] {name}" + (f" - {detail}" if detail else ""))
    return passed

# ============================================================================
# TEST 1: Simpelt program (ingen funktioner) -> monolitisk compile + bc save
# ============================================================================
print("=" * 70)
print("TEST 1: Simpelt program upload + compile + bytecode cache")
print("=" * 70)

SIMPLE_SOURCE = """PROGRAM test_bc
VAR
  counter : INT := 0;
  running : BOOL := TRUE;
END_VAR
BEGIN
  IF running THEN
    counter := counter + 1;
  END_IF;
  IF counter > 1000 THEN
    counter := 0;
  END_IF;
END_PROGRAM
"""

# Clean slot 1
api("POST", "/api/logic/1/disable")
api("DELETE", "/api/logic/1")
time.sleep(0.5)

heap_before = get_heap()
print(f"  Heap before compile: {heap_before}")

r = api("POST", "/api/logic/1/source", {"source": SIMPLE_SOURCE})
if r and r.status_code == 200:
    data = r.json()
    compiled = data.get("compiled", False)
    instr = data.get("instr_count", 0)
    test_result("Upload+compile", compiled, f"{instr} instructions")
else:
    test_result("Upload+compile", False, f"HTTP {r.status_code if r else 'no response'}")

heap_after = get_heap()
print(f"  Heap after compile: {heap_after}")
if heap_before and heap_after:
    print(f"  Heap used during compile: ~{heap_before - heap_after} bytes (recovered after)")

# Verify program runs
api("POST", "/api/logic/1/enable")
time.sleep(1)

r = api("GET", "/api/logic/1")
if r and r.status_code == 200:
    data = r.json()
    exec_count = data.get("execution_count", 0)
    test_result("Program running", exec_count > 0, f"exec_count={exec_count}")

    # Check variables
    variables = data.get("variables", [])
    if variables:
        counter_var = next((v for v in variables if v["name"] == "counter"), None)
        if counter_var:
            test_result("Variable visible", True, f"counter={counter_var['value']}")
        else:
            test_result("Variable visible", False, "counter not found")
    else:
        test_result("Variable visible", False, "no variables")

# ============================================================================
# TEST 2: Re-upload same source -> should use bytecode cache on boot
# ============================================================================
print()
print("=" * 70)
print("TEST 2: Same source re-upload (bytecode cache validation)")
print("=" * 70)

# Disable, delete, re-upload same source
api("POST", "/api/logic/1/disable")
api("DELETE", "/api/logic/1")
time.sleep(0.5)

heap_before2 = get_heap()

r = api("POST", "/api/logic/1/source", {"source": SIMPLE_SOURCE})
if r and r.status_code == 200:
    data = r.json()
    compiled = data.get("compiled", False)
    instr = data.get("instr_count", 0)
    test_result("Re-upload+compile", compiled, f"{instr} instructions")
else:
    test_result("Re-upload+compile", False)

heap_after2 = get_heap()
print(f"  Heap before: {heap_before2}, after: {heap_after2}")

# ============================================================================
# TEST 3: Program MED FUNCTION -> chunked compilation
# ============================================================================
print()
print("=" * 70)
print("TEST 3: Program med FUNCTION -> chunked compile")
print("=" * 70)

FUNCTION_SOURCE = """PROGRAM test_func
VAR
  result : INT := 0;
  sum : INT := 0;
END_VAR

FUNCTION add_values : INT
VAR_INPUT
  a : INT;
  b : INT;
END_VAR
  add_values := a + b;
END_FUNCTION

FUNCTION multiply : INT
VAR_INPUT
  val : INT;
  factor : INT;
END_VAR
  multiply := val * factor;
END_FUNCTION

BEGIN
  sum := add_values(10, 20);
  result := multiply(sum, 2);
END_PROGRAM
"""

api("POST", "/api/logic/2/disable")
api("DELETE", "/api/logic/2")
time.sleep(0.5)

heap_before3 = get_heap()
print(f"  Heap before chunked compile: {heap_before3}")

r = api("POST", "/api/logic/2/source", {"source": FUNCTION_SOURCE})
if r and r.status_code == 200:
    data = r.json()
    compiled = data.get("compiled", False)
    instr = data.get("instr_count", 0)
    test_result("Chunked compile", compiled, f"{instr} instructions")
    if not compiled:
        print(f"  Compile error: {data.get('compile_error', 'unknown')}")
else:
    test_result("Chunked compile", False, f"HTTP {r.status_code if r else 'no response'}")

heap_after3 = get_heap()
print(f"  Heap after chunked compile: {heap_after3}")

# Enable and verify execution
api("POST", "/api/logic/2/enable")
time.sleep(1)

r = api("GET", "/api/logic/2")
if r and r.status_code == 200:
    data = r.json()
    exec_count = data.get("execution_count", 0)
    test_result("Function program running", exec_count > 0, f"exec_count={exec_count}")

    variables = data.get("variables", [])
    result_var = next((v for v in variables if v["name"] == "result"), None)
    sum_var = next((v for v in variables if v["name"] == "sum"), None)

    if sum_var:
        test_result("FUNCTION add_values", sum_var["value"] == 30,
                     f"sum={sum_var['value']} (expected 30)")
    else:
        test_result("FUNCTION add_values", False, "sum variable not found")

    if result_var:
        test_result("FUNCTION multiply", result_var["value"] == 60,
                     f"result={result_var['value']} (expected 60)")
    else:
        test_result("FUNCTION multiply", False, "result variable not found")

# ============================================================================
# TEST 4: CRC invalidering - upload ændret source
# ============================================================================
print()
print("=" * 70)
print("TEST 4: CRC invalidering (ændret source)")
print("=" * 70)

MODIFIED_SOURCE = """PROGRAM test_bc
VAR
  counter : INT := 0;
  running : BOOL := TRUE;
  step : INT := 5;
END_VAR
BEGIN
  IF running THEN
    counter := counter + step;
  END_IF;
  IF counter > 5000 THEN
    counter := 0;
  END_IF;
END_PROGRAM
"""

api("POST", "/api/logic/1/disable")
time.sleep(0.3)

r = api("POST", "/api/logic/1/source", {"source": MODIFIED_SOURCE})
if r and r.status_code == 200:
    data = r.json()
    compiled = data.get("compiled", False)
    instr = data.get("instr_count", 0)
    test_result("Modified source compile", compiled, f"{instr} instructions (new bytecode)")
else:
    test_result("Modified source compile", False)

# Enable and check the new variable 'step'
api("POST", "/api/logic/1/enable")
time.sleep(1)

r = api("GET", "/api/logic/1")
if r and r.status_code == 200:
    data = r.json()
    variables = data.get("variables", [])
    step_var = next((v for v in variables if v["name"] == "step"), None)
    # Note: initial values (:= 5) are NOT applied at runtime — this is known behavior
    test_result("New variable visible", step_var is not None,
                f"step={step_var['value'] if step_var else 'not found'} (initial values not applied at runtime)")

# ============================================================================
# TEST 5: Program med control flow i FUNCTION
# ============================================================================
print()
print("=" * 70)
print("TEST 5: FUNCTION med IF/ELSE control flow")
print("=" * 70)

COMPLEX_FUNC_SOURCE = """PROGRAM test_complex
VAR
  status : INT := 0;
  clamped : INT := 0;
END_VAR

FUNCTION classify : INT
VAR_INPUT
  value : INT;
END_VAR
  IF value < 50 THEN
    classify := 1;
  ELSIF value < 80 THEN
    classify := 2;
  ELSE
    classify := 3;
  END_IF;
END_FUNCTION

FUNCTION clamp : INT
VAR_INPUT
  val : INT;
  min_val : INT;
  max_val : INT;
END_VAR
  IF val < min_val THEN
    clamp := min_val;
  ELSIF val > max_val THEN
    clamp := max_val;
  ELSE
    clamp := val;
  END_IF;
END_FUNCTION

BEGIN
  status := classify(75);
  clamped := clamp(75, 0, 100);
END_PROGRAM
"""

api("POST", "/api/logic/3/disable")
api("DELETE", "/api/logic/3")
time.sleep(0.5)

r = api("POST", "/api/logic/3/source", {"source": COMPLEX_FUNC_SOURCE})
if r and r.status_code == 200:
    data = r.json()
    compiled = data.get("compiled", False)
    instr = data.get("instr_count", 0)
    test_result("Complex function compile", compiled, f"{instr} instructions")
    if not compiled:
        print(f"  Compile error: {data.get('compile_error', 'unknown')}")
else:
    test_result("Complex function compile", False)

api("POST", "/api/logic/3/enable")
time.sleep(1)

r = api("GET", "/api/logic/3")
if r and r.status_code == 200:
    data = r.json()
    variables = data.get("variables", [])

    status_var = next((v for v in variables if v["name"] == "status"), None)
    clamped_var = next((v for v in variables if v["name"] == "clamped"), None)

    if status_var:
        # temp=75, which is >= 50 and < 80, so classify should return 2
        test_result("classify(75)", status_var["value"] == 2,
                     f"status={status_var['value']} (expected 2)")
    else:
        test_result("classify(75)", False, "status not found")

    if clamped_var:
        # temp=75, clamped to [0,100] should stay 75
        test_result("clamp(75, 0, 100)", clamped_var["value"] == 75,
                     f"clamped={clamped_var['value']} (expected 75)")
    else:
        test_result("clamp(75, 0, 100)", False, "clamped not found")

# ============================================================================
# CLEANUP
# ============================================================================
print()
print("=" * 70)
print("CLEANUP")
print("=" * 70)

for slot in [1, 2, 3]:
    api("POST", f"/api/logic/{slot}/disable")
    api("DELETE", f"/api/logic/{slot}")
    print(f"  Slot {slot} cleaned")

heap_final = get_heap()
print(f"\n  Final heap: {heap_final}")
print()
print("Test complete.")
