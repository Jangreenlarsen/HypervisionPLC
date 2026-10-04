"""
Start counters and test - Build #106
"""
import serial
import time
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

try:
    ser = serial.Serial('COM11', 115200, timeout=2)
    time.sleep(1)
    ser.reset_input_buffer()

    print("Starting all counters...")

    # Start Counter 1
    ser.write(b'show registers 140 1\n')
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Start Counter 2
    ser.write(b'show registers 141 1\n')
    time.sleep(0.3)
    ser.reset_input_buffer()

    # Start Counter 3
    ser.write(b'show registers 150 1\n')
    time.sleep(0.3)
    ser.reset_input_buffer()

    print("\nReading T=0...")
    ser.write(b'show registers 100 4\n')
    time.sleep(0.5)
    while ser.in_waiting > 0:
        print(ser.readline().decode('utf-8', errors='ignore').strip())

    print("\nWaiting 10 seconds...")
    time.sleep(10)

    print("\nReading T=10...")
    ser.write(b'show registers 100 4\n')
    time.sleep(0.5)
    while ser.in_waiting > 0:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        print(line)
        if "96:" in line or "104:" in line or "112:" in line:
            print(f"  >>> {line}")

    ser.write(b'show registers 105 4\n')
    time.sleep(0.5)
    print("\nCounter 2:")
    while ser.in_waiting > 0:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if "104:" in line:
            print(f"  >>> {line}")

    ser.close()
    print("\n=== TEST DONE ===")

except Exception as e:
    print(f"Error: {e}")
    import traceback
    traceback.print_exc()
