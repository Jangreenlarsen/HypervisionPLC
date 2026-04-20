# Eletechsup ES32D26 — Board Guide & W5500 Tilslutning

**Board:** Eletechsup ES32D26 (2AO-8AI-8DI-8DO)
**ESP32 Modul:** ESP32-WROVER (ESP32-D0WD-V3, dual-core 240MHz, **4 MB PSRAM** onboard)
**Form factor:** 38-pin ESP32-WROVER DevKit i IO board slot (pin-kompatibel med WROOM-32)
**Forsyning:** DC 12V eller DC 24V
**Mål:** 180 x 72 x 19 mm
**Pin-verificering:** Shift register pins bekræftet med multimeter 2026-03-22
**PSRAM:** Aktiveret via `-DBOARD_HAS_PSRAM` i `platformio.ini` (board = `esp-wrover-kit`).
Verificér i runtime med `show version` eller `/api/metrics` (`esp32_psram_total_bytes`).

---

## Onboard Ressourcer

| Ressource | Antal | Type |
|-----------|-------|------|
| Relæ-udgange | 8 (8DO) | Via SN74HC595 shift registers |
| Digitale inputs | 8 (8DI) | Opto-isoleret, NPN, low-level trigger |
| Spændingsinput | 4 (Vi1-Vi4) | 0-5V / 0-10V |
| Strøminput | 4 (Ii1-Ii4) | 0/4-20mA |
| Analog output | 2 (2AO) | 0-5V/10V ELLER 0/4-20mA (vælg via SW1) |
| RS485 | 1 | Onboard transceiver (GPIO1/3, DIR=GPIO21) |

**OBS:** ES32D26 har **ingen knapper** (modsat ES32A08 som har KEY1-KEY4).
**OBS:** Ingen ledig LED — GPIO2 og GPIO15 er optaget af 74HC165 shift register.

---

## Komplet GPIO Pin-Mapping

### Shift Registers — Relæ-udgange (SN74HC595) — VERIFIED med multimeter

| ESP32 GPIO | 74HC595 Pin | Funktion | Signal |
|------------|-------------|----------|--------|
| GPIO12 | Pin 14 | Serial Data | SER / DS |
| GPIO22 | Pin 11 | Shift Clock | SRCLK / SHCP |
| GPIO23 | Pin 12 | Latch (Storage Clock) | RCLK / STCP |
| GPIO13 | Pin 13 | Output Enable | OE (active LOW) |

Styrer 8 relæer via kaskadede 74HC595 chips.

### Shift Registers — Digitale Inputs (SN74HC165) — VERIFIED med multimeter

| ESP32 GPIO | 74HC165 Pin | Funktion | Signal | Note |
|------------|-------------|----------|--------|------|
| GPIO0 | Pin 1 | Parallel Load | SH/LD (active LOW) | ⚠️ Strapping pin (BOOT) |
| GPIO2 | Pin 2 | Clock | CLK | ⚠️ Strapping pin |
| GPIO15 | Pin 9 | Serial Data IN | QH | ⚠️ Strapping pin |

Læser 8 opto-isolerede digitale inputs.

### RS485 (Onboard Transceiver)

| ESP32 GPIO | Funktion | Note |
|------------|----------|------|
| GPIO1 | TX | UART0 TX (delt med USB debug) |
| GPIO3 | RX | UART0 RX (delt med USB debug) |
| GPIO21 | DIR (DE/RE) | Retningsstyring |

**OBS:** RS485 og USB debug serial deler UART0. Kun én kan være aktiv ad gangen.

### Analog Inputs — Spænding (0-5V / 0-10V)

| ESP32 GPIO | ES32D26 Kanal | ADC Kanal | Note |
|------------|--------------|-----------|------|
| GPIO14 | Vi1 | ADC2_CH6 | ⚠️ IKKE tilgængelig med WiFi aktiv |
| GPIO33 | Vi2 | ADC1_CH5 | ✅ OK med WiFi |
| GPIO27 | Vi3 | ADC2_CH7 | ⚠️ IKKE tilgængelig med WiFi aktiv |
| GPIO32 | Vi4 | ADC1_CH4 | ✅ OK med WiFi |

### Analog Inputs — Strøm (0/4-20mA)

