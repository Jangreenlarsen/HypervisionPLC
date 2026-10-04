# Analyse: Migrering fra Arduino Mega 2560 til ESP32

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


## Executive Summary

Migrering til ESP32 ville være en **major undertaking**, men med **signifikante fordele**. Det ville være en fuldstændig rebuild af hardware-lag og delvis refaktor af software, men kunne åbne op for moderne features som Wi-Fi, WebUI, og multi-tasking.

---

## 1. Sammenligninger: Mega 2560 vs ESP32

| Aspect | Mega 2560 | ESP32 | Fordel |
|--------|-----------|-------|--------|
| **CPU** | 8-bit AVR 16MHz | 32-bit Dual-core 240MHz | ESP32 (30× hurtigere) |
| **RAM** | 8 KB | 520 KB | ESP32 (65× mere) |
| **Flash** | 256 KB | 4 MB (typical) | ESP32 (16× mere) |
| **GPIO** | 54 pins | 34 pins (usable) | Mega (mere I/O) |
| **Timers** | 6 (Timer0-5) | 4 (Timer0-3) | Mega (mere timer hardware) |
| **UART** | 4 (0,1,2,3) | 3 (0,1,2) | Mega (1 ekstra) |
| **SPI** | 1 | 4 | ESP32 (mer redundans) |
| **ADC** | 16 kanaler 10-bit | 18 kanaler 12-bit | ESP32 |
| **Wi-Fi** | Ingen | 802.11 b/g/n | ESP32 (helt nyt) |
| **Bluetooth** | Ingen | BLE + Classic | ESP32 (helt nyt) |
| **Strøm** | ~100mA | ~80-160 mA | Mega (lavere under load) |
| **Pris** | ~$60 | ~$10-20 | ESP32 (3× billigere) |
| **Udvikling** | Finished product | Aktiv udvikling | ESP32 |

## 2. Hardware-migrering

### 2.1 Nuværende Hardware-setup (Mega 2560)

```
Pin 2   → Timer5 T5 input (Counter HW)
Pin 3   → INT1 (Counter SW-ISR)
Pin 8   → RS-485 DIR (Modbus)
Pin 18  → UART1 RX (Modbus)
Pin 19  → UART1 TX (Modbus)
Pins 20,21,47 → INT2-5, Timer5 (counters)
Pin 50-53 → SPI (future W5500)
USB → Serial monitor (UART0, 115200)
```

### 2.2 ESP32 Pin-mapping (kritisk!)

**Usable GPIO pins: GPIO0-19, GPIO21-23, GPIO25-27, GPIO32-39** (34 total, men nogle er strapping pins)

**Problemer:**
1. **Færre GPIO** - Mega har 54, ESP32 har ~34 usable
   - Løsning: I/O expander (PCF8574) over I2C for ekstra diskrete outputs

2. **Timer5 ekstern clock ikke tilgængelig**
   - ESP32 har helt anden timer-arkitektur (LEDC PWM, Timer Group0-1)
   - HW counter mode ville kræve PCNT (Pulse Counter) modul i stedet
   - **Fordel:** PCNT kan tælle uden ISR, mere robust end Timer5

3. **Strappings pins** (kan påvirke boot):
   - GPIO0, GPIO2, GPIO15 skal være forsigtige
   - GPIO12 forbundet til chip-intern SPD kontrolregister

**Pin Assignment Proposal:**

```
ESP32 Pin  | Signum | Nuværende | Noter
-----------|--------|-----------|--------
GPIO4      | TX_RS  | Pin 19    | UART1 TX
GPIO5      | RX_RS  | Pin 18    | UART1 RX
GPIO15     | DIR_RS | Pin 8     | RS-485 DIR (strapping pin!)
GPIO16     | INT_1  | Pin 3     | Interrupt (kan på ESP32)
GPIO17     | INT_2  | Pin 20    | Interrupt
GPIO18     | INT_3  | Pin 21    | Interrupt
GPIO19     | PCNT   | Pin 2     | Pulse Counter (HW counter)
GPIO23     | SPI_CS | N/A       | For future W5500 over SPI
GPIO21     | SDA    | N/A       | I2C til I/O expander
GPIO22     | SCL    | N/A       | I2C til I/O expander
USB        | UART0  | USB       | Allerede til debug/CLI (115200)
```

**Potential issue:** GPIO15 er strapping pin. Skal måske bruge GPIO0 i stedet (med extern pull-up).

### 2.3 RS-485 hardware

