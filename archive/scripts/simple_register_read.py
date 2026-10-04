"""
Simple Register Read - Just show raw output
"""
import serial
import time
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

try:
    ser = serial.Serial('COM11', 115200, timeout=2)
    print("Reading Counter 1 and 2 (T=0)...")

    time.sleep(1)
    ser.reset_input_buffer()

    # Read initial
    ser.write(b'show registers 100 4\n')
    time.sleep(0.5)
    while ser.in_waiting > 0:
        print(ser.readline().decode('utf-8', errors='ignore').strip())

    print("\n" + "="*60)
    print("Waiting 10 seconds...")
    print("="*60 + "\n")
    time.sleep(10)

    print("Reading Counter 1 and 2 (T=10)...")
    ser.write(b'show registers 100 4\n')
    time.sleep(0.5)
    while ser.in_waiting > 0:
        print(ser.readline().decode('utf-8', errors='ignore').strip())

    ser.close()
except Exception as e:
    print(f"Error: {e}")
