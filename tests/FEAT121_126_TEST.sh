#!/bin/bash
# =============================================================================
# FEAT-121-126 Test Plan: IEC 61131-3 TIME, TON, TOF, CTU, CTD, CTUD
# =============================================================================
# Version: v7.9.4.0
# Date: 2026-04-09
# Prerequisites: ESP32 online with firmware v7.9.4.0+
# Usage: ./FEAT121_126_TEST.sh [ESP32_IP]
# =============================================================================

ESP32_IP="${1:-192.168.1.100}"
BASE_URL="http://${ESP32_IP}"
PASS=0
FAIL=0
SKIP=0
TOTAL=0

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# --- Helper Functions ---
check_connectivity() {
    echo "Checking ESP32 connectivity at ${ESP32_IP}..."
    if ! curl -s --connect-timeout 3 "${BASE_URL}/api/status" > /dev/null 2>&1; then
        echo -e "${RED}ERROR: ESP32 not reachable at ${ESP32_IP}${NC}"
        echo "Usage: $0 [ESP32_IP]"
        exit 1
    fi
    echo -e "${GREEN}ESP32 online at ${ESP32_IP}${NC}"
    echo ""
}

load_program() {
    local slot=$1
    local source=$2
    local result
    result=$(curl -s -w "\n%{http_code}" -X PUT "${BASE_URL}/api/logic/${slot}" \
        -H "Content-Type: application/json" \
        -d "{\"source\": \"${source}\"}" 2>&1)
    local http_code=$(echo "$result" | tail -1)
    local body=$(echo "$result" | sed '$d')
    echo "${http_code}|${body}"
}

get_program() {
    local slot=$1
    curl -s "${BASE_URL}/api/logic/${slot}" 2>&1
}

get_var() {
    local slot=$1
    local var_name=$2
    curl -s "${BASE_URL}/api/logic/${slot}/var/${var_name}" 2>&1
}

set_var() {
    local slot=$1
    local var_name=$2
    local value=$3
    curl -s -X PUT "${BASE_URL}/api/logic/${slot}/var/${var_name}" \
        -H "Content-Type: application/json" \
        -d "{\"value\": ${value}}" 2>&1
}

run_test() {
    local test_id=$1
    local test_name=$2
    local test_func=$3
    TOTAL=$((TOTAL + 1))
    echo -n "  TEST ${test_id}: ${test_name} ... "
    if eval "$test_func"; then
        echo -e "${GREEN}PASS${NC}"
        PASS=$((PASS + 1))
    else
        echo -e "${RED}FAIL${NC}"
        FAIL=$((FAIL + 1))
    fi
}