**Nuværende:** MAX485 modul på TX/RX/DIR pins med 5V strømforsyning

**ESP32:** 3.3V logik → Kræver level shifter (eller 5V-tolerant MAX485 variant)

**Ændringer:**
- Samme MAX485 modul kan bruges (er 5V-tolerant på input)
- GPIO15 (eller GPIO0) for DIR kontrolregister
- UART1 (GPIO4/5) for Modbus RX/TX

### 2.4 Timere og Counter-hardware

**Mega 2560 Timer5 arkitektur:**
- 16-bit counter med ekstern clock input (pin 2, T5)
- ISR ved overflow
- Hardware prescaler (IKKE funktionel for ekstern clock)
- Software prescaler implementeret

**ESP32 PCNT (Pulse Counter) arkitektur:**
- Hardware tæller på PCNT units (4 units available)
- Edge detection (rising/falling/both)
- Kan konfigurere input/control pins
- Interrupt on threshold (overflow)
- **Meget mere powerful end Timer5!**

**Fordele ved ESP32 PCNT:**
✓ Hardware prescaler VIRKER (ikke buggy som Timer5)
✓ Kan køre uden ISR (polling med mcpwm_capture_timer_get_value)
✓ 32-bit counter (ikke overflow)
✓ Flere units = flere simultane HW counters

**Migrering af counter-modes:**
- **SW (polling):** Samme GPIO polling via esp_gpio_get_level()
- **SW-ISR:** Samme interrupt approch (GPIO INT0-5 → ESP32 GPIO interrupt)
- **HW (Timer5):** PCNT unit (meget bedre!)

**Konklusiv:** Counters bliver BEDRE på ESP32, ikke værre!

---

## 3. Software-migrering strategi

### 3.1 Arkitektur-ændringer

**Mega 2560 (Arduino):**
```
Arduino IDE / PlatformIO
↓
Arduino HAL (digitalWrite, digitalRead, etc.)
↓
AVR registers (TCCR5B, PORTB, etc.)
↓
ATmega2560 silicon
```

**ESP32 (IDF/Arduino-ESP32):**
```
Arduino IDE / PlatformIO / ESP-IDF
↓
Arduino-ESP32 HAL OR ESP-IDF
↓
ESP-IDF driver API (gpio, uart, ledc, pcnt)
↓
ESP32 silicon
```

**Anbefaling:** Brug **Arduino-ESP32 core** for maksimal backward compatibility

### 3.2 Kode-port status

| Modul | Mega Code | ESP32 Port | Indsats |
|-------|-----------|-----------|---------|
| **main.cpp** | ✓ Arduino main() | ✓ Arduino setup()/loop() | Minimal |
| **modbus_core.cpp** | ✓ Serial I/O | ✓ Serial1/HardwareSerial | Minimal (pinMode↔pinmuxing) |
| **modbus_fc.cpp** | ✓ Register logic | ✓ Samme | Ingen ændring |
| **modbus_tx.cpp** | ✓ RS-485 DIR (digitalWrite) | ✓ digitalWriteFast() | Minimal |
| **modbus_timers.cpp** | ✓ millis() timer | ✓ millis() timer | Ingen ændring |
| **modbus_counters.cpp** | ✓ SW polling | ✓ GPIO level read | Minimal |
| **modbus_counters_sw_int.cpp** | ✓ INT0-5 ISR | ✓ GPIO interrupt | **Medium** (ISR signature ændring) |
| **modbus_counters_hw.cpp** | ✗ Timer5 eksternal clock | ✓ PCNT unit | **Major** (helt refaktor) |
| **config_store.cpp** | ✓ EEPROM access | ✓ NVS (partitions) eller EEPROM emulation | **Medium** |
| **cli_shell.cpp** | ✓ Serial CLI | ✓ Samme Serial1 | Minimal |

**Estimat:** 40% kode skal omskrives (hovedsageligt HW counter og config store)

### 3.3 Vigtigste porting-elementer

#### 1. UART Setup
```cpp
// Mega
Serial1.begin(115200, SERIAL_8N1);  // UART1 pins 18,19

// ESP32
Serial1.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);  // GPIO5, GPIO4
```

#### 2. GPIO Interrupt
```cpp
// Mega
attachInterrupt(digitalPinToInterrupt(PIN), isr, RISING);

// ESP32 (samme API!)
attachInterrupt(digitalPinToInterrupt(PIN), isr, RISING);
```

