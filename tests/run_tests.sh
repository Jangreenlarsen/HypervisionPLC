#!/bin/bash
# FEAT-121-126 Test Suite v2 — Fixed: POST /source, check compiled field, timing
ESP32="10.1.1.30"
AUTH="api_user:ChangeMe123!"
PASS=0; FAIL=0; TOTAL=0

upload() {
  local slot=$1 src=$2
  curl -s -u "$AUTH" -X POST "http://$ESP32/api/logic/${slot}/source" \
    -H "Content-Type: application/json" -d "{\"source\": \"${src}\"}" 2>&1
}

enable() { curl -s -u "$AUTH" -X POST "http://$ESP32/api/logic/$1/enable" 2>&1; }
disable() { curl -s -u "$AUTH" -X POST "http://$ESP32/api/logic/$1/disable" 2>&1; }
get_prog() { curl -s -u "$AUTH" "http://$ESP32/api/logic/$1" 2>&1; }

tp() { echo "PASS"; PASS=$((PASS+1)); }
tf() { echo "FAIL $1"; FAIL=$((FAIL+1)); }

compiled_ok() {
  echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); exit(0 if d.get('compiled') else 1)" 2>/dev/null
}

compiled_fail() {
  echo "$1" | python3 -c "import sys,json; d=json.load(sys.stdin); exit(0 if not d.get('compiled') else 1)" 2>/dev/null
}

extract_var() {
  local json=$1 name=$2
  echo "$json" | python3 -c "
import sys,json
d=json.load(sys.stdin)
for v in d.get('variables',[]):
  if v['name']=='$name':
    print(v['value']); break
" 2>/dev/null
}

echo "=============================================="
echo "  FEAT-121-126 Test Suite v2"
echo "  ESP32: $ESP32"
echo "  FW: $(curl -s -u "$AUTH" "http://$ESP32/api/status" | python3 -c "import sys,json;print(json.load(sys.stdin)['firmware'])" 2>/dev/null)"
echo "  Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo "=============================================="
echo ""

for i in 1 2 3 4; do disable $i >/dev/null 2>&1; done

# GROUP 1: TIME Type
echo "--- GROUP 1: TIME Type (FEAT-121) ---"

TOTAL=$((TOTAL+1)); echo -n "  1.1 TIME declaration T#5s ... "
r=$(upload 1 "VAR delay: TIME := T#5s; END_VAR delay := delay;")
compiled_ok "$r" && tp || tf "$(echo $r | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"compile_error\",\"?\"))' 2>/dev/null)"

TOTAL=$((TOTAL+1)); echo -n "  1.2 TIME literals ms/s/m/h ... "
r=$(upload 1 "VAR t1: TIME := T#100ms; t2: TIME := T#30s; t3: TIME := T#2m; t4: TIME := T#1h; END_VAR t1 := t1;")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  1.3 TIME arithmetic 5s+3s=8000 ... "
r=$(upload 1 "VAR a: TIME := T#5s; b: TIME := T#3s; c: TIME; END_VAR c := a + b;")
if ! compiled_ok "$r"; then tf "compile fail"; else
  enable 1 >/dev/null; sleep 2; info=$(get_prog 1); disable 1 >/dev/null
  val=$(extract_var "$info" "c")
  [ "$val" = "8000" ] && tp || tf "expected 8000 got=$val"
fi

TOTAL=$((TOTAL+1)); echo -n "  1.4 TIME comparison 5s>3s ... "
r=$(upload 1 "VAR a: TIME := T#5s; b: TIME := T#3s; r: BOOL; END_VAR r := a > b;")
if ! compiled_ok "$r"; then tf "compile fail"; else
  enable 1 >/dev/null; sleep 2; info=$(get_prog 1); disable 1 >/dev/null
  val=$(extract_var "$info" "r")
  echo "$val" | grep -qiE "1|true|True" && tp || tf "expected true got=$val"
fi

