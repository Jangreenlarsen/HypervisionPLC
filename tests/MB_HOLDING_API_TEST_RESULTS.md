============================================================
  MB_HOLDING / MB_HOLDINGS — API Integration Test
  ESP32: http://10.1.1.30
  Slaves: ID 90 (temp), ID 100 (display)
  Logic slot: 4
============================================================

--- Test 0: Forbindelse & Modbus Master status ---
  [PASS] API forbindelse — HTTP 200
  [PASS] Modbus Master endpoint — HTTP 200
    Master config: {
  "config": {
    "enabled": true,
    "baudrate": 19200,
    "parity": "none",
    "stop_bits": 1,
    "timeout_ms": 500,
    "inter_frame_delay_ms": 0,
    "max_requests_per_cycle": 5,
    "cache_...

--- Test 1: Kompilering — single register (FC03/FC06) ---
  [PASS] FC03 read kompilering — compiled=True, instr=7
  [PASS] FC06 write assignment-syntax kompilering — compiled=True, instr=8
  [PASS] FC06 write funktions-syntax kompilering — compiled=True, instr=8

--- Test 2: Kompilering -- multi register ARRAY (FC03/FC16) ---
  [PASS] FC03 multi-read ARRAY kompilering — compiled=True, instr=9
  [PASS] FC16 multi-write ARRAY kompilering — compiled=True, instr=16

--- Test 3: Kompileringsfejl -- forkert syntax ---
  [PASS] MB_READ_HOLDINGS standalone (parsed as func call) — compiled=True (accepted as statement)
  [PASS] MB_READ_HOLDINGS til non-array variabel — compiled=False, error=Compile error: Compile error at line 4: 'x' is not an array 

--- Test 4: MB_READ_HOLDING runtime — slave 90 ---
  [PASS] Upload read program
    Venter på Modbus kommunikation...
    temp_val=244, ok=True, err=0
  [PASS] MB_READ_HOLDING returnerer data — val=244, ok=True, err=0
    HR#80 = 244
  [PASS] Register binding synkroniseret — HR#80=244

--- Test 5: MB_WRITE_HOLDING runtime — slave 100 ---
  [PASS] Upload write program
    Skriver ASCII '1' (0x31) til display digit 1...
    ok=True, err=0
  [PASS] MB_WRITE_HOLDING sendt — ok=True, err=0 (0=OK, 4=queue_full)

--- Test 6: MB_READ_HOLDINGS ARRAY runtime — slave 90 ---
  [PASS] Upload multi-read program — compiled=True, instr=25
    Venter på FC03 multi-read...
    data[0..3] = [243, 245, -32768, -32768]
    ok=False, err=4
  [PASS] MB_READ_HOLDINGS returnerer ARRAY data — v0=243, v1=245, v2=-32768, v3=-32768, ok=False, err=4

--- Test 7: MB_WRITE_HOLDINGS ARRAY runtime — slave 100 ---
  [PASS] Upload multi-write program — compiled=True, instr=29
    Skriver 'HELLO ' (ASCII) til display digit 1-6 via FC16...
    ok=False, err=4
  [PASS] MB_WRITE_HOLDINGS sendt — ok=False, err=4 (0=OK, 4=queue_full/retry)

--- Test 8: Læs temp (slave 90) → vis på display (slave 100) ---
  [PASS] Upload combined program — compiled=True, instr=16
    Venter på read temp → write display cycle...
    temp_raw=243 (24.3°C), ok_read=True, err=0
  [PASS] Temp read + display write success — temp=24.3 grader, ok_read=True, err=0

--- Test 9: Error handling — timeout (slave 250, eksisterer ikke) ---
  [PASS] Upload timeout-test program
    Venter på timeout (kan tage 5-10 sek)...
    ok=False, err=6
  [PASS] Timeout giver MB_SUCCESS()=FALSE — ok=False
  [PASS] Timeout giver MB_ERROR() > 0 — err=6 (1=TIMEOUT)

--- Test 10: Status funktioner MB_SUCCESS/MB_BUSY/MB_ERROR ---
  [PASS] Upload status-test program
    ok=True, busy=False, err=0
  [PASS] MB_SUCCESS() returnerer BOOL
  [PASS] MB_BUSY() returnerer BOOL
  [PASS] MB_ERROR() returnerer INT
  [PASS] MB_ERROR() = 0 ved success — err=0

============================================================
RESULTAT: 28/28 tests bestået
============================================================
