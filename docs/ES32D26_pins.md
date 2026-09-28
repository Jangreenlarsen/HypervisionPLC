# ES32D26 — ESP32 Pin Liste

**Board:** Eletechsup ES32D26 (2AO-8AI-8DI-8DO)
**Verificeret:** 2026-03-22 med multimeter (shift registers) + Eletechsup FAQ (RS485)

## Onboard I/O

| GPIO  | Funktion                                | Verificeret |
|-------|-----------------------------------------|-------------|
| IO1   | RS485 TX                                | FAQ         |
| IO3   | RS485 RX                                | FAQ         |
| IO21  | RS485 DIR (DE/RE)                       | FAQ         |
| IO12  | 74HC595 DATA  (relæ) — pin 14 SER      | Multimeter  |
| IO22  | 74HC595 CLK   (relæ) — pin 11 SRCLK    | Multimeter  |
| IO23  | 74HC595 LATCH (relæ) — pin 12 RCLK     | Multimeter  |
| IO13  | 74HC595 OE    (relæ) — pin 13 OE       | Multimeter  |
| IO0   | 74HC165 LOAD  (DI)   — pin 1 SH/LD     | Multimeter  |
| IO2   | 74HC165 CLK   (DI)   — pin 2 CLK       | Multimeter  |
| IO15  | 74HC165 DATA  (DI)   — pin 9 QH        | Multimeter  |
| IO14  | Vi1 — 0-10V AI  ⚠️ ADC2               |             |
| IO33  | Vi2 — 0-10V AI  ✅ ADC1               |             |
| IO27  | Vi3 — 0-10V AI  ⚠️ ADC2               |             |
| IO32  | Vi4 — 0-10V AI  ✅ ADC1               |             |
| IO34  | Ii1 — 4-20mA AI ✅ ADC1               |             |
| IO39  | Ii2 — 4-20mA AI ✅ ADC1               |             |
| IO36  | Ii3 — 4-20mA AI ✅ ADC1               |             |
| IO35  | Ii4 — 4-20mA AI ✅ ADC1               |             |
| IO25  | AO1 — Vo1/Io1 (DAC1) — **W5500 MOSI når Ethernet er aktiveret** |             |
| IO26  | AO2 — Vo2/Io2 (DAC2) — **W5500 RST når Ethernet er aktiveret**  |             |

## Reserveret af ESP32-WROVER-modulet — MÅ IKKE BRUGES

| GPIO  | Bruges til | Note |
|-------|------------|------|
| IO6-IO11 | Intern SPI-flash | Gælder alle ESP32-moduler |
| IO16, IO17 | Intern PSRAM (WROVER) | Var tidligere brugt til W5500 RST/MOSI — det var rodårsagen til Ethernet-crashene (BUG-423). Firmwaren afviser dem nu i GPIO-mappings (BUG-428) |

## W5500 Ethernet (fra v7.9.68.33, BUG-423b)

| GPIO  | W5500 Funktion | Note                                          |
|-------|----------------|-----------------------------------------------|
| IO19  | MISO           | Tidligere ledig                               |
| IO25  | MOSI           | = AO1/DAC1 — eksternt AO1-kredsløb frakobles  |
| IO18  | SCK            | Tidligere ledig                               |
| IO5   | CS (SCS)       | Strapping pin — intern pullup holder CS HIGH (inaktiv) ved boot |
| IO4   | INT            | Interrupt, active LOW                         |
| IO26  | RST            | = AO2/DAC2 — eksternt AO2-kredsløb frakobles  |

Mens Ethernet er aktiveret, skriver firmwaren **ikke** til DAC'en (AO1/AO2 er reelt slået fra), da `dacWrite()` ellers ville overtage MOSI/RST. Med Ethernet slået fra kan AO1/AO2 bruges som før (hvis de analoge kredsløb er tilsluttet).

**Reelt ledige GPIOs** (når Ethernet er slået fra): IO4, IO5 (strapping), IO18, IO19.
