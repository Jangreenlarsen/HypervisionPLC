"""
Quick Counter Test - Build #105
Read counter values before and after 10 seconds
"""

import serial
import time
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

SERIAL_PORT = 'COM11'
BAUDRATE = 115200

def send_cmd(ser, cmd):
    ser.write((cmd + '\n').encode())
    time.sleep(0.5)
    output = ""
    while ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            output += line + "\n"
        except:
            break
    return output

def extract_value(output, reg_num):
    """Extract register value from show registers output"""
    lines = output.split('\n')
    for line in lines:
        if line.strip().startswith(f"{reg_num}:"):
            parts = line.split()
            if len(parts) >= 2:
                return int(parts[1])
    return None

try:
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=2)
    print("=" * 60)
    print("BUILD #105 QUICK TEST - PCNT_COUNT_DEC EXPERIMENT")
    print("=" * 60)
    print("Config: pos_mode=INC, neg_mode=DEC")
    print("Expected: Counter oscillates around start value")
    print("=" * 60)

    time.sleep(1)
    ser.reset_input_buffer()

    print("\n[T=0] Reading initial values...")
    out1 = send_cmd(ser, "show registers 100 4")
    out2 = send_cmd(ser, "show registers 105 4")

    c1_init = extract_value(out1, 100)
    c2_init = extract_value(out2, 105)

    print(f"  Counter 1 (reg 100): {c1_init}")
    print(f"  Counter 2 (reg 105): {c2_init}")

    print("\n[Waiting 10 seconds...]")
    time.sleep(10)

    print("\n[T=10] Reading final values...")
    out1 = send_cmd(ser, "show registers 100 4")
    out2 = send_cmd(ser, "show registers 105 4")

    c1_final = extract_value(out1, 100)
    c2_final = extract_value(out2, 105)

    print(f"  Counter 1 (reg 100): {c1_final}")
    print(f"  Counter 2 (reg 105): {c2_final}")

    print("\n" + "=" * 60)
    print("RESULTS:")
    print("=" * 60)
    c1_delta = c1_final - c1_init if (c1_init and c1_final) else None
    c2_delta = c2_final - c2_init if (c2_init and c2_final) else None

    if c1_delta is not None:
        print(f"Counter 1 delta: {c1_delta:+d} counts ({c1_delta/10:.1f} counts/sec)")
        if abs(c1_delta) < 100:
            print("  → ✅ OSCILLATING (pos_mode & neg_mode work!)")
        elif c1_delta > 0:
            print(f"  → ❌ STILL COUNTING UP (PCNT ignores neg_mode!)")
        else:
            print(f"  → ⚠️  COUNTING DOWN (unexpected)")

    if c2_delta is not None:
        print(f"\nCounter 2 delta: {c2_delta:+d} counts ({c2_delta/10:.1f} counts/sec)")
        if abs(c2_delta) < 100:
            print("  → ✅ OSCILLATING")
        elif c2_delta > 0:
            print(f"  → ❌ STILL COUNTING UP")

    ser.close()
    print("\n" + "=" * 60)

except Exception as e:
    print(f"ERROR: {e}")
    import traceback
    traceback.print_exc()
