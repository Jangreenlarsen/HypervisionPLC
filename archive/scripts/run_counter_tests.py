#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 Counter System - Automated Test Runner
Runs all tests from test_plan_counter.md and generates report
"""

import serial
import time
import sys
import os
from datetime import datetime

# Set console encoding for Windows
if sys.platform == 'win32':
    os.system('chcp 65001 > nul')
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    sys.stderr.reconfigure(encoding='utf-8', errors='replace')

# Configuration
SERIAL_PORT = 'COM11'
BAUDRATE = 115200
TIMEOUT = 2

# Test results storage
test_results = []

def open_serial():
    """Open serial connection to ESP32"""
    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=TIMEOUT)
        time.sleep(0.5)  # Wait for connection to stabilize
        # Flush any existing data
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        return ser
    except Exception as e:
        print(f"ERROR: Could not open serial port {SERIAL_PORT}: {e}")
        sys.exit(1)

def send_command(ser, cmd, wait_time=0.5):
    """Send command to ESP32 and get response"""
    try:
        # Send command
        ser.write((cmd + '\n').encode('utf-8'))
        time.sleep(wait_time)

        # Read response
        response = ""
        while ser.in_waiting:
            response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            time.sleep(0.1)

        return response
    except Exception as e:
        print(f"ERROR sending command '{cmd}': {e}")
        return ""

def parse_register_value(response, reg_num):
    """Parse register value from 'show registers' response"""
    lines = response.split('\n')
    for line in lines:
        # Look for line starting with base address
        if ':' in line:
            parts = line.split(':')
            try:
                base = int(parts[0].strip())
                # Parse values after colon (tab-separated)
                values = parts[1].strip().split('\t')

                # Check if our register is in this line
                if base <= reg_num < base + len(values):
                    offset = reg_num - base
                    value = int(values[offset])
                    return value
            except:
                pass
    return None

def log_test(test_num, test_name, status, expected, actual, notes=""):
    """Log test result"""
    result = {
        'test': test_num,
        'name': test_name,
        'status': status,
        'expected': expected,
        'actual': actual,
        'notes': notes,
        'timestamp': datetime.now().isoformat()
    }
    test_results.append(result)

    status_icon = "✅" if status == "PASS" else "❌" if status == "FAIL" else "⚠️"
    print(f"{status_icon} Test {test_num}: {test_name}")
    print(f"   Expected: {expected}")
    print(f"   Actual: {actual}")
    if notes:
        print(f"   Notes: {notes}")
    print()

def test_1_1_hw_mode_basis(ser):
    """Test 1.1: HW mode basis counting"""
    print("\n" + "="*60)
    print("TEST 1.1: HW Mode Basis Counting (GPIO 19)")
    print("="*60)

    # Configure counter
    cmd = "set counter 1 mode 1 hw-mode:hw edge:rising direction:up hw-gpio:19 prescaler:1 bit-width:32 index-reg:100 raw-reg:110 freq-reg:120 overload-reg:130 ctrl-reg:140"
    response = send_command(ser, cmd, 1.0)
    print(f"Config: {response[:100]}...")

    # Start counter
    response = send_command(ser, "set register 140 value:2", 0.5)
    print("Counter started...")

    # Wait 10 seconds
    print("Waiting 10 seconds for counting...")
    time.sleep(10)

    # Read registers (show registers 100 40 to get 100-139 range)
    resp = send_command(ser, "show registers 100 40", 0.5)
    val_100 = parse_register_value(resp, 100)
    val_110 = parse_register_value(resp, 110)
    val_120 = parse_register_value(resp, 120)
    val_130 = parse_register_value(resp, 130)

    # Evaluate (5 kHz × 10 sec = 50k pulses, ±5% tolerance)
    expected_count = 50000
    tolerance = 0.05

    if val_100 is not None:
        deviation = abs(val_100 - expected_count) / expected_count
        status = "PASS" if deviation <= tolerance else "FAIL"
        log_test("1.1", "HW mode basis counting", status,
                 f"~50k (±5%)", f"{val_100}",
                 f"Index={val_100}, Raw={val_110}, Freq={val_120}, Overflow={val_130}")
    else:
        log_test("1.1", "HW mode basis counting", "FAIL",
                 f"~50k", "Could not read register",
                 f"Response: {resp_100[:100]}")

    return val_100  # Return for next test

def test_1_2_stop_start(ser, initial_value):
    """Test 1.2: Stop/Start functionality"""
    print("\n" + "="*60)
    print("TEST 1.2: Stop/Start Functionality")
    print("="*60)

    # Stop counter
    response = send_command(ser, "set register 140 value:4", 0.5)
    print("Counter stopped...")

    # Read value
    resp = send_command(ser, "show registers 100 8", 0.5)
    v1 = parse_register_value(resp, 100)
    print(f"Value when stopped: {v1}")

    # Wait 5 seconds
    print("Waiting 5 seconds (counter stopped)...")
    time.sleep(5)

    # Read again - should be same
    resp = send_command(ser, "show registers 100 8", 0.5)
    v2 = parse_register_value(resp, 100)
    print(f"Value after 5 sec: {v2}")

    # Check if stopped
    if v1 is not None and v2 is not None:
        if v1 == v2:
            status = "PASS"
            notes = f"Counter correctly stopped at {v1}"
        else:
            status = "FAIL"
            notes = f"Counter changed from {v1} to {v2} while stopped!"
    else:
        status = "FAIL"
        notes = "Could not read register values"

    log_test("1.2a", "Stop functionality", status,
             "No change when stopped", f"{v1} → {v2}", notes)

    # Start again
    response = send_command(ser, "set register 140 value:2", 0.5)
    print("Counter restarted...")

    # Wait 5 seconds
    print("Waiting 5 seconds (counter running)...")
    time.sleep(5)

    # Read again - should have changed
    resp_v3 = send_command(ser, "show registers 100 8", 0.5)
    v3 = parse_register_value(resp, 3)
    print(f"Value after restart: {v3}")

    if v2 is not None and v3 is not None:
        delta = v3 - v2
        expected_delta = 25000  # 5 kHz × 5 sec
        tolerance = 0.10
        deviation = abs(delta - expected_delta) / expected_delta

        if deviation <= tolerance:
            status = "PASS"
            notes = f"Counter resumed counting: +{delta} pulses"
        else:
            status = "FAIL"
            notes = f"Expected ~+25k, got +{delta}"
    else:
        status = "FAIL"
        notes = "Could not read register values"

    log_test("1.2b", "Start functionality", status,
             "~+25k after restart", f"+{delta if v3 and v2 else 'N/A'}", notes)

def test_1_3_reset(ser):
    """Test 1.3: Reset functionality"""
    print("\n" + "="*60)
    print("TEST 1.3: Reset Functionality")
    print("="*60)

    # Reset counter
    response = send_command(ser, "set register 140 value:1", 0.5)
    print("Counter reset...")

    # Read value - should be 0
    resp = send_command(ser, "show registers 100 8", 0.5)
    val_100 = parse_register_value(resp, 100)

    resp = send_command(ser, "show registers 130 8", 0.5)
    val_130 = parse_register_value(resp, 130)

    if val_100 == 0 and val_130 == 0:
        status = "PASS"
        notes = "Counter and overflow flag both reset to 0"
    else:
        status = "FAIL"
        notes = f"Counter={val_100}, Overflow={val_130}"

    log_test("1.3", "Reset functionality", status,
             "Counter=0, Overflow=0", f"Counter={val_100}, Overflow={val_130}", notes)

def test_1_4_direction_down(ser):
    """Test 1.4: Direction DOWN"""
    print("\n" + "="*60)
    print("TEST 1.4: Direction DOWN")
    print("="*60)

    # Reconfigure with DOWN direction and start_value=100000
    cmd = "set counter 1 mode 1 hw-mode:hw edge:rising direction:down hw-gpio:19 prescaler:1 bit-width:32 start-value:100000 index-reg:100 raw-reg:110 freq-reg:120 overload-reg:130 ctrl-reg:140"
    response = send_command(ser, cmd, 1.0)
    print("Reconfigured with DOWN direction, start=100000")

    # Start counter
    response = send_command(ser, "set register 140 value:2", 0.5)
    print("Counter started...")

    # Wait 5 seconds
    print("Waiting 5 seconds...")
    time.sleep(5)

    # Read value - should be ~75k (100k - 25k)
    resp = send_command(ser, "show registers 100 8", 0.5)
    val_100 = parse_register_value(resp, 100)

    expected = 75000
    tolerance = 0.10

    if val_100 is not None:
        deviation = abs(val_100 - expected) / expected
        if deviation <= tolerance:
            status = "PASS"
            notes = f"Counter correctly counting down from 100k"
        else:
            status = "FAIL"
            notes = f"Expected ~75k, got {val_100} (deviation {deviation*100:.1f}%)"
    else:
        status = "FAIL"
        notes = "Could not read register"

    log_test("1.4", "Direction DOWN", status,
             "~75k (100k - 25k)", f"{val_100}", notes)

def test_1_5_prescaler(ser):
    """Test 1.5: Prescaler (divide by 10)"""
    print("\n" + "="*60)
    print("TEST 1.5: Prescaler")
    print("="*60)

    # Reconfigure with prescaler=10
    cmd = "set counter 1 mode 1 hw-mode:hw edge:rising direction:up hw-gpio:19 prescaler:10 bit-width:32 index-reg:100 raw-reg:110 freq-reg:120 overload-reg:130 ctrl-reg:140"
    response = send_command(ser, cmd, 1.0)
    print("Reconfigured with prescaler=10")

    # Reset and start
    send_command(ser, "set register 140 value:1", 0.5)
    send_command(ser, "set register 140 value:2", 0.5)
    print("Counter reset and started...")

    # Wait 10 seconds
    print("Waiting 10 seconds...")
    time.sleep(10)

    # Read registers
    resp = send_command(ser, "show registers 100 8", 0.5)
    val_100 = parse_register_value(resp, 100)

    resp = send_command(ser, "show registers 110 8", 0.5)
    val_110 = parse_register_value(resp, 110)

    # Index should be ~50k (counts ALL edges)
    # Raw should be ~5k (divided by prescaler)
    expected_index = 50000
    expected_raw = 5000
    tolerance = 0.10

    status_index = "UNKNOWN"
    status_raw = "UNKNOWN"

    if val_100 is not None:
        dev_index = abs(val_100 - expected_index) / expected_index
        status_index = "PASS" if dev_index <= tolerance else "FAIL"

    if val_110 is not None:
        dev_raw = abs(val_110 - expected_raw) / expected_raw
        status_raw = "PASS" if dev_raw <= tolerance else "FAIL"

    overall_status = "PASS" if status_index == "PASS" and status_raw == "PASS" else "FAIL"

    log_test("1.5", "Prescaler (divide by 10)", overall_status,
             "Index=~50k, Raw=~5k", f"Index={val_100}, Raw={val_110}",
             f"Index counts ALL edges, Raw divided by prescaler")

def test_1_6_overflow_16bit(ser):
    """Test 1.6: 16-bit overflow"""
    print("\n" + "="*60)
    print("TEST 1.6: 16-bit Overflow")
    print("="*60)

    # Reconfigure with 16-bit width
    cmd = "set counter 1 mode 1 hw-mode:hw edge:rising direction:up hw-gpio:19 prescaler:1 bit-width:16 index-reg:100 raw-reg:110 freq-reg:120 overload-reg:130 ctrl-reg:140"
    response = send_command(ser, cmd, 1.0)
    print("Reconfigured with bit-width=16")

    # Reset and start
    send_command(ser, "set register 140 value:1", 0.5)
    send_command(ser, "set register 140 value:2", 0.5)
    print("Counter reset and started...")

    # Wait 15 seconds (75k pulses > 65535)
    print("Waiting 15 seconds for overflow...")
    time.sleep(15)

    # Read registers
    resp = send_command(ser, "show registers 100 8", 0.5)
    val_100 = parse_register_value(resp, 100)

    resp = send_command(ser, "show registers 130 8", 0.5)
    val_130 = parse_register_value(resp, 130)

    # 75000 mod 65536 = 9464
    expected_wrapped = 75000 % 65536  # = 9464
    tolerance = 0.10

    if val_100 is not None and val_130 is not None:
        dev = abs(val_100 - expected_wrapped) / expected_wrapped
        if dev <= tolerance and val_130 == 1:
            status = "PASS"
            notes = "Counter wrapped correctly, overflow flag set"
        else:
            status = "FAIL"
            notes = f"Expected wrap to ~{expected_wrapped} with overflow=1, got {val_100} with overflow={val_130}"
    else:
        status = "FAIL"
        notes = "Could not read registers"

    log_test("1.6", "16-bit overflow", status,
             f"~{expected_wrapped}, overflow=1", f"{val_100}, overflow={val_130}", notes)

def test_2_1_sw_isr_basis(ser):
    """Test 2.1: SW-ISR mode basis"""
    print("\n" + "="*60)
    print("TEST 2.1: SW-ISR Mode Basis (GPIO 13)")
    print("="*60)

    # Configure counter 2
    cmd = "set counter 2 mode 1 hw-mode:sw-isr edge:rising direction:up interrupt-pin:13 prescaler:1 bit-width:32 index-reg:200 raw-reg:210 freq-reg:220 overload-reg:230 ctrl-reg:240"
    response = send_command(ser, cmd, 1.0)
    print("Counter 2 configured in SW-ISR mode")

    # Start counter
    response = send_command(ser, "set register 240 value:2", 0.5)
    print("Counter started...")

    # Wait 10 seconds
    print("Waiting 10 seconds...")
    time.sleep(10)

    # Read registers
    resp = send_command(ser, "show registers 200 8", 0.5)
    val_200 = parse_register_value(resp, 200)

    resp = send_command(ser, "show registers 220 8", 0.5)
    val_220 = parse_register_value(resp, 220)

    expected_count = 50000
    expected_freq = 5000
    tolerance = 0.10

    if val_200 is not None:
        dev = abs(val_200 - expected_count) / expected_count
        status = "PASS" if dev <= tolerance else "FAIL"
    else:
        status = "FAIL"

    log_test("2.1", "SW-ISR mode basis", status,
             "~50k, freq=~5000", f"{val_200}, freq={val_220}",
             f"Interrupt-based counting on GPIO 13")

def generate_report():
    """Generate comprehensive test report"""
    print("\n" + "="*80)
    print("GENERATING TEST REPORT")
    print("="*80)

    # Count results
    total = len(test_results)
    passed = sum(1 for r in test_results if r['status'] == 'PASS')
    failed = sum(1 for r in test_results if r['status'] == 'FAIL')

    success_rate = (passed / total * 100) if total > 0 else 0

    # Generate report content
    report = f"""# ESP32 Counter System - Test Results Report

