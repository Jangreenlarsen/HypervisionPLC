#!/bin/bash
# ============================================================================
# API v1 Versioning Test — FEAT-030 komplet test
# Tester alle /api/v1/* endpoints inkl. nyligt tilføjede routes
# ============================================================================
#
# Brug:  bash tests/test_api_v1.sh [IP] [USER:PASS]
#
# Standard: IP=10.1.32.20  AUTH=api_user:ChangeMe123!
# ============================================================================

ESP32_IP="${1:-10.1.32.20}"
AUTH="${2:-api_user:ChangeMe123!}"
BASE="http://${ESP32_IP}"
V1="${BASE}/api/v1"

PASS=0
FAIL=0
SKIP=0
TOTAL=0

# Farver
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
NC='\033[0m'

run_test() {
  local num=$1
  local desc="$2"
  local method="${3:-GET}"
  local url="$4"
  local expect="${5:-200}"
  local body="$6"
  TOTAL=$((TOTAL + 1))

  local curl_args="-s -o /tmp/api_test_body.txt -w %{http_code} -u ${AUTH}"

  if [ "$method" = "POST" ]; then
    curl_args="$curl_args -X POST -H Content-Type:application/json"
    if [ -n "$body" ]; then
      curl_args="$curl_args -d $body"
    fi
  elif [ "$method" = "DELETE" ]; then
    curl_args="$curl_args -X DELETE"
  fi

  local status
  status=$(curl $curl_args "$url" 2>/dev/null)

  if [ "$status" = "$expect" ]; then
    echo -e "  ${GREEN}PASS${NC}  #${num}  ${desc}  (HTTP ${status})"
    PASS=$((PASS + 1))
  else
    echo -e "  ${RED}FAIL${NC}  #${num}  ${desc}  (HTTP ${status}, expected ${expect})"
    cat /tmp/api_test_body.txt 2>/dev/null | head -1
    echo ""
    FAIL=$((FAIL + 1))
  fi
}

run_test_json() {
  local num=$1
  local desc="$2"
  local url="$3"
  local jq_filter="$4"
  local expected="$5"
  TOTAL=$((TOTAL + 1))

  local response
  response=$(curl -s -u "${AUTH}" "$url" 2>/dev/null)
  local actual
  actual=$(echo "$response" | python3 -c "import sys,json; d=json.load(sys.stdin); print($jq_filter)" 2>/dev/null)

  if [ "$actual" = "$expected" ]; then
    echo -e "  ${GREEN}PASS${NC}  #${num}  ${desc}  (${actual})"
    PASS=$((PASS + 1))
  else
    echo -e "  ${RED}FAIL${NC}  #${num}  ${desc}  (got '${actual}', expected '${expected}')"
    FAIL=$((FAIL + 1))
  fi
}

compare_endpoints() {
  local num=$1
  local desc="$2"
  local url_v1="$3"
  local url_plain="$4"
  TOTAL=$((TOTAL + 1))

  local resp_v1 resp_plain
  resp_v1=$(curl -s -u "${AUTH}" "$url_v1" 2>/dev/null)
  resp_plain=$(curl -s -u "${AUTH}" "$url_plain" 2>/dev/null)

  if [ "$resp_v1" = "$resp_plain" ]; then
    echo -e "  ${GREEN}PASS${NC}  #${num}  ${desc}  (identical response)"
    PASS=$((PASS + 1))
  else
    # Check if both are valid JSON with same keys (values may differ due to timing)
    local keys_v1 keys_plain
    keys_v1=$(echo "$resp_v1" | python3 -c "import sys,json; print(sorted(json.load(sys.stdin).keys()))" 2>/dev/null)
    keys_plain=$(echo "$resp_plain" | python3 -c "import sys,json; print(sorted(json.load(sys.stdin).keys()))" 2>/dev/null)
    if [ -n "$keys_v1" ] && [ "$keys_v1" = "$keys_plain" ]; then
      echo -e "  ${GREEN}PASS${NC}  #${num}  ${desc}  (same structure, values may differ)"
      PASS=$((PASS + 1))
    else
      echo -e "  ${RED}FAIL${NC}  #${num}  ${desc}  (different responses)"
      echo "    v1:    $(echo "$resp_v1" | head -c 120)"
      echo "    plain: $(echo "$resp_plain" | head -c 120)"
      FAIL=$((FAIL + 1))
    fi
  fi
}

