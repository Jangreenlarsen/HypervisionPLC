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
| GPIO35 | Ii3 | ADC1_CH7 | ✅ Input-only pin — bekræftet med sensor fysisk flyttet mellem klemmer (BUG-495, 2026-10-22); var tidligere fejlagtigt dokumenteret som GPIO36 |
| GPIO36 | Ii4 | ADC1_CH0 | ✅ Input-only pin — bekræftet med sensor fysisk flyttet mellem klemmer (BUG-495, 2026-10-22); var tidligere fejlagtigt dokumenteret som GPIO35 |

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
| GPIO4 | Ledig | Nej | Fri til brug (W5500 INT hvis Ethernet) |
| GPIO5 | Ledig | Ja (BOOT) | ⚠️ Intern pullup. Påvirker boot mode (W5500 CS hvis Ethernet) |
| GPIO18 | Ekstern watchdog WAKE | Nej | TPL5010 WAKE (FEAT-507) — W5500 SCK hvis Ethernet |
| GPIO19 | Ekstern watchdog DONE | Nej | TPL5010 DONE (FEAT-507) — W5500 MISO hvis Ethernet |

**Totalt:** 4 frie GPIOs (4, 5, 18, 19) — GPIO18/19 er nu reserveret til den eksterne watchdog. GPIO5 er strapping pin — brug med forsigtighed.

---

## Reset-supervisor + ekstern watchdog (beslutning 2026-10-26)

**Baggrund (BUG-506):** PLC'en starter ikke efter strømtab — hver gang — men starter ved tryk på RESET. ESP32 slipper ud af reset, før 3,3 V fra step-downen er stabil, og hænger i ROM-koden, før firmwaren kører. Ingen intern watchdog (task-, interrupt- eller RTC-watchdog) kan hjælpe dér.

**Beslutning:**
- **W5500 Ethernet droppes indtil videre** — netværk kører på WiFi. Det frigør GPIO18/19 (og AO1/AO2 virker igen).
- **APX803 eller TPS3839** (spændingsovervåger, ca. 2,9–3,0 V tærskel) på **EN**: holder ESP32 i reset, til 3,3 V er stabil. Fjerner rodårsagen til BUG-506.
- **TPL5010** (ekstern watchdog/timer) som sidste sikkerhedsnet: aktiv fra power-on, uafhængig af firmwaren.

**TPL5010-tilslutning:**

| TPL5010 | Til | Note |
|---|---|---|
| VDD / GND | 3,3 V / GND | |
| DELAY/M_RST | modstand til GND | Interval **ca. 60 s** — modstandsværdi fra databladets tabel |
| WAKE | **GPIO18** (input) | Puls hvert interval |
| DONE | **GPIO19** (output) | Firmwaren kvitterer for hver WAKE |
| RSTn | **EN** | Reset ved manglende DONE |

**Funktion:** TPL5010 sender WAKE hvert ~60 s. Firmwaren svarer med DONE — men **kun fra hovedløkken** (`watchdog_feed()`), aldrig fra opstarten. Udebliver DONE før næste WAKE, trækker den EN lav = fuld hardware-reset. Effektiv timeout er dermed **ca. 60–120 s**: lang nok til opstart og firmware-opgradering (TPL5010 nulstilles ikke af en software-genstart), men aktiv fra første sekund — også hvis ESP32 aldrig når sin egen kode.

**EN-ledningsføring:** supervisorens og TPL5010's RESET-udgange samt RESET-knappen deler EN (wired-OR). Tjek i databladene om udgangene er open-drain. Er en af dem push-pull, så forbind den via en Schottky-diode (katode mod udgangen), så den kun kan trække EN lav og ikke kæmper mod knappen eller den anden kreds. Behold pull-up'en og kondensatoren på EN.

**Firmware:** `set watchdog ext on` + `save` + genstart aktiverer DONE-kvitteringen (FEAT-507). **Monteres TPL5010 uden at slå den til, resetter den PLC'en hvert ~2. minut.** Kan ikke bruges samtidig med W5500 (firmwaren afviser kombinationen). Test: `test crash ext yes` — `show watchdog` viser bagefter om testen bestod.

**Watchdog-lag samlet:**

| Lag | Timeout | Fanger |
|---|---|---|
| Task-watchdog (intern) | 30 s | hovedløkken hænger — med crash-log |
| RTC-watchdog (intern, BUG-504) | 60 s | hårde låsninger efter firmwaren er startet |
| TPL5010 (ekstern, FEAT-507) | ~60–120 s | alt andet, også hængning før firmwaren starter |
| APX803/TPS3839 (supervisor) | — | holder ESP32 i reset under ustabil forsyning |

> ⚠️ **GPIO16 og GPIO17 er IKKE ledige** på dette board: ESP32-WROVER-modulet bruger dem internt til sin PSRAM. Ældre udgaver af denne guide angav dem som frie og brugte dem til W5500 RST/MOSI — det var rodårsagen til Ethernet-crashene (BUG-423). Firmwaren afviser dem nu i GPIO-mappings (BUG-428).

---

## W5500 Ethernet Tilslutning

### Pin-Mapping (ledige GPIOs)

Fra v7.9.68.33 (BUG-423b) tilsluttes W5500 via de 4 ledige GPIOs **plus de to DAC-udgange** (AO1/AO2 — de eksterne analog-udgangskredsløb skal frakobles):

| W5500 Pin | ESP32 GPIO | Funktion | Note |
|-----------|-----------|----------|------|
| MISO | GPIO19 | SPI MISO (remapped) | Ledig |
| MOSI | GPIO25 | SPI MOSI (remapped) | = AO1/DAC1 |
| SCK | GPIO18 | SPI CLK (remapped) | Ledig |
| CS (SCS) | GPIO5 | Chip Select | Strapping pin — intern pullup holder CS inaktiv ved boot |
| INT | GPIO4 | Interrupt | Ledig |
| RST | GPIO26 | Hardware Reset | = AO2/DAC2 |

AO1/AO2 er slået fra i firmwaren, mens Ethernet er aktiveret (`analog_driver_flush_outputs()` skriver ikke til DAC'en).
| VCC | — | 3.3V | 3V3 |
| GND | — | Ground | GND |

### Tilslutnings-diagram

```
    ESP32 (38-pin)              W5500 Module
    +-----------+               +----------+
    |           |               |          |
    | GPIO18 ---|---SCK-------->| SCK      |
    | GPIO19 ---|---MISO-----<--| MISO     |
    | GPIO25 ---|---MOSI------->| MOSI     |
    | GPIO5  ---|---CS--------->| SCS      |
    | GPIO4  ---|---INT------<--| INT      |
    | GPIO26 ---|---RST-------->| RST      |
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

6. **SPI Host:** HSPI (SPI2_HOST, se `W5500_SPI_HOST` i constants.h — skiftet fra VSPI under BUG-423). Alle W5500-ben er remappede via ESP32 GPIO-matrixen; MOSI ligger på GPIO25 (tidl. GPIO17 = PSRAM).

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