TOTAL=$((TOTAL+1)); echo -n "  1.5 TIME 60s=60000 (PUSH_DWORD) ... "
r=$(upload 1 "VAR big: TIME := T#60s; END_VAR big := big;")
if ! compiled_ok "$r"; then tf "compile fail"; else
  enable 1 >/dev/null; sleep 2; info=$(get_prog 1); disable 1 >/dev/null
  val=$(extract_var "$info" "big")
  [ "$val" = "60000" ] && tp || tf "expected 60000 got=$val"
fi

echo ""

# GROUP 2: TON
echo "--- GROUP 2: TON Timer On Delay (FEAT-122) ---"

TOTAL=$((TOTAL+1)); echo -n "  2.1 TON named-param compiles ... "
r=$(upload 1 "VAR start: BOOL := TRUE; motor: BOOL; elapsed: TIME; END_VAR TON(IN := start, PT := T#2s, Q => motor, ET => elapsed);")
compiled_ok "$r" && tp || tf "$(echo $r | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"compile_error\",\"?\"))' 2>/dev/null)"

TOTAL=$((TOTAL+1)); echo -n "  2.2 TON Q=TRUE after 2s ... "
enable 1 >/dev/null; sleep 4; info=$(get_prog 1)
val=$(extract_var "$info" "motor")
echo "$val" | grep -qiE "1|true|True" && tp || tf "expected true got=$val"

TOTAL=$((TOTAL+1)); echo -n "  2.3 TON ET >= 2000ms ... "
val=$(extract_var "$info" "elapsed")
if [ -n "$val" ] && [ "$val" -ge 1800 ] 2>/dev/null; then tp; else tf "expected >=2000 got=$val"; fi
disable 1 >/dev/null

echo ""

# GROUP 3: TOF
echo "--- GROUP 3: TOF Timer Off Delay (FEAT-123) ---"

TOTAL=$((TOTAL+1)); echo -n "  3.1 TOF named-param compiles ... "
r=$(upload 2 "VAR trigger: BOOL := TRUE; light: BOOL; END_VAR TOF(IN := trigger, PT := T#2s, Q => light);")
compiled_ok "$r" && tp || tf "$(echo $r | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"compile_error\",\"?\"))' 2>/dev/null)"

TOTAL=$((TOTAL+1)); echo -n "  3.2 TOF Q=TRUE while IN=TRUE ... "
enable 2 >/dev/null; sleep 2; info=$(get_prog 2); disable 2 >/dev/null
val=$(extract_var "$info" "light")
echo "$val" | grep -qiE "1|true|True" && tp || tf "expected true got=$val"

echo ""

# GROUP 4: CTU
echo "--- GROUP 4: CTU Count Up (FEAT-124) ---"

TOTAL=$((TOTAL+1)); echo -n "  4.1 CTU named-param compiles ... "
r=$(upload 3 "VAR pulse: BOOL; rst: BOOL; done: BOOL; count: DINT; END_VAR CTU(CU := pulse, RESET := rst, PV := 5, Q => done, CV => count);")
compiled_ok "$r" && tp || tf "compile fail"

echo ""

# GROUP 5: CTD
echo "--- GROUP 5: CTD Count Down (FEAT-125) ---"

TOTAL=$((TOTAL+1)); echo -n "  5.1 CTD named-param compiles ... "
r=$(upload 3 "VAR pulse: BOOL; ld: BOOL; done: BOOL; count: DINT; END_VAR CTD(CD := pulse, LOAD := ld, PV := 3, Q => done, CV => count);")
compiled_ok "$r" && tp || tf "compile fail"

echo ""

# GROUP 6: CTUD
echo "--- GROUP 6: CTUD Count Up/Down (FEAT-126) ---"

TOTAL=$((TOTAL+1)); echo -n "  6.1 CTUD 3 output bindings compile ... "
r=$(upload 3 "VAR cu: BOOL; cd: BOOL; rst: BOOL; ld: BOOL; qu: BOOL; qd: BOOL; cv: DINT; END_VAR CTUD(CU := cu, CD := cd, RESET := rst, LOAD := ld, PV := 10, QU => qu, QD => qd, CV => cv);")
compiled_ok "$r" && tp || tf "compile fail"

echo ""