echo ""
echo -e "${CYAN}============================================================${NC}"
echo -e "${CYAN} API v1 Versioning Test — FEAT-030${NC}"
echo -e "${CYAN} Device: ${ESP32_IP}   Auth: ${AUTH%%:*}${NC}"
echo -e "${CYAN}============================================================${NC}"
echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}--- 1. API Version Endpoint ---${NC}"
# ------------------------------------------------------------------
run_test 1 "GET /api/version" GET "${BASE}/api/version"
run_test_json 2 "api_version = 1" "${BASE}/api/version" "d['api_version']" "1"
run_test_json 3 "versioned_prefix = /api/v1" "${BASE}/api/version" "d['versioned_prefix']" "/api/v1"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 2. v1 GET — Exact Match Endpoints ---${NC}"
# ------------------------------------------------------------------
run_test 4  "v1/status"           GET "${V1}/status"
run_test 5  "v1/config"           GET "${V1}/config"
run_test 6  "v1/counters"         GET "${V1}/counters"
run_test 7  "v1/timers"           GET "${V1}/timers"
run_test 8  "v1/logic"            GET "${V1}/logic"
run_test 9  "v1/gpio"             GET "${V1}/gpio"
run_test 10 "v1/wifi"             GET "${V1}/wifi"
run_test 11 "v1/ethernet"         GET "${V1}/ethernet"
run_test 12 "v1/debug"            GET "${V1}/debug"
run_test 13 "v1/modules"          GET "${V1}/modules"
run_test 14 "v1/hostname"         GET "${V1}/hostname"
run_test 15 "v1/telnet"           GET "${V1}/telnet"
run_test 16 "v1/system/watchdog"  GET "${V1}/system/watchdog"
run_test 17 "v1/system/backup"    GET "${V1}/system/backup"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 3. v1 GET — Bulk Register Read (NEW) ---${NC}"
# ------------------------------------------------------------------
run_test 18 "v1/registers/hr"     GET "${V1}/registers/hr"
run_test 19 "v1/registers/ir"     GET "${V1}/registers/ir"
run_test 20 "v1/registers/coils"  GET "${V1}/registers/coils"
run_test 21 "v1/registers/di"     GET "${V1}/registers/di"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 4. v1 GET — SSE + Version (NEW) ---${NC}"
# ------------------------------------------------------------------
run_test 22 "v1/events/status"    GET "${V1}/events/status"
run_test 23 "v1/version"          GET "${V1}/version"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 5. v1 GET — Heartbeat (NEW) ---${NC}"
# ------------------------------------------------------------------
run_test 24 "v1/gpio/2/heartbeat" GET "${V1}/gpio/2/heartbeat"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 6. v1 GET — Wildcard Resource IDs ---${NC}"
# ------------------------------------------------------------------
run_test 25 "v1/counters/1"       GET "${V1}/counters/1"
run_test 26 "v1/timers/1"         GET "${V1}/timers/1"
run_test 27 "v1/logic/1"          GET "${V1}/logic/1"
run_test 28 "v1/registers/hr/0"   GET "${V1}/registers/hr/0"
run_test 29 "v1/registers/ir/0"   GET "${V1}/registers/ir/0"
run_test 30 "v1/registers/coils/0" GET "${V1}/registers/coils/0"
run_test 31 "v1/registers/di/0"   GET "${V1}/registers/di/0"
run_test 32 "v1/modbus/slave"     GET "${V1}/modbus/slave"
run_test 33 "v1/gpio/2"           GET "${V1}/gpio/2"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 7. v1 POST — Write Operations ---${NC}"
# ------------------------------------------------------------------
run_test 34 "v1 write HR[0]=42"  POST "${V1}/registers/hr/0" 200 '{"value":42}'
run_test 35 "v1 verify HR[0]=42" GET  "${V1}/registers/hr/0"
run_test_json 36 "HR[0] value=42" "${V1}/registers/hr/0" "d['value']" "42"
run_test 37 "v1 restore HR[0]=0" POST "${V1}/registers/hr/0" 200 '{"value":0}'

run_test 38 "v1 write coil[0]=1" POST "${V1}/registers/coils/0" 200 '{"value":true}'
run_test 39 "v1 restore coil[0]" POST "${V1}/registers/coils/0" 200 '{"value":false}'

run_test 40 "v1 bulk HR write"   POST "${V1}/registers/hr/bulk" 200 '{"writes":[{"addr":1,"value":100},{"addr":2,"value":200}]}'
run_test 41 "v1 restore HR[1]"   POST "${V1}/registers/hr/1" 200 '{"value":0}'
run_test 42 "v1 restore HR[2]"   POST "${V1}/registers/hr/2" 200 '{"value":0}'

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 8. v1 vs unversioned — Parity Check ---${NC}"
# ------------------------------------------------------------------
compare_endpoints 43 "status: v1 == plain"    "${V1}/status"    "${BASE}/api/status"
compare_endpoints 44 "counters: v1 == plain"  "${V1}/counters"  "${BASE}/api/counters"
compare_endpoints 45 "timers: v1 == plain"    "${V1}/timers"    "${BASE}/api/timers"
compare_endpoints 46 "modules: v1 == plain"   "${V1}/modules"   "${BASE}/api/modules"
compare_endpoints 47 "registers/hr: v1==plain" "${V1}/registers/hr" "${BASE}/api/registers/hr"

# ------------------------------------------------------------------
echo ""
echo -e "${YELLOW}--- 9. Auth + Error Handling ---${NC}"
# ------------------------------------------------------------------
TOTAL=$((TOTAL + 1))
status_noauth=$(curl -s -o /dev/null -w "%{http_code}" "${V1}/status" 2>/dev/null)
if [ "$status_noauth" = "401" ]; then
  echo -e "  ${GREEN}PASS${NC}  #48  v1 rejects unauthenticated  (HTTP 401)"
  PASS=$((PASS + 1))
else
  echo -e "  ${RED}FAIL${NC}  #48  v1 rejects unauthenticated  (HTTP ${status_noauth}, expected 401)"
  FAIL=$((FAIL + 1))
fi

run_test 49 "v1/nonexistent -> 404" GET "${V1}/nonexistent" 404

# ------------------------------------------------------------------
echo ""
echo -e "${CYAN}============================================================${NC}"
if [ $FAIL -eq 0 ]; then
  echo -e "${GREEN} RESULT: ${PASS}/${TOTAL} PASS (100%)${NC}"
else
  echo -e "${RED} RESULT: ${PASS}/${TOTAL} PASS, ${FAIL} FAIL${NC}"
fi
echo -e "${CYAN}============================================================${NC}"
echo ""

# Cleanup
rm -f /tmp/api_test_body.txt

exit $FAIL
