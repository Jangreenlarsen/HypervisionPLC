# Plan: W5500 Ethernet Interface til Modbus Slave

<!-- DOC-STATUS: historisk -->
> ⚠️ **Historisk dokument — vedligeholdes ikke længere.** Skrevet til en ældre firmwareversion og kan være forældet (CLI-syntaks, pins, endpoints, standardværdier). Den aktuelle, vedligeholdte dokumentation er [brugermanualen](manual/00_INDEKS.md) (CLI: appendiks A, REST: appendiks B, ST: appendiks D). Markeret ved dokumentationsgennemgang 2026-09-28.


Jeg kan give dig en grundig analyse af mulighederne. Her er de vigtigste overvejelser:

## 1. Hardware-integration

**SPI-forbindelse (påkrævet):**
- W5500 kommunikerer via SPI
- Arduino Mega 2560 har SPI på pins 50 (MISO), 51 (MOSI), 52 (SCK)
- Chip Select (CS) på pin 53 eller anden GPIO
- Disse pins er **ikke i konflikt** med dine nuværende UART1 (18,19) og RS-485 DIR (8)

**Strøm:**
- W5500 bruger 3,3V (kræver level shifter eller 3,3V supply med Pull-ups)
- Bør få egen strømforsyning eller voltage regulator

## 2. Arkitektoniske valg (vigtigste beslutninger)

Du skal vælge mellem tre tilgange:

**Option A: Modbus TCP Server (bedste integration)**
- Erstat RTU med TCP over ethernet
- Samme register-struktur, samme Modbus logik
- Port 502 (standard Modbus TCP)
- Fordele: Direkte migration af eksisterende Modbus clients
- Ulempe: Kræver statisk IP eller DHCP config

**Option B: Dobbelt protokol (RTU + TCP)**
- Behold RS-485 RTU som primær
- Tilføj Modbus TCP som parallel interface
- Samme register-struktur, begge protokoller
- Fordele: Backward compatible, redundans
- Ulempe: Mere kompleks, RAM-intensiv

**Option C: HTTP Management Interface (lightweight)**
- Beholde RS-485 RTU
- Tilføje simpel HTTP server til status/config
- REST-API for læsning af registers, ændring af config
- Fordele: Lavere kompleksitet, god for monitoring
- Ulempe: Ikke rigtig Modbus protocol compliance

## 3. Software-arkitektur

**Nye komponenter:**
- W5500 driver (Wiznet Arduino bibliotek)
- Ethernet initialisering + DHCP/statisk IP config
- Modbus TCP server (hvis Option A/B) eller HTTP handler (hvis Option C)
- IP/gateway/netmask konfiguration i CLI eller EEPROM

**Loop-integration:**
```
Main loop:
  - CLI handling (som nu)
  - Modbus RTU processing (som nu)
  - Timers (som nu)
  - Counters (som nu)
  + Ethernet packet processing (NYTT)
  + Modbus TCP request handling (hvis TCP valgt)
```

**CLI-udvidelse:**
```
show network       // IP, gateway, MAC, link status
set network dhcp on|off
set network ip <addr>
set network gateway <addr>
set network netmask <addr>
```

## 4. Ressource-vurdering (KRITISK)

**RAM (i dag 83% = 6798 bytes / 8 KB):**
- W5500 driver: ~500 bytes
- Ethernet buffers (RX/TX): ~2 KB minimum
- Modbus TCP handler: ~200 bytes
- **TOTAL: ~2,7 KB nyt** → ville blive ~94% RAM (risiko!)
- **Løsning:** Reducere eksisterende buffers eller optimere current code

**Flash (i dag 27.7% = 70226 bytes / 256 KB):**
- W5500 bibliotek: ~8-10 KB
- Modbus TCP implementering: ~3-5 KB (mindre end RTU pga. ingen CRC)
- HTTP handler: ~2-3 KB (hvis valgt)
- **TOTAL: ~13-15 KB nyt** → ville blive ~34-40% Flash (acceptable)

**Konklusion:** RAM er **bottleneck**, ikke Flash!

## 5. Implementeringsstrategi

**Fase 1: Proof of Concept**
- Få W5500 til at køre med simpel ping/DHCP
- Test SPI kommunikation, ingen Modbus ændringer

**Fase 2: Valg af protokol**
- Implementer **Option A (Modbus TCP)** eller **Option C (HTTP)** baseret på dine behov
- Option B (dual) ville være for RAM-tung

**Fase 3: Integration**
- Tilføj ethernet event loop
- CLI config for IP-indstillinger
- Test med ekstern Modbus TCP client (hvis TCP) eller curl (hvis HTTP)

**Fase 4: Optimering**
- RAM-profilering og bufferoptimering
- Netværksperformance testing
- EEPROM schema update (ny v13) for ethernet config

## 6. Konkrete anbefalinger

✓ **Gør dette:**
- Brug **Option A (Modbus TCP)** - cleanest solution, reuse existing logic
- Beholde RS-485 RTU (kan tilføjes senere som dual hvis behov)
- Statisk IP først, DHCP senere
- Separate SPI transaction handling fra main Modbus loop (non-blocking)

✗ **Undgå dette:**
- Option B (dual RTU+TCP) fra start - for kompleks
- DHCP med callback-baseret design - timing issues
- RAM-tunge buffer strategier

## 7. Kritiske spørgsmål at besvare

1. **Formål:** Skal Modbus clients tale TCP over ethernet, eller bare monitoring/status?
2. **Redundans:** Skal RS-485 blive ved som fallback?
3. **Network:** Er dette LAN, WAN, eller IoT cloud?
4. **DHCP:** Statisk IP eller dynamisk tildeling?
5. **Performance:** Hvor mange simultane TCP clients skal understøttes?

---

**Min anbefaling:** Start med **Option A (Modbus TCP)** hvis du vil erstatte RTU, eller **Option C (HTTP REST)** hvis du vil beholde RTU og bare tilføje monitoring. RAM vil være main challenge - du skal præfab fjerne eller komprimere noget eksisterende.