#### 3. PCNT Counter (NEW)
```cpp
// Mega - Timer5 ISR
ISR(TIMER5_OVF_vect) { ... }

// ESP32 - PCNT
pcnt_unit_t unit = PCNT_UNIT_0;
pcnt_config_t config = {
  .pulse_gpio_num = GPIO_NUM_19,
  .ctrl_gpio_num = -1,
  .channel = PCNT_CHANNEL_0,
  .unit = PCNT_UNIT_0,
  .pos_mode = PCNT_COUNT_INC,
  .neg_mode = PCNT_COUNT_DIS,
  .counter_h_lim = 32767,
  .counter_l_lim = 0,
  .flags = {.reverse = 0}
};
pcnt_unit_config(&config);
```

#### 4. EEPROM → NVS
```cpp
// Mega - direct EEPROM
EEPROM.write(addr, value);

// ESP32 - options:
// A) EEPROM emulation library (Drop-in replacement)
// B) NVS (more robust, flash-aware)
// C) LittleFS (modern file system)
```

#### 5. RS-485 DIR Control
```cpp
// Mega
digitalWrite(PIN_8, state);  // Direct pin control

// ESP32
digitalWrite(GPIO15, state);  // Samme (men GPIO15 er strapping pin!)
```

---

## 4. Nye muligheder på ESP32

### 4.1 Built-in networking
- **Wi-Fi SoftAP:** Device som access point
- **Wi-Fi Client:** Connect til eksisterende netværk
- **Modbus TCP:** Native over Wi-Fi (ingen W5500 nødvendig!)
- **WebUI:** Browser-baseret dashboard
- **MQTT:** IoT integrering

### 4.2 Dual-core arkitektur
- **Core 0:** RTOS-kernel, BT, Wi-Fi
- **Core 1:** Application kode (Modbus, timers, counters)
- Kan køre CLI og Modbus uden blocking

### 4.3 Sleep modes
- Deep sleep (5µA) med RTC timer
- Lyset power consumption ved low-frequency operation

### 4.4 Flash storage
- 4 MB vs 256 KB (16× mere)
- OTA (Over-The-Air) firmware updates
- Partition table for firmware + data

### 4.5 ADC/DAC
- 12-bit ADC (vs 10-bit på Mega)
- Analog output (DAC) - Mega har ingen

---

## 5. Udfordringer og risici

### 5.1 GPIO mangel
- Mega har 54, ESP32 har 34 usable
- **Løsning:** I2C I/O expander (PCF8574) for diskrete I/O
- **Omkostning:** +$2, +1 I2C bus slot

### 5.2 Timer-arkitektur helt anderledes
- Mega: 6 unidependente 8/16-bit timers
- ESP32: 2 Timer Groups (16-bit), 1 RTC timer, 1 systick
- **Mega fordel:** Flere timers til simultane operationer
- **ESP32 fordel:** PCNT units meget bedre for counters

### 5.3 Interrupt handling
- INT0-INT5 på Mega → GPIO interrupt på ESP32
- Inte alle pins kan være interrupt pins på ESP32
- SW-ISR counter mode skal reposuples

### 5.4 Strommangel på nogle pins
- GPIO12, GPIO15 strapping pins (påvirker boot)
- GPIO34-39 er input-only (kan ikke være outputs)
- Måske omorganisering af pin-assignment nødvendig

### 5.5 Stability / Maturity
- Arduino-ESP32 core er stable, men mindre testet end AVR
- Wi-Fi kan skabe EMI issues
- Watchdog timer skal håndteres anderledes (har TWDT)

### 5.6 Debug
- Mega: AVRISP debugger eller seriel
- ESP32: JTAG via USB-C (mere kompleks setup)

---

## 6. Implementeringsfaser

### Fase 1: Prototype Setup (1-2 uger)
- [ ] Procure ESP32 devboard + breadboard
- [ ] PlatformIO project setup
- [ ] Blink test, UART test, GPIO interrupt test
- [ ] Baseline: no Modbus, just LED/serial

### Fase 2: Core Port (2-3 uger)
- [ ] Port UART1 Modbus til GPIO4/5
- [ ] Port modbus_core.cpp, modbus_fc.cpp (no changes!)
- [ ] Port RS-485 DIR control (GPIO15 eller GPIO0)
- [ ] Test Modbus RTU over serial
- [ ] **Milestone:** RTU slave works identically to Mega

### Fase 3: SW Counter Port (1 uge)
- [ ] Port SW polling counter (GPIO level read)
- [ ] Port SW-ISR counter (GPIO interrupt)
- [ ] Test both counter modes with signal generator
- [ ] **Milestone:** All counter data matches Mega

