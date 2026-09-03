# 4. Web Dashboard & Monitor

[← 3. Installation](03_Installation_og_Foerste_Opstart.md) · [Indeks](00_INDEKS.md) · Næste: [5. CLI/Konsol →](05_CLI_Konsol.md)

---

## 4.1 Sideoversigt

Webgrænsefladen består af fem sider, alle bag samme login:

| Side | URL | Formål |
|------|-----|--------|
| **Dashboard** | `/` | Live-monitor: system, netværk, Modbus, alarmer, IO, tællere/timere |
| **ST Logic Editor** | `/editor` | Skriv, kompilér, upload og debug ST-programmer med runtime-monitor |
| **System** | `/system` | Systemindstillinger (uden for det almindelige `set`-CLI-flow) |
| **OTA-opdatering** | `/ota` | Upload ny firmware |
| **Web-CLI** | `/cli` | Fuld CLI-konsol i browseren — samme kommandoer som seriel/telnet |

Login sker via HTTP Basic Auth; browseren gemmer credentials i `sessionStorage` for resten af sessionen (går tabt ved lukning af fanen).

## 4.2 Dashboard — layout og faner

Dashboardet er organiseret i **kort** (cards), grupperet under faner:

- **Alle** — alle kort samlet
- **Overblik** — System, Netværk, Alarm Historik
- **Modbus** — Modbus Slave, Modbus Master (+ manuel Read/Write), Modbus Aktivitetslog, RTU-trafik
- **Forbindelser** — HTTP API/SSE, TCP-forbindelsesmonitor, NTP
- **Applikation** — Tællere, Timere, ST Logic, Digital I/O

Kortenes størrelse (bred/normal) og synlighed kan tilpasses direkte i UI'et (⇔-knap på hvert kort) og gemmes per browser.

### Nøglekort

**System** — firmwareversion, oppetid, heap fri/min/fragmentering, PSRAM, aktive FreeRTOS-tasks.

**Netværk** — Wi-Fi/Ethernet-status, IP/gateway/DNS, Telnet-status, Wi-Fi-genforbindelser.

**Modbus Slave / Modbus Master** — konfiguration og løbende statistik (requests, success rate, fejltyper). Master-kortet indeholder desuden cache-/kø-statistik (hit rate, kø-dybde, prioritets-drops) og en **manuel Read/Write-formular** til hurtige ad-hoc-forespørgsler uden at skulle bruge CLI.

**Modbus Aktivitetslog** — et wire-level-vindue ind i hvad der rent faktisk sker på Modbus-interfacet lige nu: hver transaktion (Master *og* Slave-rolle) med rolle, kilde (ST Logic/CLI/dashboard/ekstern master), slave-ID, function code, adresse, værdi og status. RAM-only (nulstilles ved reboot), fungerer som et levende diagnoseværktøj — se [kapitel 13](13_Fejlfinding.md) for hvordan den bruges til fejlsøgning.

**Alarm Historik** — systemhændelser (heap-advarsler, kommunikationsfejl, auth-fejl, m.fl.), med filtrering på sværhedsgrad og kvitteringsstatus.

**Digital I/O** — live-visning og manuel styring af digitale ind-/udgange.

**Tællere / Timere / ST Logic** — status og seneste værdier for hver af de 4 tællere, 4 timere og 4 ST-programmer.

## 4.3 ST Logic Editor

Se [kapitel 8](08_ST_Logic_Programmering.md) for selve sproget. Dette afsnit dækker værktøjet.

Editoren har 4 uafhængige program-faner (Logic1-4). Værktøjslinjen:

| Knap | Funktion |
|------|----------|
| **Kompilér** | Oversætter kildekoden til bytecode. Fejl markeres direkte i editoren med linjenummer. |
| **Gem Config** | Gemmer program + bindings persistent. |
| **Stop** | Deaktiverer programmet (stopper eksekvering, bevarer variabeltilstand). |
| **Reinit** | "Cold restart" — nulstiller alle variabler til deres initialværdier, stateful storage (timere/tællere/edge-detektion) og statistik. |
| **Slet** | Fjerner programmet helt. |
| **Download / Upload** | Hent/gem kildekode som `.st`-fil. |
| **Find** | Søg/erstat i kildekoden (Ctrl+F/Ctrl+H). |
| **Editor / Bindings / Monitor** | Skift mellem kildekode-visning, variabel-bindings-konfiguration og runtime-monitor. |

### Runtime Monitor

Live-visning af alle programvariabler mens programmet kører, med:
- **Udførelser / Tid (ms) / Min-Max / Fejl / Overruns** — performance- og sundhedsstatistik
- **Trend-kurver** pr. variabel, valgbar historik-længde og opdateringshastighed
- **Debug-kontroller:** Pause Execute, Single Step, Single Cycle, Normal Execute — fuld single-step-debugger med PC-visning og breakpoints

> **Fejlfindingstip:** stiger "Udførelser" støt, men ingen variabler ændrer sig og "Fejl" forbliver 0, er programmet ikke crashet — det venter typisk på noget der aldrig sker (f.eks. et Modbus-svar). Se [kapitel 13, afsnit "ST-program ser ud til at køre, men intet opdateres"](13_Fejlfinding.md#st-program-ser-ud-til-at-koere-men-intet-opdateres) for en systematisk fremgangsmåde.

## 4.4 Web-CLI (`/cli`)

En fuld terminal-emulering i browseren, med samme kommandosæt som seriel/telnet-konsollen (se [kapitel 5](05_CLI_Konsol.md)). Praktisk når man er logget ind via HTTPS og ikke ønsker at åbne en separat telnet-session — og det eneste af de tre konsol-adgange der kan beskyttes af TLS.

## 4.5 Real-time opdatering (SSE)

Dashboardet bruger **Server-Sent Events** til at modtage register-, tæller- og timer-ændringer i realtid uden konstant polling. Falder SSE-forbindelsen væk, falder UI'et automatisk tilbage til almindelig polling — funktionaliteten er uændret, blot med lidt højere opdateringsforsinkelse. Se [`../SSE_USER_GUIDE.md`](../SSE_USER_GUIDE.md) for den tekniske protokol, hvis I bygger jeres eget klient-integration mod samme SSE-strøm.

---

[← 3. Installation](03_Installation_og_Foerste_Opstart.md) · [Indeks](00_INDEKS.md) · Næste: [5. CLI/Konsol →](05_CLI_Konsol.md)