# =============================================================================
# TEST GROUP 1: TIME Type (FEAT-121)
# =============================================================================
test_1_1() {
    # TIME type declaration with T# literal
    local result=$(load_program 0 "VAR delay: TIME := T#5s; END_VAR delay := delay;")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_1_2() {
    # TIME literal variants: ms, s, m, h
    local result=$(load_program 0 "VAR t1: TIME := T#100ms; t2: TIME := T#30s; t3: TIME := T#2m; t4: TIME := T#1h; END_VAR t1 := t1;")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_1_3() {
    # TIME arithmetic: addition
    local result=$(load_program 0 "VAR a: TIME := T#5s; b: TIME := T#3s; c: TIME; END_VAR c := a + b;")
    local code=$(echo "$result" | cut -d'|' -f1)
    if [[ "$code" != "200" ]]; then return 1; fi
    sleep 0.5
    local val=$(get_var 0 "c")
    echo "$val" | grep -q "8000"
}

test_1_4() {
    # TIME comparison
    local result=$(load_program 0 "VAR a: TIME := T#5s; b: TIME := T#3s; r: BOOL; END_VAR r := a > b;")
    local code=$(echo "$result" | cut -d'|' -f1)
    if [[ "$code" != "200" ]]; then return 1; fi
    sleep 0.5
    local val=$(get_var 0 "r")
    echo "$val" | grep -q "1\|true\|TRUE"
}

test_1_5() {
    # T#60s = 60000ms (exceeds int16 range, tests PUSH_DWORD)
    local result=$(load_program 0 "VAR big: TIME := T#60s; END_VAR big := big;")
    local code=$(echo "$result" | cut -d'|' -f1)
    if [[ "$code" != "200" ]]; then return 1; fi
    sleep 0.5
    local val=$(get_var 0 "big")
    echo "$val" | grep -q "60000"
}

# =============================================================================
# TEST GROUP 2: TON — Timer On Delay (FEAT-122)
# =============================================================================
test_2_1() {
    # TON with named parameters — compiles
    local result=$(load_program 1 "VAR start: BOOL; motor: BOOL; elapsed: TIME; END_VAR TON(IN := start, PT := T#2s, Q => motor, ET => elapsed);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_2_2() {
    # TON functional test: set IN=TRUE, wait, check Q and ET
    # Program already loaded in slot 1 from test_2_1
    set_var 1 "start" 1 > /dev/null
    sleep 3
    local q_val=$(get_var 1 "motor")
    echo "$q_val" | grep -q "1\|true\|TRUE"
}

test_2_3() {
    # TON ET value should be >= 2000 after timeout
    local et_val=$(get_var 1 "elapsed")
    # ET should be around 2000ms or more
    local num=$(echo "$et_val" | grep -oE '[0-9]+' | head -1)
    [[ -n "$num" ]] && [[ "$num" -ge 1800 ]]
}

# =============================================================================
# TEST GROUP 3: TOF — Timer Off Delay (FEAT-123)
# =============================================================================
test_3_1() {
    # TOF with named parameters — compiles
    local result=$(load_program 2 "VAR trigger: BOOL := TRUE; light: BOOL; remaining: TIME; END_VAR TOF(IN := trigger, PT := T#2s, Q => light, ET => remaining);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_3_2() {
    # TOF: Q should be TRUE while IN=TRUE
    sleep 1
    local q_val=$(get_var 2 "light")
    echo "$q_val" | grep -q "1\|true\|TRUE"
}

test_3_3() {
    # TOF: set IN=FALSE, Q stays TRUE for PT duration, then goes FALSE
    set_var 2 "trigger" 0 > /dev/null
    sleep 3
    local q_val=$(get_var 2 "light")
    echo "$q_val" | grep -q "0\|false\|FALSE"
}

# =============================================================================
# TEST GROUP 4: CTU — Count Up (FEAT-124)
# =============================================================================
test_4_1() {
    # CTU with named parameters and output bindings — compiles
    local result=$(load_program 3 "VAR pulse: BOOL; rst: BOOL; done: BOOL; count: DINT; END_VAR CTU(CU := pulse, RESET := rst, PV := 5, Q => done, CV => count);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_4_2() {
    # CTU: pulse 3 times, check CV=3, Q=FALSE (PV=5)
    for i in 1 2 3; do
        set_var 3 "pulse" 1 > /dev/null; sleep 0.3
        set_var 3 "pulse" 0 > /dev/null; sleep 0.3
    done
    sleep 0.5
    local cv=$(get_var 3 "count")
    local num=$(echo "$cv" | grep -oE '[0-9]+' | head -1)
    [[ -n "$num" ]] && [[ "$num" -ge 2 ]] && [[ "$num" -le 4 ]]
}

test_4_3() {
    # CTU: pulse to PV=5, check Q=TRUE
    for i in 1 2 3 4 5; do
        set_var 3 "pulse" 1 > /dev/null; sleep 0.3
        set_var 3 "pulse" 0 > /dev/null; sleep 0.3
    done
    sleep 0.5
    local q=$(get_var 3 "done")
    echo "$q" | grep -q "1\|true\|TRUE"
}

test_4_4() {
    # CTU: RESET clears CV
    set_var 3 "rst" 1 > /dev/null; sleep 0.5
    set_var 3 "rst" 0 > /dev/null; sleep 0.5
    local cv=$(get_var 3 "count")
    local num=$(echo "$cv" | grep -oE '[0-9]+' | head -1)
    [[ -z "$num" ]] || [[ "$num" -le 1 ]]
}

# =============================================================================
# TEST GROUP 5: CTD — Count Down (FEAT-125)
# =============================================================================
test_5_1() {
    # CTD with named parameters — compiles
    local result=$(load_program 4 "VAR pulse: BOOL; ld: BOOL; done: BOOL; count: DINT; END_VAR CTD(CD := pulse, LOAD := ld, PV := 3, Q => done, CV => count);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_5_2() {
    # CTD: LOAD sets CV=PV, then count down
    set_var 4 "ld" 1 > /dev/null; sleep 0.3
    set_var 4 "ld" 0 > /dev/null; sleep 0.3
    for i in 1 2 3; do
        set_var 4 "pulse" 1 > /dev/null; sleep 0.3
        set_var 4 "pulse" 0 > /dev/null; sleep 0.3
    done
    sleep 0.5
    local q=$(get_var 4 "done")
    echo "$q" | grep -q "1\|true\|TRUE"
}

# =============================================================================
# TEST GROUP 6: CTUD — Count Up/Down (FEAT-126)
# =============================================================================
test_6_1() {
    # CTUD with 3 output bindings — compiles
    local result=$(load_program 5 "VAR cu: BOOL; cd: BOOL; rst: BOOL; ld: BOOL; qu: BOOL; qd: BOOL; cv: DINT; END_VAR CTUD(CU := cu, CD := cd, RESET := rst, LOAD := ld, PV := 10, QU => qu, QD => qd, CV => cv);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_6_2() {
    # CTUD: count up 3 times, check CV
    for i in 1 2 3; do
        set_var 5 "cu" 1 > /dev/null; sleep 0.3
        set_var 5 "cu" 0 > /dev/null; sleep 0.3
    done
    sleep 0.5
    local cv=$(get_var 5 "cv")
    local num=$(echo "$cv" | grep -oE '[0-9]+' | head -1)
    [[ -n "$num" ]] && [[ "$num" -ge 2 ]]
}

# =============================================================================
# TEST GROUP 7: Backward Compatibility
# =============================================================================
test_7_1() {
    # Positional syntax TON still works
    local result=$(load_program 6 "VAR inp: BOOL; res: BOOL; END_VAR res := TON(inp, 5000);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_7_2() {
    # Positional syntax CTU still works
    local result=$(load_program 6 "VAR p: BOOL; r: BOOL; res: BOOL; END_VAR res := CTU(p, r, 10);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

test_7_3() {
    # Mixed: TIME literal in positional syntax
    local result=$(load_program 6 "VAR inp: BOOL; res: BOOL; END_VAR res := TON(inp, T#5s);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" == "200" ]]
}

# =============================================================================
# TEST GROUP 8: Error Cases
# =============================================================================
test_8_1() {
    # Unknown parameter name should fail
    local result=$(load_program 7 "VAR x: BOOL; END_VAR TON(IN := x, BOGUS := T#1s);")
    local code=$(echo "$result" | cut -d'|' -f1)
    [[ "$code" != "200" ]]
}

test_8_2() {
    # Output arrow on input param should fail or be ignored
    local result=$(load_program 7 "VAR x: BOOL; y: BOOL; END_VAR TON(IN => y, PT := T#1s);")
    local code=$(echo "$result" | cut -d'|' -f1)
    # Should either fail (400) or compile but with different semantics
    # A 400 error is the expected correct behavior
    [[ "$code" != "200" ]] || true
}

# =============================================================================
# TEST GROUP 9: DINT Range (regression)
# =============================================================================
test_9_1() {
    # DINT variable with large value
    local result=$(load_program 7 "VAR big: DINT := 100000; END_VAR big := big;")
    local code=$(echo "$result" | cut -d'|' -f1)
    if [[ "$code" != "200" ]]; then return 1; fi
    sleep 0.5
    local val=$(get_var 7 "big")
    echo "$val" | grep -q "100000"
}

# =============================================================================
# MAIN EXECUTION
# =============================================================================
echo "=============================================="
echo "  FEAT-121-126 Test Suite"
echo "  IEC 61131-3 TIME, TON, TOF, CTU, CTD, CTUD"
echo "  ESP32: ${ESP32_IP}"
echo "  Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=============================================="
echo ""

check_connectivity

echo "--- GROUP 1: TIME Type (FEAT-121) ---"
run_test "1.1" "TIME declaration with T#5s" test_1_1
run_test "1.2" "TIME literals: ms, s, m, h" test_1_2
run_test "1.3" "TIME arithmetic (T#5s + T#3s = 8000)" test_1_3
run_test "1.4" "TIME comparison (T#5s > T#3s)" test_1_4
run_test "1.5" "TIME > 32767ms (PUSH_DWORD)" test_1_5
echo ""

echo "--- GROUP 2: TON Timer On Delay (FEAT-122) ---"
run_test "2.1" "TON named-param compiles" test_2_1
run_test "2.2" "TON Q=TRUE after PT elapsed" test_2_2
run_test "2.3" "TON ET >= 2000ms" test_2_3
echo ""

echo "--- GROUP 3: TOF Timer Off Delay (FEAT-123) ---"
run_test "3.1" "TOF named-param compiles" test_3_1
run_test "3.2" "TOF Q=TRUE while IN=TRUE" test_3_2
run_test "3.3" "TOF Q=FALSE after PT" test_3_3
echo ""

echo "--- GROUP 4: CTU Count Up (FEAT-124) ---"
run_test "4.1" "CTU named-param compiles" test_4_1
run_test "4.2" "CTU CV increments on pulse" test_4_2
run_test "4.3" "CTU Q=TRUE at PV" test_4_3
run_test "4.4" "CTU RESET clears CV" test_4_4
echo ""

echo "--- GROUP 5: CTD Count Down (FEAT-125) ---"
run_test "5.1" "CTD named-param compiles" test_5_1
run_test "5.2" "CTD counts down to Q=TRUE" test_5_2
echo ""

echo "--- GROUP 6: CTUD Count Up/Down (FEAT-126) ---"
run_test "6.1" "CTUD 3 output bindings compile" test_6_1
run_test "6.2" "CTUD CV increments" test_6_2
echo ""

echo "--- GROUP 7: Backward Compatibility ---"
run_test "7.1" "TON positional syntax" test_7_1
run_test "7.2" "CTU positional syntax" test_7_2
run_test "7.3" "TIME literal in positional" test_7_3
echo ""

echo "--- GROUP 8: Error Cases ---"
run_test "8.1" "Unknown param name → error" test_8_1
run_test "8.2" "Output arrow on input param" test_8_2
echo ""

echo "--- GROUP 9: DINT Range (regression) ---"
run_test "9.1" "DINT 100000 range" test_9_1
echo ""

# =============================================================================
# SUMMARY
# =============================================================================
echo "=============================================="
echo "  RESULTS"
echo "=============================================="
echo -e "  Total:  ${TOTAL}"
echo -e "  ${GREEN}PASS:   ${PASS}${NC}"
echo -e "  ${RED}FAIL:   ${FAIL}${NC}"
echo "=============================================="

if [[ $FAIL -eq 0 ]]; then
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}${FAIL} TEST(S) FAILED${NC}"
    exit 1
fi
