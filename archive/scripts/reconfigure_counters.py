"""
ESP32 Counter Reconfiguration Script
Automatically configure counters with correct GPIO pins and save to NVS
"""

import serial
import time
import sys
import io

# Fix Windows console encoding for emojis
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

# Configuration
SERIAL_PORT = 'COM11'
BAUDRATE = 115200
TIMEOUT = 2

def send_command(ser, command, wait_time=0.5):
    """Send CLI command and wait for response"""
    print(f"\n>>> {command}")
    ser.write((command + '\n').encode())
    time.sleep(wait_time)

    # Read all available output
    output = ""
    while ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"    {line}")
                output += line + "\n"
        except:
            break

    return output

def main():
    print("=" * 80)
    print("ESP32 COUNTER RECONFIGURATION SCRIPT")
    print("=" * 80)
    print(f"Serial Port: {SERIAL_PORT}")
    print(f"Baudrate: {BAUDRATE}")
    print("=" * 80)

    try:
        # Open serial connection
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=TIMEOUT)
        print("✅ Serial connection opened")
        time.sleep(1)

        # Clear input buffer
        ser.reset_input_buffer()

        print("\n" + "=" * 80)
        print("STEP 1: Configure Counter 1 (HW mode, GPIO 19)")
        print("=" * 80)
        send_command(ser, "set counter 1 mode 1 hw-mode:hw edge:rising direction:up hw-gpio:19 prescaler:1 bit-width:32 index-reg:100 raw-reg:101 freq-reg:102 overload-reg:103 ctrl-reg:140", wait_time=1.0)

        print("\n" + "=" * 80)
        print("STEP 2: Configure Counter 2 (HW mode, GPIO 19)")
        print("=" * 80)
        send_command(ser, "set counter 2 mode 1 hw-mode:hw edge:rising direction:up hw-gpio:19 prescaler:1 bit-width:32 index-reg:105 raw-reg:106 freq-reg:107 overload-reg:108 ctrl-reg:141", wait_time=1.0)

        print("\n" + "=" * 80)
        print("STEP 3: Configure Counter 3 (SW-ISR mode, GPIO 13)")
        print("=" * 80)
        send_command(ser, "set counter 3 mode 1 hw-mode:sw-isr edge:rising direction:up interrupt-pin:13 prescaler:1 bit-width:32 index-reg:110 raw-reg:111 freq-reg:112 overload-reg:113 ctrl-reg:150", wait_time=1.0)

        print("\n" + "=" * 80)
        print("STEP 4: Save configuration to NVS")
        print("=" * 80)
        send_command(ser, "save config", wait_time=2.0)

        print("\n" + "=" * 80)
        print("STEP 5: Show current configuration")
        print("=" * 80)
        output = send_command(ser, "show config counters", wait_time=1.0)

        # Check if hw_gpio is configured
        if "hw_gpio" in output or "pin" in output:
            print("\n✅ Configuration appears successful")
        else:
            print("\n⚠️  Could not verify configuration in output")

        print("\n" + "=" * 80)
        print("STEP 6: Reboot ESP32 to load new config")
        print("=" * 80)
        print("Sending reset command...")
        ser.write(b'\x03')  # Send Ctrl+C
        time.sleep(0.5)

        # Close and reopen to trigger hardware reset
        ser.close()
        print("Closed serial port")
        time.sleep(2)

        # Reopen after reset
        print("Reopening serial port after reset...")
        ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=TIMEOUT)
        time.sleep(3)  # Wait for boot

        # Read boot messages
        print("\n--- BOOT OUTPUT ---")
        for _ in range(50):  # Read up to 50 lines of boot output
            if ser.in_waiting > 0:
                try:
                    line = ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(line)
                        # Look for debug output
                        if "DEBUG: Counter" in line and "hw_gpio" in line:
                            print(f"  ✅ FOUND: {line}")
                        if "WARNING: PCNT not configured" in line:
                            print(f"  ❌ WARNING: {line}")
                except:
                    break
            else:
                break

        print("\n" + "=" * 80)
        print("STEP 7: Verify counters after reboot")
        print("=" * 80)
        time.sleep(2)
        output = send_command(ser, "show config counters", wait_time=2.0)

        print("\n" + "=" * 80)
        print("STEP 8: Start Counter 1 for testing")
        print("=" * 80)
        send_command(ser, "show registers 140 1", wait_time=0.5)

        print("\nWaiting 5 seconds for counting...")
        time.sleep(5)

        print("\n" + "=" * 80)
        print("STEP 9: Read Counter 1 values")
        print("=" * 80)
        output = send_command(ser, "show registers 100 4", wait_time=1.0)

        # Parse register values
        print("\n📊 RESULTS:")
        if "100" in output:
            print("✅ Counter 1 registers readable")
        else:
            print("❌ Could not read counter 1 registers")

        ser.close()
        print("\n✅ Serial connection closed")

        print("\n" + "=" * 80)
        print("RECONFIGURATION COMPLETE!")
        print("=" * 80)
        print("\nNext steps:")
        print("1. Check boot output for 'DEBUG: Counter X hw_gpio = Y'")
        print("2. Verify NO warnings about 'PCNT not configured'")
        print("3. Start counters via Modbus ctrl registers")
        print("4. Monitor counting with 500 Hz signal")

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