| ESP32 GPIO | ES32D26 Kanal | ADC Kanal | Note |
|------------|--------------|-----------|------|
| GPIO34 | Ii1 | ADC1_CH6 | ✅ Input-only pin |
| GPIO39 | Ii2 | ADC1_CH3 | ✅ Input-only pin |
| GPIO36 | Ii3 | ADC1_CH0 | ✅ Input-only pin |
| GPIO35 | Ii4 | ADC1_CH7 | ✅ Input-only pin |

### Analog Outputs (DAC)

| ESP32 GPIO | ES32D26 Kanal | Funktion |
|------------|--------------|----------|
| GPIO25 | AO1 | Vo1/Io1 (DAC1) |
| GPIO26 | AO2 | Vo2/Io2 (DAC2) |

Valg mellem spænding (0-5V/10V) og strøm (0/4-20mA) output via onboard SW1 switch.

### Strøm

| ESP32 Pin | Funktion |
|-----------|----------|
| VIN | 5V fra boardets regulator |
| 3V3 | 3.3V fra boardets regulator |
| GND | Fælles GND |
| EN | Enable (intern pullup) |

---

## Ledige GPIOs

Disse GPIOs er **ikke brugt** af ES32D26 boardet:

| ESP32 GPIO | Status | Strapping | Anbefaling |
|------------|--------|-----------|------------|
| GPIO4 | Ledig | Nej | Fri til brug |
| GPIO5 | Ledig | Ja (BOOT) | ⚠️ Intern pullup. Påvirker boot mode |
| GPIO16 | Ledig | Nej | Fri til brug |
| GPIO17 | Ledig | Nej | Fri til brug |
| GPIO18 | Ledig | Nej | Fri til brug (VSPI CLK default) |
| GPIO19 | Ledig | Nej | Fri til brug (VSPI MISO default) |

**Totalt:** 6 frie GPIOs (4, 5, 16, 17, 18, 19). GPIO5 er strapping pin — brug med forsigtighed.

---

## W5500 Ethernet Tilslutning

### Pin-Mapping (ledige GPIOs)

W5500 modulet tilsluttes via de 6 ledige GPIOs:

| W5500 Pin | ESP32 GPIO | Funktion | Note |
|-----------|-----------|----------|------|
| MISO | GPIO19 | VSPI MISO | Ledig |
| MOSI | GPIO17 | SPI MOSI (remapped) | Ledig |
| SCK | GPIO18 | VSPI CLK | Ledig |
| CS (SCS) | GPIO5 | Chip Select | Strapping pin — intern pullup holder CS inaktiv ved boot |
| INT | GPIO4 | Interrupt | Ledig |
| RST | GPIO16 | Hardware Reset | Ledig |
| VCC | — | 3.3V | 3V3 |
| GND | — | Ground | GND |

### Tilslutnings-diagram

```
    ESP32 (38-pin)              W5500 Module
    +-----------+               +----------+
    |           |               |          |
    | GPIO18 ---|---SCK-------->| SCK      |
    | GPIO19 ---|---MISO-----<--| MISO     |
    | GPIO17 ---|---MOSI------->| MOSI     |
    | GPIO5  ---|---CS--------->| SCS      |
    | GPIO4  ---|---INT------<--| INT      |
    | GPIO16 ---|---RST-------->| RST      |
    |           |               |          |
    | 3V3  ----|---VCC-------->| VCC      |
    | GND  ----|---GND-------->| GND      |
    +-----------+               +----------+
```

### Hardware-noter

1. **GPIO5 (CS):** Strapping pin — har intern pullup til 3.3V. W5500 CS er active LOW, så pullup holder CS inaktiv ved boot. Sikker at bruge.

2. **GPIO0 (74HC165 SH/LD):** Strapping pin — brugt som parallel load for shift register. Tilføj ekstern 10K pullup til 3.3V for at sikre HIGH ved boot (normal boot mode). SH/LD idle HIGH er kompatibelt.

3. **GPIO2 (74HC165 CLK):** Strapping pin — brugt af shift register. Skal være LOW ved boot for korrekt flash boot.

