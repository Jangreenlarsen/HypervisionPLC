# 2. Hardware & Moduler

[← 1. Systembeskrivelse](01_Systembeskrivelse.md) · [Indeks](00_INDEKS.md) · Næste: [3. Installation →](03_Installation_og_Foerste_Opstart.md)

---

## 2.1 Understøttede board-varianter

Hypervision PLC-firmwaren bygges til én af tre hovedvarianter, valgt via en build-flag i `platformio.ini`. Alle deler samme softwarelag (Modbus, ST Logic, REST API, dashboard) — forskellen er GPIO-tildeling og fysisk IO.

| Variant | Modul | Beregnet til |
|---------|-------|--------------|
| **ESP32-WROOM-32 (30-pin)** — standard | ESP32-D0WD | Generel udviklings-/prototype-brug, direkte GPIO-adgang |
| **ESP32-WROOM-32 (38-pin)** | ESP32-D0WD | Som ovenfor, flere fysiske pins tilgængelige |
| **ES32D26** (Eletechsup) | ESP32-D0WD-V3 rev3.1 / ESP32-WROVER (4 MB PSRAM) | Industri-IO-modul: 8× digital ind, 8× relæudgang, 8× analog ind, 2× analog ud, onboard RS-485 |
| **Waveshare ESP32-S3-ETH** | ESP32-S3 | Indbygget kablet Ethernet (W5500 onboard, ingen ekstern ledning) |

> Se [`../ES32D26_BOARD_GUIDE.md`](../ES32D26_BOARD_GUIDE.md) og [`../ES32D26_pins.md`](../ES32D26_pins.md) for komplet, verificeret pin-for-pin-dokumentation af ES32D26-varianten — det mest brugte industri-board. Dette kapitel giver overblikket; de filer giver detaljerne.

## 2.2 IO-oversigt (ES32D26)

Det mest almindeligt anvendte board i felten. Alt IO er hardware-signalkonditioneret på boardet — ingen eksterne komponenter nødvendige for standard 0-10V/4-20mA/24V-signaler.

| Type | Antal | Detaljer |
|------|-------|----------|
| **Digitale indgange (DI)** | 8 | Via SN74HC165 skifteregister, opto-isoleret |
| **Digitale udgange / relæer (DO)** | 8 | Via SN74HC595 skifteregister |
| **Analoge indgange, spænding** | 4 | 0-10V (Vi1-Vi4) |
| **Analoge indgange, strøm** | 4 | 4-20mA (Ii1-Ii4) |
| **Analoge udgange** | 2 | DAC-baseret (Vo/Io, AO1-AO2) |
| **RS-485** | 1 | Onboard transceiver, delt mellem Modbus Slave og Master (kun én rolle ad gangen — se §2.4) |
| **Ethernet (valgfri)** | 1 | Via eksternt W5500-modul over SPI |

Digitale ind- og udgange tilgås internt som **virtuelle GPIO'er** (101-108 for indgange, 201-208 for relæudgange), så de kan bindes til Modbus-registre og ST Logic-variabler på præcis samme måde som fysiske GPIO-pins.

## 2.3 Trådløs og kablet netværk

- **Wi-Fi** — indbygget på alle ESP32-varianter. Klient-mode (tilslutter et eksisterende netværk).
- **Ethernet (W5500)** — valgfrit eksternt SPI-modul (indbygget på Waveshare S3-ETH). Kan køre samtidig med Wi-Fi.
- Begge kan have statisk IP eller DHCP, konfigureres uafhængigt af hinanden. Se [kapitel 12](12_Netvaerkskonfiguration.md).

## 2.4 Modbus-transceiver: delt vs. dedikeret UART

Dette er den vigtigste hardware-detalje at forstå før installation:

- **ESP32-WROOM-32 (30/38-pin):** UART0 (USB-seriel) er Modbus **Slave**, UART1 (separate GPIO-pins) er Modbus **Master**. De to roller har **hver deres fysiske UART** og kan køre samtidig.
- **ES32D26:** har kun **én** RS-485-transceiver ombord, delt mellem Slave- og Master-rollen (`MODBUS_SINGLE_TRANSCEIVER`). Systemet kan altså være **enten** Slave **eller** Master ad gangen på dette board — ikke begge samtidig — styret af `set modbus mode slave|master`. Samme fysiske pins (GPIO1/GPIO3) deles desuden med USB-seriel-konsollen ved boot; se boot-sekvensen i [kapitel 3](03_Installation_og_Foerste_Opstart.md#opstart-pa-es32d26--rs-485-vs-usb-konsol).

> **Vigtigt ved ES32D26:** hvis I har brug for at være Modbus Slave **og** Master samtidig, kræver det enten et separat RS-485-modul på en anden UART, eller en anden board-variant med to fysisk adskilte UART'er.

## 2.5 Status-LED og fysiske indikatorer

Standard-boards har en status-LED på GPIO2 (hjerteslag, ca. 500 ms interval — se `HEARTBEAT_INTERVAL_MS`), der viser at hovedløkken kører. **På ES32D26 er GPIO2 optaget af skifteregister-logikken**, så der er ingen fysisk status-LED på dette board — brug i stedet dashboardets uptime-visning eller CLI'ens `show status` til at bekræfte at systemet kører.

## 2.6 Flash & hukommelse

| Ressource | Standard ESP32-WROOM-32 | ES32D26 (WROVER) |
|-----------|--------------------------|--------------------|
| Flash | 4 MB | 4 MB |
| PSRAM | Ingen | 4 MB (aktiveret, bruges til ST Logic-kildekode-pool) |
| NVS (konfiguration) | 64 KB partition | 64 KB partition |
| OTA-partitioner | 2× ~1,8 MB (dual-bank, rollback-understøttet) | Samme |

Flash er typisk 89-90% udnyttet på et fuldt konfigureret build (Ethernet + alle features aktiveret) — se [`../../BUGS_INDEX.md`](../../BUGS_INDEX.md) (FEAT-145/146) for baggrund om flash-optimeringen der gjorde plads til PSRAM-migreringen.

---

[← 1. Systembeskrivelse](01_Systembeskrivelse.md) · [Indeks](00_INDEKS.md) · Næste: [3. Installation →](03_Installation_og_Foerste_Opstart.md)
