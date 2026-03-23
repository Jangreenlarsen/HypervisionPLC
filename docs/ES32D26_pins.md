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
| IO25  | AO1 — Vo1/Io1 (DAC1)                   |             |
| IO26  | AO2 — Vo2/Io2 (DAC2)                   |             |

## Ledige GPIOs — brugt til W5500 Ethernet

| GPIO  | W5500 Funktion | Note                                          |
|-------|----------------|-----------------------------------------------|
| IO19  | MISO           | VSPI default                                  |
| IO17  | MOSI           | Remapped via GPIO matrix                      |
| IO18  | SCK            | VSPI default                                  |
| IO5   | CS (SCS)       | Strapping pin — intern pullup holder CS HIGH (inaktiv) ved boot |
| IO4   | INT            | Interrupt, active LOW                         |
| IO16  | RST            | Hardware reset, active LOW                    |
