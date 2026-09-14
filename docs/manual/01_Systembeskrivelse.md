# 1. Systembeskrivelse

[← Indeks](00_INDEKS.md) · Næste: [2. Hardware & Moduler →](02_Hardware_og_Moduler.md)

---

## 1.1 Hvad er Hypervision PLC?

Hypervision PLC er en **letvægts, netværkstilkoblet PLC** bygget på ESP32-platformen. Den kombinerer tre ting, der traditionelt kræver separat udstyr:

1. **En Modbus RTU-slave** — svarer på forespørgsler fra et overordnet SCADA/HMI-system, som en almindelig Modbus-I/O-enhed.
2. **En Modbus RTU/RS-485-master** — poller selv andre enheder på bussen (VFD'er, sensorer, målere) og gør deres data tilgængelige internt.
3. **Et programmerbart logiklag** — et ST-sprog (Structured Text, IEC 61131-3) der kører uafhængigt af Modbus-kommunikationen og kan læse/skrive registre, styre GPIO, køre tællere/timere og selv agere Modbus-master.

Det hele er tilgængeligt både klassisk (RS-485/Modbus) og moderne (REST API, webdashboard, WiFi/Ethernet) — samme enhed kan stå i et rack og tale RTU til et ældre SCADA-system, samtidig med at et Node-RED-flow eller en Python-service læser og skriver data over HTTP.

![Monitor Dashboard — Overblik-fanen med System- og Netværks-status](assets/screenshots/dashboard_overview.png)

## 1.2 Hvilket problem løser den?

Klassiske PLC'er er ofte enten:
- **Rene Modbus-slaver** — billige I/O-moduler uden egen logik, kun register-mapping.
- **Fuldgyldige industrielle PLC'er** — kraftfulde, men dyre, lukkede økosystemer, og typisk uden indbygget REST API eller webbaseret administration.

Hypervision PLC sigter efter mellemrummet: **programmerbar logik og Modbus-connectivity til en brøkdel af prisen**, med et moderne integrationslag (REST API, JSON, webdashboard) der gør den lige så let at koble til en Node-RED-flow, et Python-script eller et cloud-dashboard som til et traditionelt SCADA-system.

Typiske anvendelser:
- **Fjernstyret I/O-udvidelse** til et eksisterende SCADA-system, med lokal logik der kan reagere selv ved tab af netværksforbindelse.
- **Protokol-gateway/aggregator** — poll flere Modbus RTU-enheder på en RS-485-bus og eksponér dem samlet som ét register-map eller via REST API.
- **Selvstændig lille styring** — tælle pulser, styre relæer efter tidsstyring, beregne afledte værdier — uden at skulle bygge om en hel PLC-installation.
- **Undervisning/prototyping** — et system hvor man kan se og forstå hele stakken (kildekode er åben og modulær), i modsætning til lukkede industrielle PLC-økosystemer.

## 1.3 Målgruppe

- **Automationsingeniører** der har brug for en billig, programmerbar Modbus-node med moderne integrationsmuligheder.
- **Integratorer/udviklere** der bygger IoT- eller SCADA-broer og har brug for REST API-adgang til Modbus-data.
- **Vedligeholdelsespersonale** der skal kunne fejlsøge og overvåge et anlæg via en webbrowser, uden specialsoftware.

## 1.4 Nøglefunktioner

| Område | Beskrivelse | Se kapitel |
|--------|-------------|------------|
| **Modbus Slave** | RTU over RS-485. Konfigurerbar slave-ID, baudrate, paritet. Standard function codes (læs/skriv coils, discrete inputs, holding- og input-registre). | [6](06_Modbus_Interface.md) |
| **Modbus Master** | Asynkron baggrundstask med prioritetskø og cache — poller eksterne enheder uden at blokere ST-logikken. Konfigurerbar via CLI/REST/ST. | [6](06_Modbus_Interface.md), [8](08_ST_Logic_Programmering.md) |
| **ST Logic** | Op til 4 uafhængige programmer, IEC 61131-3-inspireret sprog med typesystem (BOOL/INT/DINT/REAL/DWORD/TIME), kompileres til bytecode og køres på en indbygget VM. Understøtter IF/CASE/FOR/WHILE/REPEAT, brugerdefinerede funktioner/funktionsblokke, TON/TOF/TP-timere, CTU/CTD/CTUD-tællere, og direkte Modbus Master-kald (`MB_READ_HOLDING`, `MB_WRITE_HOLDING` m.fl.). | [8](08_ST_Logic_Programmering.md) |
| **Tællere & Timere** | 4 uafhængige hardware-nære tællere (software-, hardware- eller interrupt-baserede) og 4 timere med flere driftstilstande, begge tilgængelige for Modbus og ST Logic. | [9](09_Taellere_og_Timere.md) |
| **REST API** | JSON over HTTP(S), dækker konfiguration, register-/coil-adgang, GPIO, ST Logic-programmer (inkl. debugger), backup/restore, OTA, metrics. | [7](07_REST_API.md) |
| **Web Dashboard** | Live-monitor, ST Logic-editor med debugger og trend-visning, webbaseret CLI-konsol, alarmhistorik, Modbus-aktivitetslog. | [4](04_Web_Dashboard_og_Monitor.md) |
| **CLI** | Fuld `show`/`set`-kommandostruktur, tilgængelig via seriel USB, telnet og web — samme kommandosæt alle tre steder. | [5](05_CLI_Konsol.md) |
| **Netværk** | Wi-Fi og/eller kablet Ethernet (W5500), NTP-tidssynkronisering, statisk IP eller DHCP. | [12](12_Netvaerkskonfiguration.md) |
| **Modbus Expansion Boards** | Administrér eksterne "HypervisionPLC Extension Board"-enheder (egne RS485/RS232-kanaler) fra PLC'ens egen web-UI — Modbus TCP-dataplan + REST-management-API, med `MBX_*`-ST Logic-funktioner til kontinuerlig datatrafik. | [6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409) |
| **Sikkerhed** | Rollebaseret adgangsstyring (RBAC) med flere brugere/roller, valgfri HTTPS/TLS, rate-limiting. | [10](10_Sikkerhed_og_Adgangsstyring.md) |
| **Overvågning** | Prometheus-kompatible metrics, SSE (Server-Sent Events) for real-time push af register-/tæller-/timer-ændringer til dashboardet. | [4](04_Web_Dashboard_og_Monitor.md), [7](07_REST_API.md) |

## 1.5 Arkitektur i fugleperspektiv

```
                    ┌─────────────────────────────────────────┐
                    │              Hypervision PLC              │
                    │                                           │
  RS-485 ───────────┤  Modbus Slave (UART)                     │
  (SCADA/HMI)        │       ↕                                  │
                    │  Register/Coil Storage ←──┐               │
                    │       ↕                    │              │
  RS-485 ───────────┤  Modbus Master (UART) ─────┤              │
  (eksterne enheder) │  (async kø + cache)         │              │
                    │       ↕                    │              │
                    │  ST Logic VM (×4 progr.) ───┘              │
                    │       ↕                                   │
                    │  Tællere / Timere / GPIO                  │
                    │                                           │
  Wi-Fi/Ethernet ───┤  REST API · Web Dashboard · CLI (telnet)  │
                    └─────────────────────────────────────────┘
```

Register- og coil-lageret er det centrale knudepunkt: Modbus Slave, Modbus Master, ST Logic, tællere/timere og REST API'et læser og skriver alle sammen til det samme lager. Det er derfor et enkelt system kan fungere som både slave og master, og både styres lokalt (ST Logic) og eksternt (SCADA eller REST) på samme tid.

## 1.6 Hvad er Hypervision PLC *ikke*?

For at sætte forventningerne rigtigt:

- **Ikke sikkerhedscertificeret (SIL/PL)** — den er ikke beregnet til safety-kritiske funktioner uden ekstern sikkerhedsniveau-vurdering.
- **Ikke en fuld IEC 61131-3-implementering** — ST-sproget er et praktisk, funktionsorienteret undersæt, ikke en komplet standard-compliant implementering med Ladder/FBD/SFC.
- **Ikke redundant** — én ESP32, én strømforsyning. Kritiske installationer bør planlægge for det.

---

[← Indeks](00_INDEKS.md) · Næste: [2. Hardware & Moduler →](02_Hardware_og_Moduler.md)
