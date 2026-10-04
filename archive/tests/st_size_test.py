#!/usr/bin/env python3
"""
ST Logic compiler stress test — progressively larger programs.
Tests the AST node memory optimization (BUG-240 fix).
Uses GPIO 101-108 (DI, 74HC165) and GPIO 201-202 (DO, 74HC595).
"""

import requests
import time
import json
import sys

BASE = "http://10.1.1.30"
AUTH = ("api_user", "ChangeMe123!")
SLOT = 1

def api(method, path, data=None):
    """API call with auth."""
    url = f"{BASE}{path}"
    if method == "GET":
        r = requests.get(url, auth=AUTH, timeout=10)
    elif method == "POST":
        r = requests.post(url, auth=AUTH, json=data, timeout=30)
    return r

def disable_logic():
    """Disable logic slot before uploading."""
    try:
        api("POST", f"/api/logic/{SLOT}/disable")
    except:
        pass
    time.sleep(0.2)

def upload_and_test(name, source):
    """Upload ST program and report result."""
    disable_logic()

    src_bytes = len(source.encode('utf-8'))
    lines = source.count('\n') + 1

    print(f"\n{'='*60}")
    print(f"TEST: {name}")
    print(f"  Source: {src_bytes} bytes, {lines} lines")
    print(f"{'='*60}")

    # Upload
    r = api("POST", f"/api/logic/{SLOT}/source", {"source": source})
    resp = r.json()

    if r.status_code != 200 or not resp.get("compiled", False):
        print(f"  COMPILE: FAILED — {resp.get('error', resp.get('message', 'unknown'))}")
        return False, src_bytes, 0, 0

    source_size = resp.get("source_size", 0)
    print(f"  COMPILE: OK — source_size={source_size}")
    instructions = 0
    size = 0

    # Enable
    r = api("POST", f"/api/logic/{SLOT}/enable")
    if r.status_code != 200:
        print(f"  ENABLE: FAILED")
        return False, src_bytes, instructions, size

    # Wait and check execution
    time.sleep(2)
    r = api("GET", f"/api/logic/{SLOT}")
    status = r.json()

    exec_count = status.get("execution_count", 0)
    error_count = status.get("error_count", 0)
    exec_us = status.get("last_execution_us", 0)

    if exec_count > 0 and error_count == 0:
        print(f"  EXECUTE: OK — {exec_count} cycles, {exec_us} µs/cycle, 0 errors")
        # Show some variables
        variables = status.get("variables", [])
        for v in variables[:6]:
            print(f"    {v['name']} = {v['value']}")
        success = True
    else:
        print(f"  EXECUTE: FAILED — {exec_count} cycles, {error_count} errors")
        success = False

    # Check heap
    r = api("GET", "/api/metrics")
    for line in r.text.split('\n'):
        if 'esp32_heap_free_bytes ' in line and not line.startswith('#'):
            heap = int(line.split()[-1])
            print(f"  HEAP: {heap} bytes free")

    disable_logic()
    return success, src_bytes, instructions, size


def gen_small():
    """Test 1: Minimal program — 3 variables, simple logic."""
    return """PROGRAM test_small
VAR
  a : INT;
  b : INT;
  c : INT;
END_VAR
a := a + 1;
b := a * 2;
c := b - a;
END_PROGRAM"""


def gen_medium():
    """Test 2: Medium — 8 variables, IF/ELSE, arithmetic."""
    return """PROGRAM test_medium
VAR
  counter : INT;
  limit : INT;
  flag : BOOL;
  result : INT;
  temp : INT;
  acc : INT;
  prev : INT;
  step : INT;
END_VAR
limit := 1000;
step := 1;
prev := counter;
counter := counter + step;
IF counter >= limit THEN
  counter := 0;
  flag := TRUE;
ELSE
  flag := FALSE;
END_IF;
temp := counter * 3;
acc := acc + temp;
IF acc > 30000 THEN
  acc := 0;
END_IF;
result := acc - prev;
END_PROGRAM"""


def gen_large():
    """Test 3: Large — 16 variables, nested IF, multiple calculations."""
    return """PROGRAM test_large
VAR
  c1 : INT;
  c2 : INT;
  c3 : INT;
  c4 : INT;
  sum : INT;
  avg : INT;
  max_val : INT;
  min_val : INT;
  flag1 : BOOL;
  flag2 : BOOL;
  flag3 : BOOL;
  mode : INT;
  delay : INT;
  output : INT;
  scale : INT;
  offset : INT;
END_VAR

scale := 10;
offset := 5;
delay := delay + 1;

IF delay >= 100 THEN
  delay := 0;
  c1 := c1 + 1;
  IF c1 > 999 THEN
    c1 := 0;
    c2 := c2 + 1;
  END_IF;
  IF c2 > 99 THEN
    c2 := 0;
    c3 := c3 + 1;
  END_IF;
  IF c3 > 9 THEN
    c3 := 0;
    c4 := c4 + 1;
  END_IF;
  IF c4 > 9 THEN
    c4 := 0;
  END_IF;
END_IF;

sum := c1 + c2 + c3 + c4;
avg := sum / 4;

IF c1 > c2 THEN
  IF c1 > c3 THEN
    max_val := c1;
  ELSE
    max_val := c3;
  END_IF;
ELSE
  IF c2 > c3 THEN
    max_val := c2;
  ELSE
    max_val := c3;
  END_IF;
END_IF;

IF c1 < c2 THEN
  IF c1 < c3 THEN
    min_val := c1;
  ELSE
    min_val := c3;
  END_IF;
ELSE
  IF c2 < c3 THEN
    min_val := c2;
  ELSE
    min_val := c3;
  END_IF;
END_IF;

flag1 := max_val > 50;
flag2 := min_val < 10;
flag3 := flag1 AND flag2;

IF flag3 THEN
  mode := 1;
ELSIF flag1 THEN
  mode := 2;
ELSIF flag2 THEN
  mode := 3;
ELSE
  mode := 0;
END_IF;

output := (sum * scale) + offset;
IF output > 9999 THEN
  output := 9999;
END_IF;
END_PROGRAM"""