4. **GPIO12 (74HC595 DATA):** Strapping pin — brugt som SER data til shift register. Skal være LOW ved boot (ellers sættes flash til 1.8V). 74HC595 SER input har ingen pull, så det er OK.

5. **SPI Clock:** 8 MHz (konservativ for eksternt modul med ledninger). Kan øges til 20 MHz med korte, gode forbindelser.

6. **SPI Host:** VSPI (SPI3_HOST). MOSI er remapped fra GPIO23 (optaget af 74HC595) til GPIO17 via ESP32 GPIO matrix.

---

## Build Konfiguration

### PlatformIO Environment

Brug det dedikerede `[env:es32d26]` environment i `platformio.ini`:

```bash
# Build til ES32D26
pio run -e es32d26

# Upload til ES32D26
pio run -e es32d26 -t upload

# Monitor seriel output
pio device monitor -e es32d26
```

### Build Flags

ES32D26 environmentet sætter automatisk:
- `-DBOARD_ES32D26` — vælger korrekt pin-mapping i `constants.h`
- `-DETHERNET_W5500_ENABLED` — aktiverer W5500 Ethernet driver

### Første Upload til Nyt Board

1. Tilslut ESP32 via USB
2. `pio run -e es32d26 -t upload`
3. SPIFFS formateres automatisk ved første boot (kan tage ~5 sek)
4. Konfigurér Ethernet via CLI: `set ethernet enable`

---

## Begrænsninger på ES32D26

| Feature | Status | Årsag |
|---------|--------|-------|
| HW Counter (ISR mode) | Ikke tilgængelig | Ingen ledige interrupt-egnede pins |
| SW Counter (polling) | Tilgængelig | Bruger register-baseret polling |
| I2C | Ikke tilgængelig | Ingen ledige I2C-egnede pins |
| USB Debug + RS485 | Kun én ad gangen | Deler UART0 (GPIO1/3) |
| ADC2 (Vi1, Vi3) | Ikke med WiFi | ADC2 blokeres af WiFi driver |
| LED heartbeat | Ikke tilgængelig | GPIO2 optaget af 74HC165 CLK |
| Timers | Fuld support | Software-baseret |
| ST Logic | Fuld support | Software-baseret |
| Modbus RTU | Fuld support | Via onboard RS485 |
| Ethernet (W5500) | Fuld support | Via VSPI, se tilslutning ovenfor |
| WiFi | Fuld support | Onboard ESP32 WiFi |
| Analog Output | Fuld support | 2x DAC (GPIO25, GPIO26) |

---

## Forskelle: ES32D26 vs ES32A08

| Feature | ES32D26 | ES32A08 |
|---------|---------|---------|
| Analog output (AO) | 2 stk (DAC) | Ingen |
| Knapper (KEY1-4) | Ingen | 4 stk (GPIO18,19,21,23) |
| 7-segment display | Ingen | 4-digit tube |
| LED | Ingen ledig | GPIO2 |
| 74HC595 pins | GPIO12(D),22(CK),23(LA),13(OE) | GPIO13(D),27(CK),14(LA),4(OE) |
| 74HC165 pins | GPIO0,2,15 | GPIO5,16,17 |
| Ledige GPIOs | 4,5,16,17,18,19 | 0,2,12,15 |
| W5500 kompatibel | Ja (6 frie GPIOs) | Begrænset (KEY konflikter) |

---

## Kilder

- [ES32D26 Product Page — Eletechsup](https://eletechsup.com/products/es32d26-2ao-8ai-8di-8do-esp32-wifi-network-relay-board-4-20ma-0-10v-digital-analog-input-output-module-for-smart-switch-iot-simple-plc)
- [ES32D26 FAQ — Eletechsup](https://eletechsup.com/blogs/news/es32d26-faq-1)
- [ESPHome ES32A08 Example (bekræftet pin-arkitektur)](https://github.com/makstech/esphome-es32a08-expansion-board-example)
- [Eletechsup Manual/Datasheet List](https://eletechsup.com/blogs/news/eletechsup-sku-list)

---

**Sidst opdateret:** 2026-03-22 (shift register pins verificeret med multimeter)
**Board definition:** `BOARD_ES32D26` i `include/constants.h`
**Build environment:** `[env:es32d26]` i `platformio.ini`