### Fase 4: HW Counter Port (2-3 uger)
- [ ] Replace Timer5 med PCNT unit
- [ ] Implement prescaler via software (som nu)
- [ ] Frequency measurement (samme logic)
- [ ] Intensive testing med high-speed pulses
- [ ] **Milestone:** HW counter counts accurately at 20 kHz

### Fase 5: Config & Storage (1-2 uger)
- [ ] Migrate EEPROM → NVS (eller EEPROM lib)
- [ ] Port config_store.cpp
- [ ] Test persistence after reboot
- [ ] **Milestone:** config save/load works

### Fase 6: CLI & Integration (1 uge)
- [ ] Port CLI commands (Serial1 → GPIO4/5)
- [ ] Test all CLI commands
- [ ] **Milestone:** Full CLI parity with Mega

### Fase 7: Optional: Wi-Fi & Modbus TCP (2-4 uger)
- [ ] Add Wi-Fi provisioning
- [ ] Implement Modbus TCP server
- [ ] WebUI dashboard
- [ ] OTA firmware update
- [ ] **Milestone:** Device reachable over Wi-Fi

**Total estimat: 10-16 uger til full parity + Wi-Fi**

---

## 7. Cost Analysis

| Item | Mega Setup | ESP32 Setup | Delta |
|------|-----------|------------|-------|
| Board | $60 | $15 | -$45 |
| RS-485 Module | $5 | $5 | $0 |
| Level Shifter | $0 | $2 | +$2 |
| I/O Expander (opt) | $0 | $2 | +$2 |
| Antennaes/Housing | $0 | $3 | +$3 |
| **Total** | **$65** | **$27** | **-$38** |

**Besparelse: 60% billigere hardware**

---

## 8. Pros & Cons

### Fordele (Pros)
✅ 65× mere RAM (520 KB vs 8 KB) - no more buffer constraints
✅ 16× mere Flash (4 MB vs 256 KB) - room for features
✅ 30× hurtigere CPU - can handle Wi-Fi stack without lag
✅ Built-in Wi-Fi + BLE - no external module nødvendig
✅ PCNT hardware counters - better than Timer5
✅ 60% billigere hardware
✅ Active community og biblioteker
✅ Modern development tools (VS Code + PlatformIO trivial)
✅ OTA firmware updates

### Ulemper (Cons)
❌ færre GPIO pins (34 vs 54)
❌ Timer5 replacement kræver PCNT refaktor
❌ Strapping pins kan gøre debug svagere
❌ More power consumption (~160 mA vs ~100 mA under load)
❌ SW-ISR counter ISR signature ændring
❌ EEPROM vs NVS paradigm shift
❌ Stability under Wi-Fi+Modbus load ukendt (skal testes)

---

## 9. Anbefaling & Decision Tree

### Hvis du skal vælge...

**BLIV med Mega 2560, hvis:**
- ✓ Projekt har 50+ GPIO behov
- ✓ Anelse om høj noise EMI miljø (Wi-Fi problematisk)
- ✓ Kritisk timing på timer-hardware
- ✓ Ikke behov for netværk eller moderne features

**MIGRÉR til ESP32, hvis:**
- ✓ Wi-Fi eller Bluetooth ønskes
- ✓ RAM/FlashConstrains er problem (de er det ikke i dag, men hvis udvidelse planlagt)
- ✓ WebUI/remote monitoring ønskes
- ✓ Pris er kritisk parameter
- ✓ Dual-core/RTOS multitasking skal bruges
- ✓ Villig til 10-16 uger porting indsats

---

## 10. Konkret næste trin

Hvis du interesseret i ESP32 migration:

1. **Proof of Concept (PCB):**
   - Køb ESP32 devboard (~$15)
   - Basic blink + UART test
   - Estimate porting tid for HW counters

2. **Risk Assessment:**
   - Test Wi-Fi stability under Modbus load
   - Measure power consumption
   - Validate PCNT counter accuracy at 20 kHz

3. **Business Case:**
   - Quantify value af Wi-Fi/WebUI features
   - Weigh 60% hardware cost saving mod porting cost (1-2 man-months)
   - Decide if jump-off point er worth it

---

**Konklusion:** ESP32 migration er **mulig og potentielt attraktiv**, men **kræver betydelig indsats**. Den vigtigste fordel er netværk-capability; hvis det ikke er behov, **bliv ved Mega 2560** som er proven i production.
