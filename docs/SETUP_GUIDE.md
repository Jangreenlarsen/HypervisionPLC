# ESP32 Modbus RTU Server - Setup Guide

## Prerequisites

- **ESP32-DOIT-DEVKIT-V1** med USB kabel
- **PlatformIO** installeret (CLI eller VS Code extension)
- **Windows/Linux/macOS**

---

## Step 1: Clone Repository

```bash
git clone https://github.com/Jangreenlarsen/HypervisionPLC.git
cd Modbus_server_slave_ESP32
```

---

## Step 2: Check PlatformIO Version

```bash
pio --version
```

Expected: `PlatformIO Core, version 6.0+`

If not installed:
```bash
python -m pip install platformio --upgrade
```

---

## Step 3: Configure Serial Port

Edit `platformio.ini`:

```ini
monitor_port = COM10      # Change to your ESP32 COM port
upload_port = COM10       # Same as monitor_port
```

**Find your COM port:**
- Windows: Open "Device Manager" → "Ports (COM & LPT)" → Look for "USB" or "CH340" (UART chip)
- Linux: `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
- macOS: `ls /dev/cu.usb*`

---

## Step 4: Build Project

```bash
pio run
```

Expected output:
```
RAM:   [=         ]   6.5% (used 21400 bytes from 327680 bytes)
Flash: [==        ]  20.0% (used 262273 bytes from 1310720 bytes)
========================= [SUCCESS] Took 10.14 seconds =========================
```

If build fails, check:
- [ ] PlatformIO installed: `pio --version`
- [ ] Correct COM port in `platformio.ini`
- [ ] Internet connection (first build downloads toolchains)

---

## Step 5: Upload to ESP32 Hardware

**Connections:**
1. Connect ESP32 to computer via USB
2. Wait 2 seconds for USB driver to initialize
3. Check Device Manager to confirm COM port

**Upload firmware:**

```bash
pio run -t upload
```

Expected output:
```
Uploading .pio/build/esp32/firmware.bin
...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

**Troubleshooting:**
- If upload fails with "timeout", try:
  1. Hold BOOT button on ESP32 during upload
  2. Check USB cable is connected properly
  3. Try different USB port on computer

---

## Step 6: Monitor Serial Output

Open serial monitor:

```bash
pio device monitor
```

Expected output:
```
=== Modbus RTU Server (ESP32) ===
Version: 1.0.0
Setup complete.
```

Exit monitor: Press `Ctrl+]` (PlatformIO) or close terminal

---

## Useful Commands

```bash
# Full rebuild (clean + build)
pio run --target clean
pio run

# Upload without building
pio run -t upload

# Monitor with filtering (show only DEBUG lines)
pio device monitor --filter=log

# Show memory usage details
pio run --verbose

# Open IDE
pio home
```

---

## Project Structure

```
C:\Projekter\Modbus_server_slave_ESP32\
├── src/                    # Source files (35 .cpp files)
├── include/                # Header files (37 .h files)
├── docs/                   # Documentation
├── .pio/                   # Build artifacts (auto-generated)
├── platformio.ini          # Build configuration
├── CLAUDE.md               # Project architecture (DANISH)
└── .gitignore
```

---

## Quick Start Examples

After successful upload, test features via CLI:

### Test 1: Timer Mode 3 (Blink)
```bash
# Setup: 1 Hz blink (1000ms ON, 1000ms OFF)
set timer 1 mode 3 on-ms:1000 off-ms:1000 output-coil:200 enabled:1

# Check output
read coil 200 1  # Should be 1 (HIGH)
# Wait 1.1 seconds
read coil 200 1  # Should be 0 (LOW)
# Wait 1.1 seconds
read coil 200 1  # Should be 1 (HIGH) again
```

### Test 2: Timer Mode 4 (Input-Triggered) ✨ NEW
```bash
# Setup: Respond to rising edge (0→1)
set timer 2 mode 4 input-dis:30 trigger-edge:1 delay-ms:0 output-coil:250

# Simulate input
write coil 30 value 1    # Rising edge → Output goes HIGH
read coil 250 1          # Should be 1

write coil 30 value 0    # Falling edge (no trigger)
read coil 250 1          # Still 1

write coil 30 value 1    # Rising edge again → Trigger!
read coil 250 1          # Still 1 (stays set)
```

### Test 3: Save Configuration
```bash
# Save current timer/counter settings to NVS
save

# Verify save
show config

# After reboot - config will auto-load
reboot
```

### Test 4: Hardware Counter (requires GPIO signal)
```bash
# Setup: Count pulses on GPIO19 via PCNT
set counter 1 mode 3 hw-gpio:19 prescaler:1 bit-width:32 scale:1.0

# Start counter
set counter 1 control auto-start:on

# After 10 seconds with 1kHz signal:
read h-reg 10 5  # Register 10 = counter value

# Check frequency measurement
read h-reg 12 1  # Register 12 = Hz
```

---

## Next Steps

Once build & upload works:

1. **Read FEATURE_GUIDE.md**
   - Complete reference for all Timer Modes (1-4)
   - Counter modes and features
   - CLI commands (70+)
   - GPIO mapping and persistence

2. **Read CLAUDE.md**
   - Project architecture (8 layers)
   - Code modification guidelines
   - Development best practices

3. **Test on Hardware**
   - Try Quick Start Examples above
   - Verify Timer Mode 3 and Mode 4
   - Test persistence with `save`/`load`

---

## Common Issues

### Issue: "Unknown configuration option `include_dir`"

**Fix:** These are warnings only. Ignore them. Build will succeed.

### Issue: "Failed to open serial device"

**Solution:**
```bash
# List available ports
pio device list

# Use correct port in platformio.ini
monitor_port = COM4  # Update to your port
```

### Issue: "No module named 'platformio'"

**Solution:**
```bash
python -m pip install platformio --upgrade
pio --version
```

### Issue: Build takes forever (first time)

**Expected:** First build downloads ~500MB of toolchains and compilers. This can take 5-15 minutes.

Subsequent builds will be fast (~10 seconds).

---

## Hardware: ESP32-DOIT-DEVKIT-V1 Specs

- **Microcontroller:** ESP32 (Dual-core, 240 MHz)
- **RAM:** 320 KB (327 KB free)
- **Flash:** 4 MB (1.3 MB usable, 1.04 MB free)
- **USB:** CH340 UART chip
- **Voltage:** 3.3V logic (5V tolerant on inputs via protection)
- **GPIO:** 34 usable pins

---

## References

- [PlatformIO Docs](https://docs.platformio.org/)
- [ESP32 Arduino Core](https://github.com/espressif/arduino-esp32)
- [ESP32-DOIT-DEVKIT-V1 Pinout](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32_devkitc.html)

---

## Support

- Check CLAUDE.md for architecture documentation
- See docs/ESP32_Module_Architecture.md for module structure
- GitHub Issues: [Link to repo issues]

God dag! Happy coding! 🚀