def gen_xlarge():
    """Test 4: Extra large — 16 vars, sequential IF blocks."""
    lines = ["PROGRAM test_xlarge", "VAR"]
    for i in range(16):
        lines.append(f"  v{i} : INT;")
    lines.append("END_VAR")
    lines.append("")

    # 15 sequential simple IF blocks (no deep nesting)
    for s in range(15):
        v = f"v{s % 16}"
        lines.append(f"v{s % 16} := v{s % 16} + 1;")
        lines.append(f"IF v{s % 16} > {100 + s * 50} THEN")
        lines.append(f"  v{s % 16} := 0;")
        lines.append(f"END_IF;")

    lines.append("END_PROGRAM")
    return "\n".join(lines)


def gen_xxlarge():
    """Test 5: XXL — 16 vars, 25 sequential IF blocks with arithmetic."""
    lines = ["PROGRAM test_xxlarge", "VAR"]
    for i in range(16):
        lines.append(f"  v{i} : INT;")
    lines.append("END_VAR")
    lines.append("")

    for s in range(25):
        v = f"v{s % 16}"
        lines.append(f"{v} := {v} + {1 + s % 3};")
        lines.append(f"IF {v} > {200 + s * 30} THEN")
        lines.append(f"  {v} := 0;")
        lines.append(f"END_IF;")

    lines.append("END_PROGRAM")
    return "\n".join(lines)


def gen_monster():
    """Test 6: Monster — 16 vars, 40 sequential blocks."""
    lines = ["PROGRAM test_monster", "VAR"]
    for i in range(16):
        lines.append(f"  v{i} : INT;")
    lines.append("END_VAR")
    lines.append("")

    for s in range(40):
        v = f"v{s % 16}"
        lines.append(f"{v} := {v} + {1 + s % 5};")
        lines.append(f"IF {v} > {100 + s * 20} THEN")
        lines.append(f"  {v} := 0;")
        lines.append(f"END_IF;")

    lines.append("END_PROGRAM")
    return "\n".join(lines)


def gen_ultra():
    """Test 7: Ultra — 16 vars, 60 sequential blocks."""
    lines = ["PROGRAM test_ultra", "VAR"]
    for i in range(16):
        lines.append(f"  v{i} : INT;")
    lines.append("END_VAR")
    lines.append("")

    for s in range(60):
        v = f"v{s % 16}"
        lines.append(f"{v} := {v} + {1 + s % 7};")
        lines.append(f"IF {v} > {50 + s * 15} THEN")
        lines.append(f"  {v} := 0;")
        lines.append(f"END_IF;")

    lines.append("END_PROGRAM")
    return "\n".join(lines)


def gen_max():
    """Test 8: MAX — 16 vars, 100 sequential blocks."""
    lines = ["PROGRAM test_max", "VAR"]
    for i in range(16):
        lines.append(f"  v{i} : INT;")
    lines.append("END_VAR")
    lines.append("")

    for s in range(100):
        v = f"v{s % 16}"
        lines.append(f"{v} := {v} + {1 + s % 9};")
        lines.append(f"IF {v} > {50 + s * 10} THEN")
        lines.append(f"  {v} := 0;")
        lines.append(f"END_IF;")

    lines.append("END_PROGRAM")
    return "\n".join(lines)


# ─── Main ───

tests = [
    ("1-SMALL (baseline)", gen_small),
    ("2-MEDIUM (8 vars, IF/ELSE)", gen_medium),
    ("3-LARGE (16 vars, nested IF)", gen_large),
    ("4-XLARGE (15 IF blocks)", gen_xlarge),
    ("5-XXLARGE (25 IF blocks)", gen_xxlarge),
    ("6-MONSTER (40 IF blocks)", gen_monster),
    ("7-ULTRA (60 IF blocks)", gen_ultra),
    ("8-MAX (100 IF blocks)", gen_max),
]

results = []
print("=" * 60)
print("ST LOGIC COMPILER STRESS TEST")
print("Firmware: v7.2.0 (BUG-240 AST node fix)")
print(f"Target: {BASE}")
print("=" * 60)

# Get initial heap
r = api("GET", "/api/metrics")
for line in r.text.split('\n'):
    if 'esp32_heap_free_bytes ' in line and not line.startswith('#'):
        print(f"Initial heap: {line.split()[-1]} bytes free")

for name, gen_func in tests:
    source = gen_func()
    success, src_bytes, _, _ = upload_and_test(name, source)
    results.append({
        "name": name,
        "success": success,
        "source_bytes": src_bytes,
    })

    if not success:
        print(f"\n>>> LIMIT REACHED at test: {name} ({src_bytes} bytes source)")
        break

    time.sleep(1)

# Summary
print(f"\n\n{'='*60}")
print("SUMMARY")
print(f"{'='*60}")
print(f"{'Test':<35} {'Source':>7} {'Result':>8}")
print(f"{'-'*35} {'-'*7} {'-'*8}")
for r in results:
    status = "PASS" if r["success"] else "FAIL"
    print(f"{r['name']:<35} {r['source_bytes']:>7} {status:>8}")

# Final cleanup
disable_logic()
print(f"\nDone. {sum(1 for r in results if r['success'])}/{len(results)} tests passed.")