**Test Date:** {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
**Firmware:** Build #80
**Hardware:** ESP32-WROOM-32 (DOIT DevKit V1)
**Test Signals:** 5 kHz på GPIO 13 og GPIO 19

---

## EXECUTIVE SUMMARY

| Metric | Value |
|--------|-------|
| **Total Tests** | {total} |
| **Passed** | {passed} ({passed/total*100:.1f}%) |
| **Failed** | {failed} ({failed/total*100:.1f}%) |
| **Success Rate** | {success_rate:.1f}% |

**Overall Status:** {'✅ GODKENDT' if success_rate >= 90 else '⚠️ GODKENDT MED FORBEHOLD' if success_rate >= 75 else '❌ IKKE GODKENDT'}

---

## DETAILED TEST RESULTS

"""

    # Add each test result
    for result in test_results:
        status_icon = "✅" if result['status'] == 'PASS' else "❌" if result['status'] == 'FAIL' else "⚠️"
        report += f"""### Test {result['test']}: {result['name']}

**Status:** {status_icon} {result['status']}
**Expected:** {result['expected']}
**Actual:** {result['actual']}
**Notes:** {result['notes']}
**Timestamp:** {result['timestamp']}

---

"""

    # Add bug verification summary
    report += """## BUG VERIFICATION SUMMARY

| Bug | Feature | Test | Status |
|-----|---------|------|--------|
"""

    bug_tests = {
        '1.1': ('Overflow tracking SW', '9.1', 'PENDING'),
        '1.2': ('Overflow tracking SW-ISR', '9.2', 'PENDING'),
        '1.3': ('PCNT int16_t', '3', 'PENDING'),
        '1.4': ('HW delta wrap', '3', 'PENDING'),
        '1.5': ('Direction UP/DOWN', '1.4', 'TESTED'),
        '1.6': ('Start value', '5', 'PENDING'),
        '1.7': ('Debounce', '7', 'PENDING'),
        '1.8': ('Frequency 64-bit', '4', 'PENDING'),
        '1.9': ('GPIO mapping', '8', 'PENDING'),
        '2.1': ('Control bits', '1.2', 'TESTED'),
        '2.3': ('Bit width', '6', 'PENDING'),
        '3.1': ('ISR volatile', '2.3', 'PENDING'),
    }

    for bug, (feature, test, status) in bug_tests.items():
        # Find actual test results
        test_result = next((r for r in test_results if r['test'] == test), None)
        if test_result:
            status = '✅' if test_result['status'] == 'PASS' else '❌'
        report += f"| {bug} | {feature} | {test} | {status} |\n"

    report += f"""

---

## CONCLUSION

**Tests Completed:** {total} / 20
**Success Rate:** {success_rate:.1f}%

{'✅ **ANBEFALING: GODKENDT TIL PRODUKTION**' if success_rate >= 90 else '⚠️ **ANBEFALING: GODKENDT MED FORBEHOLD**' if success_rate >= 75 else '❌ **ANBEFALING: IKKE GODKENDT**'}

**Underskrift:** ___________________
**Dato:** {datetime.now().strftime('%Y-%m-%d')}

---

*Rapport genereret automatisk af run_counter_tests.py*
"""

    # Save report
    report_file = 'TEST_RESULTS_REPORT.md'
    with open(report_file, 'w', encoding='utf-8') as f:
        f.write(report)

    print(f"\n✅ Report saved to: {report_file}")
    print(f"\n📊 TEST SUMMARY: {passed}/{total} tests passed ({success_rate:.1f}%)")

    return report

def main():
    """Main test runner"""
    print("="*80)
    print("ESP32 COUNTER SYSTEM - AUTOMATED TEST RUNNER")
    print("="*80)
    print(f"Serial Port: {SERIAL_PORT}")
    print(f"Baudrate: {BAUDRATE}")
    print(f"Test Start: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("="*80)

    # Open serial connection
    ser = open_serial()
    print(f"✅ Serial connection opened\n")

    try:
        # Clear any startup messages
        time.sleep(1)
        ser.reset_input_buffer()

        # Run tests
        print("Starting tests...\n")

        # Test 1.1
        initial_val = test_1_1_hw_mode_basis(ser)
        time.sleep(2)

        # Test 1.2
        if initial_val is not None:
            test_1_2_stop_start(ser, initial_val)
            time.sleep(2)

        # Test 1.3
        test_1_3_reset(ser)
        time.sleep(2)

        # Test 1.4
        test_1_4_direction_down(ser)
        time.sleep(2)

        # Test 1.5
        test_1_5_prescaler(ser)
        time.sleep(2)

        # Test 1.6
        test_1_6_overflow_16bit(ser)
        time.sleep(2)

        # Test 2.1
        test_2_1_sw_isr_basis(ser)
        time.sleep(2)

        # Generate report
        generate_report()

    except KeyboardInterrupt:
        print("\n\n⚠️ Test interrupted by user")
    except Exception as e:
        print(f"\n\n❌ ERROR during testing: {e}")
        import traceback
        traceback.print_exc()
    finally:
        # Close serial
        ser.close()
        print("\n✅ Serial connection closed")
        print(f"Test End: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")

if __name__ == '__main__':
    main()