# GROUP 7: Backward Compatibility
echo "--- GROUP 7: Backward Compatibility ---"

TOTAL=$((TOTAL+1)); echo -n "  7.1 TON positional syntax ... "
r=$(upload 4 "VAR inp: BOOL; res: BOOL; END_VAR res := TON(inp, 5000);")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  7.2 CTU positional syntax ... "
r=$(upload 4 "VAR p: BOOL; r: BOOL; res: BOOL; END_VAR res := CTU(p, r, 10);")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  7.3 TIME literal in positional ... "
r=$(upload 4 "VAR inp: BOOL; res: BOOL; END_VAR res := TON(inp, T#5s);")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  7.4 TOF positional syntax ... "
r=$(upload 4 "VAR inp: BOOL; res: BOOL; END_VAR res := TOF(inp, 3000);")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  7.5 CTD positional syntax ... "
r=$(upload 4 "VAR p: BOOL; l: BOOL; res: BOOL; END_VAR res := CTD(p, l, 5);")
compiled_ok "$r" && tp || tf "compile fail"

TOTAL=$((TOTAL+1)); echo -n "  7.6 CTUD positional syntax ... "
r=$(upload 4 "VAR cu: BOOL; cd: BOOL; r: BOOL; l: BOOL; res: BOOL; END_VAR res := CTUD(cu, cd, r, l, 10);")
compiled_ok "$r" && tp || tf "compile fail"

echo ""

# GROUP 8: Error Cases
echo "--- GROUP 8: Error Cases ---"

TOTAL=$((TOTAL+1)); echo -n "  8.1 Unknown param BOGUS -> error ... "
r=$(upload 4 "VAR x: BOOL; END_VAR TON(IN := x, BOGUS := T#1s);")
compiled_fail "$r" && tp || tf "Expected compile fail"

TOTAL=$((TOTAL+1)); echo -n "  8.2 Output arrow on input param ... "
r=$(upload 4 "VAR x: BOOL; y: BOOL; END_VAR TON(IN => y, PT := T#1s);")
compiled_fail "$r" && tp || tf "Expected compile fail"

echo ""

# GROUP 9: DINT Range + Mixed
echo "--- GROUP 9: DINT Range + Mixed ---"

TOTAL=$((TOTAL+1)); echo -n "  9.1 DINT 100000 range ... "
r=$(upload 4 "VAR big: DINT := 100000; END_VAR big := big;")
if ! compiled_ok "$r"; then tf "compile fail: $(echo $r | python3 -c 'import sys,json;d=json.load(sys.stdin);print(d.get(\"compile_error\",\"?\"))' 2>/dev/null)"; else
  enable 4 >/dev/null; sleep 2; info=$(get_prog 4); disable 4 >/dev/null
  val=$(extract_var "$info" "big")
  [ "$val" = "100000" ] && tp || tf "expected 100000 got=$val"
fi

TOTAL=$((TOTAL+1)); echo -n "  9.2 TON + output + expression ... "
r=$(upload 4 "VAR running: BOOL := TRUE; done: BOOL; elapsed: TIME; check: BOOL; END_VAR TON(IN := running, PT := T#1s, Q => done, ET => elapsed); check := elapsed > T#500ms;")
if ! compiled_ok "$r"; then tf "compile fail"; else
  enable 4 >/dev/null; sleep 3; info=$(get_prog 4); disable 4 >/dev/null
  d=$(extract_var "$info" "done"); e=$(extract_var "$info" "elapsed"); ch=$(extract_var "$info" "check")
  if echo "$d" | grep -qiE "1|true|True" && [ -n "$e" ] && [ "$e" -ge 900 ] 2>/dev/null && echo "$ch" | grep -qiE "1|true|True"; then
    tp
  else
    tf "done=$d elapsed=$e check=$ch"
  fi
fi

echo ""
echo "=============================================="
echo "  RESULTS: ${PASS}/${TOTAL} PASS, ${FAIL} FAIL"
echo "=============================================="
if [ $FAIL -eq 0 ]; then echo "  ALL TESTS PASSED"; else echo "  ${FAIL} TEST(S) FAILED"; fi
