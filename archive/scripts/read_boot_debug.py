"""
ESP32 Boot Debug Output Reader
Read boot messages to see PCNT edge configuration
"""

import serial
import time
import sys
import io

# Fix Windows console encoding
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

SERIAL_PORT = 'COM11'
BAUDRATE = 115200
TIMEOUT = 2

def main():
    print("=" * 80)
    print("ESP32 BOOT DEBUG OUTPUT READER - Build #104")
    print("=" * 80)
    print("Looking for PCNT edge configuration debug output...")
    print("=" * 80)

    try:
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=TIMEOUT)
        print("\n✅ Serial connection opened")
        print("Reading boot output for 10 seconds...\n")

        start_time = time.time()
        boot_lines = []

        while time.time() - start_time < 10:
            if ser.in_waiting > 0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        boot_lines.append(line)
                        print(line)

                        # Highlight important lines
                        if "PCNT" in line or "Edge" in line or "COUNT" in line:
                            print(f"  ⚠️  IMPORTANT: {line}")
                except:
                    pass

        ser.close()
        print("\n" + "=" * 80)
        print(f"Captured {len(boot_lines)} lines of boot output")
        print("=" * 80)

        # Filter and show only PCNT related lines
        print("\n📊 PCNT RELATED LINES:")
        pcnt_lines = [l for l in boot_lines if "PCNT" in l or "Unit" in l or "GPIO" in l]
        for line in pcnt_lines:
            print(f"  {line}")

    except serial.SerialException as e:
        print(f"\n❌ ERROR: Could not open serial port {SERIAL_PORT}")
        print(f"   {e}")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ ERROR: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
