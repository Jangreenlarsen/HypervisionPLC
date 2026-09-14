# Design: Modbus Expansion Board (op til 8 boards × 8 Master-interfaces via netværk)

**Status:** Designforslag — ikke implementeret. Til brug for en separat udviklingsindsats (nyt board, ny firmware, formentlig nyt repo).
**Målgruppe:** En udvikler (menneske eller Claude-session) der skal bygge expansion-boardets hardware og firmware fra bunden.
**Ophav:** Hypervision PLC-projektet (`Modbus_server_slave_ESP32`), efter et forsøg (FEAT-408) på at tilføje en 2. Modbus Master-motor DIREKTE på PLC'ens egen ESP32-chip stødte på en hardware-blocker — se §0.
**Afhængigheder:** Se "Kan dette dokument stå alene?" nedenfor.

---

## Kan dette dokument stå alene, eller kræver det adgang til PLC-repoet?

**Kort svar: arkitekturen og alle designbeslutninger står alene — men flere afsnit henviser bevidst til konkrete filer i `Modbus_server_slave_ESP32` som implementeringsmønstre, og fuld udnyttelse af dokumentet kræver læse-adgang til dem.**

Mere præcist, opdelt efter hvad man rent faktisk skal bygge:

- **Expansion-boardets eget hardware+firmware (§1-§4, §9-§10):** INGEN kode- eller build-afhængighed til PLC-repoet. Boardet er et selvstændigt firmware-projekt (nyt repo, egen `platformio.ini`/build-system) der udelukkende taler til PLC'en via de protokoller dette dokument definerer (§4 — Modbus TCP + REST). Man kan tekniske bygge boardet uden nogensinde at åbne PLC-repoet.
- **Men:** ~9 steder i dokumentet (samlet i Appendiks A) peger eksplicit på konkrete filer i PLC-repoet som **anbefalede implementeringsskabeloner** — fx `src/modbus_master.cpp`s PDU/CRC-logik, `mb_async.cpp`s kø/cache-design (§5.1.1), `api_handlers.cpp`s REST-auth-stil, `ota_handler.cpp`s dual-partition-mønster, og `config_load.cpp`s schema-migrationsmønster (§3.5). Uden adgang til disse filer skal udvikleren **genopfinde** disse (allerede afprøvede, produktionshærdede) løsninger fra bunden ud fra tekstbeskrivelserne alene — muligt, men et reelt tab af genbrugsværdi, og den primære begrundelse for hvorfor dette dokument overhovedet henviser så meget til PLC-repoet.
- **PLC-siden af integrationen (§5) er derimod IKKE uafhængig** — det arbejde sker bogstaveligt talt INDE I `Modbus_server_slave_ESP32`-repoet (nye filer som `modbus_expansion.cpp`, ændringer til `web/system.html`, CLI/ST Logic-udvidelser) og kræver fuld adgang til og forståelse af den eksisterende kodebase, ikke bare reference. Det er en separat, fremtidig arbejdsopgave for nogen med kontekst i DETTE repo, ikke expansion-board-udvikleren.

**Praktisk anbefaling:** giv expansion-board-udvikleren (menneske eller Claude-session) læse-adgang til `Modbus_server_slave_ESP32`-repoet, eller som minimum kopier af de ~9 filer listet i Appendiks A, selvom de kun skal bygge boardet og ikke PLC-siden.

---

## 0. Baggrund og motivation

PLC'en i dette projekt (ESP32-D0WD-V3/WROVER, "ES32D26"-board) har i dag:
- Modbus RTU **Slave** (altid til stede)
- Modbus RTU **Master #1** — ét RS485-bus, delt onboard-transceiver (UART-periferi #2 på denne chip)

Et forsøg på at tilføje en fuldstændig sideordnet **Master #2** på ESP32-chippens egen, ledige UART-hardware-periferi #1 blev grundigt implementeret (kø, cache, adaptiv backoff, CLI, hele arkitekturen — se `BUGS_INDEX.md` FEAT-408) men måtte rulles tilbage: `Serial1_inst.begin()` (aktivering af UART-periferi #1) fik enheden til konsekvent at heap-korrumpere og panic-genstarte, uafhængigt af GPIO-pins, mutex, task-oprettelse, eller om andre UART-perifierer var aktive samtidig. Root cause blev isoleret (symboliseret backtrace via seriel-konsol) til `malloc()`'s interne TLSF-heap-allokator — dvs. **heap-korruption**, ikke en logikfejl. Stærkeste forklaring: en kendt ESP32-silicium-erratum (PSRAM-cache kan korrumperes af en interrupt der rammer samtidig med en cache-miss — Espressifs egen `-mfix-esp32-psram-cache-issue`-compiler-flag afbøder dette for velkendte interrupt-kilder, men ikke nødvendigvis en helt ny, første-gangs UART-interrupt-kilde på en chip med PSRAM).

**Konklusion af FEAT-408:** at presse flere UART-perifierer ind i PLC'ens egen ESP32-chip er hardware-risikabelt på denne specifikke chip-familie. Løsningen er i stedet et **fysisk separat board**, der ejer og driver de ekstra RS485-busser selv, og som PLC'en taler til over et netværks-API i stedet for over intern UART-hardware.

**God nyhed:** Størstedelen af det arbejde der blev lavet til FEAT-408 er direkte genbrugeligt i denne nye arkitektur — se §5 (PLC-side integration).

**Kerneprincip — "single pane of glass":** Expansion-boardet har **ingen egen, rigtig brugerflade**. Det eneste der konfigureres lokalt, direkte på boardet, er det strengt nødvendige bootstrap for at få det på nettet (WiFi-credentials, se §3.4) — en klassisk "kan ikke løses via API, fordi uden netværk intet API"-undtagelse. **Alt andet** (kanal-baudrate/parity/timeout, enable/disable pr. kanal, firewall-regler, firmware-opdatering, osv.) konfigureres **udelukkende via API'et, styret fra en ny sektion i PLC'ens allerede-eksisterende System-side** (samme sted brugeren i dag konfigurerer Ethernet/WiFi/ACL/RBAC for selve PLC'en). Brugeren skal aldrig logge ind på to forskellige systemer for at drifte løsningen. Dette er gennemgående i hele designet, se især det udvidede §5.

---

## 1. High-Level Design

### 1.1 Systemoversigt

```
┌─────────────────────────┐
│   Hypervision PLC        │
│   (ESP32, denne repo)     │
│                            │
│  - ST Logic-motor          │
│  - CLI / Web-UI             │
│  - System-side: AL config  │      Modbus TCP (§4.1 — data-plan, 8 porte pr. board)
│    af ALLE boards          │      + REST/JSON management-API (§4.2 — autentificeret,
│    (§5 — "single pane")    │      inkl. firewall §4.3 og OTA §4.2)
│  - Kø/cache/backoff         │  ════════════╦══════════════╦═══ ... ══╦═══════
│    (genbrugt fra FEAT-408)  │              ║              ║          ║
│  - Egen RS485 (Slave+       │              ▼              ▼          ▼
│    Master #1) URØRT          │      ┌───────────┐  ┌───────────┐  ┌───────────┐
└─────────────────────────┘      │  Board #1  │  │  Board #2  │  │  Board #8  │
                                    │  (egen IP) │  │  (egen IP) │  │  (egen IP) │
                                    │  8 kanaler │  │  8 kanaler │  │  8 kanaler │
                                    └─────┬─────┘  └─────┬─────┘  └─────┬─────┘
                                        RS485×8        RS485×8        RS485×8
                                          ▼               ▼               ▼
                                    Feltbusser      Feltbusser      Feltbusser

Op til 8 boards × 8 kanaler = op til 64 uafhængige Modbus RTU-master-forbindelser,
hvert board fuldstændig identisk i design/firmware, kun adskilt ved IP-adresse (§1.4).
```

### 1.2 Ansvarsfordeling — hvorfor denne grænse er tegnet her

| Ansvar | PLC (eksisterende kodebase) | Expansion Board (nyt) |
|---|---|---|
| ST Logic-programmer, brugerflade, CLI, web-UI | ✅ | ❌ |
| Beslutte HVILKE registre der skal læses/skrives, HVOR OFTE | ✅ (kø/prioritet/cache) | ❌ |
| Fysisk RS485-transaktion (frame, CRC, timing, DE/RE) | ❌ (for de 8 nye kanaler) | ✅ |
| Egen Slave + Master #1 (eksisterende, onboard) | ✅ (**urørt**) | ❌ |
| Fejl-klassificering (timeout/CRC/exception) | Modtager resultat | Udfører og rapporterer |
| Persistering af PLC-konfiguration (ACL, RBAC, ST-programmer) | ✅ | ❌ |
| Kanal-konfiguration, firewall-regler, firmware-opdatering | Sender via management-API'et | Anvender + husker indtil ny config |

**Designprincip:** Expansion-boardet er bevidst "tyndt" — det er en pålidelig, lav-latency Modbus RTU-**udførelses**-motor, ikke en selvstændig PLC. Al forretningslogik, prioritering og caching forbliver på hoved-PLC'en (hvor den allerede er bygget og afprøvet). Dette minimerer expansion-boardets kompleksitet og angrebsflade, og betyder hoved-PLC'ens eksisterende ST Logic-sprog/CLI-brugere ikke mærker forskel på om en Modbus-transaktion går til den onboard Master #1 eller til en af de 8 nye, eksterne kanaler.

### 1.3 Kommunikationsmodel — protokolvalg (analyse)

**Transport:** IP-netværk (WiFi og/eller Ethernet — se §3.4), samme LAN-segment som PLC'en, eller et dedikeret punkt-til-punkt-link.

**Protokol — dette valg er ikke oplagt og fortjener en rigtig afvejning, ikke en antagelse.** Alternativer overvejet:

| Protokol | Vurdering |
|---|---|
| **REST/HTTP + JSON** | God til konfiguration/status/firmware-opdatering (sjælden, menneske-læselig trafik, variabel-længde data, rigtig auth), men JSON-parsing + HTTP-headers er unødvendig overhead for HØJFREKVENT register-læsning/-skrivning på en lille MCU. |
| **Modbus TCP** (industristandard for "Modbus over IP") | Modbus TCP er **bogstaveligt talt** Modbus RTU's PDU (function code + data — FC01-FC06/FC16, uændret) pakket i en 7-byte MBAP-header i stedet for RTU's adresse-byte+CRC, transporteret over TCP i stedet for RS485. Det er PRÆCIS den use case en "Modbus TCP-til-RTU-gateway" er lavet til, og det er hvad kommercielle multi-port gateways (Moxa, Advantech, m.fl.) reelt gør internt. **PLC'ens egen, allerede-hærdede protokol-forståelse (`src/modbus_master.cpp`'s FC-håndtering) er direkte genbrugelig** — kun framing ændrer sig, ikke selve protokol-semantikken. **Ulempe:** Modbus TCP definerer KUN selve data-transaktionen — ingen konfiguration, health-check, statistik, firewall-styring, firmware-opdatering eller autentificering er en del af protokollen (og har historisk ingen sikkerhed indbygget overhovedet). |
| MQTT | Godt til mange-til-mange pub/sub; dette er et 1-til-1-forhold med synkrone spørgsmål/svar — pub/sub passer dårligt til det mønster, og en broker er en ekstra komponent uden modsvarende gevinst her. Vurderet fra. |
| Rå TCP, eget binært protokol | Lavest overhead teoretisk, men opfinder et helt nyt, udokumenteret format der skal bygges og vedligeholdes fra bunden på BEGGE sider, uden fordel over Modbus TCP/REST (som allerede er standarder for præcis disse to use cases). Vurderet fra. |
| CoAP | Lettere end HTTP, designet til MCU'er — men UDP (ikke-garanteret levering, kræver egen retry-logik), og hverken PLC'en eller expansion-boardet har et CoAP-bibliotek i forvejen. Fordelen opvejer ikke merarbejdet. Vurderet fra. |
| WebSocket | God fremtidig optimering af kontrol-planet (vedvarende forbindelse, mindre per-kald-overhead end HTTP) — men ikke en selvstændig løsning for data-planet, og ikke nødvendig for v1. |

**Konklusion — revideret to gange, endelig model er en bevidst hybrid:**

Det oprindelige udkast landede på en hybrid (Modbus TCP for data, REST/JSON for kontrol). En efterfølgende revision (fordi PLC'en selv er 100% register-orienteret internt i sin egen ST Logic-integration) samlede ALT i Modbus TCP, inkl. management-planet som et register-map. Den beslutning er nu rullet delvist tilbage igen, af tre konkrete, tekniske grunde fundet under design af firewall- og OTA-funktionerne:

1. **Firewall-regelsæt (IP-allowlist, §4.3) er i sagens natur variabel-længde data** — det passer dårligt ind i faste 16-bit-registerblokke uden enten en kunstig øvre grænse eller en klodset "slot"-protokol opfundet til lejligheden.
2. **Firmware-opdatering (OTA) er en binær blob-overførsel** — det kan strukturelt slet ikke udtrykkes som Modbus-registre, uanset design. Det kræver et transportlag der kan strømme vilkårligt store payloads, hvilket HTTP allerede løser.
3. **Management-planet har reelt brug for rigtig autentificering** (hvem må ændre firewall-regler, kanal-config, uploade ny firmware, reboote boardet). Modbus TCP har principielt ingen protokol-understøttelse for dette — en register-baseret management-løsning ville enten skulle opfinde sin egen auth-konvention oven på Modbus (ikke-standard, skrøbeligt), eller reelt stole blindt på netværkssegmentering alene for BÅDE data og management.

REST/JSON giver auth (Bearer-token), strukturerede fejlbeskeder, variabel-længde data (lister, binære uploads) "gratis" — alt sammen ting management-planet viser sig at have brug for — mens data-planets høj-frekvente register-læs/skriv fortsat er den use case Modbus TCP er skabt til og excellerer ved.

**Endelig, hybrid model:**
1. **Data-plan** (selve Modbus-transaktionerne mod feltbusserne): Modbus TCP, 8 porte pr. board (§4.1) — denne del af analysen har holdt gennem alle revisioner.
2. **Management-plan** (kanal-config, statistik, board-status, firewall-styring, OTA): **REST/JSON**, autentificeret, samme stil som PLC'ens egen, etablerede REST-API (`src/api_handlers.cpp`), på én dedikeret HTTP-port pr. board (§4.2).

**Konsekvens:** PLC-siden får to klienter mod hvert board i stedet for én — en Modbus TCP-klient (data, genbruger `mb_async.cpp`-designet, §5.1.1) og en simpel, autentificeret REST-klient (management, lav frekvens, ikke underlagt samme kø/cache-krav som data-planet, §5.1). Mere kode end "ét samlet Modbus TCP-design", men løser auth-, firewall- og OTA-behovet uden ikke-standard protokol-opfindelser.

### 1.4 Skalerbarhed — enkelt kanal, enkelt board, og flere boards

Designet skalerer på to uafhængige niveauer:

1. **Kanaler pr. board (1-8, hardware-bestemt, IKKE fastlåst til 8):** boardets kanaltal er en funktion af hvor mange UART-expander-kredsløb der fysisk er monteret, ikke en firmware-grænse — se §2.2.2 for den præcise hardware/firmware-mekanik (modulær bestykning + auto-detektion ved boot). Arkitekturen (kanal-indekseret, én FreeRTOS-task pr. kanal) skalerer ned til færre kanaler uden kodeændring — et 1-, 2-, 4- eller 8-kanals-board kører **samme firmware-binary**, blot med tilsvarende færre task-instanser aktive, fastlagt af hvad boardet selv finder ved opstart.
2. **Antal boards (op til 8):** hvert board er en **fuldstændig identisk, selvstændig enhed** med sin egen IP-adresse — der er intet i selve board-firmwaren eller grænseflade-designet der skal ændres for at understøtte flere boards, fordi IP-adressen alene adskiller dem. **Al kompleksitet ved multi-board-understøttelse ligger derfor udelukkende på PLC-siden**: en liste af (navn, IP, management-token) op til 8 boards, gemt i PLC'ens konfiguration og administreret fra System-siden (§5.2), hvor hvert board i listen giver PLC'en 8 kanaler mere at tale med (op til 8 × 8 = **64 uafhængige feltbus-forbindelser** i alt).

**Adressering af en given feltbus-forbindelse fra PLC-siden (CLI/ST Logic):** to eksplicitte koordinater, `(board 1-8, kanal 1-8)`, fremfor ét fladt "global kanal 1-64"-tal — mindre fejlbehæftet at huske/taste korrekt, og matcher direkte hvordan boardene selv er adskilt (IP pr. board). Se §5.1 for den konkrete CLI/ST Logic-signatur.

---

## 2. Low-Level Design — Expansion Board Hardware

### 2.1 Kritisk designregel (direkte lektion fra FEAT-408)

> **Brug ALDRIG mikrocontrollerens interne UART-hardware-periferier til mere end 1-2 kanaler uden dedikeret afprøvning.** For 8 kanaler: brug **eksterne UART-expander-ICs over SPI**, hver med egen, isoleret interrupt-controller — undgå at flere højfrekvente UART-interrupt-kilder deler samme silicium-cache/heap-arkitektur som blev roden til FEAT-408s fejl.

### 2.2 Anbefalet arkitektur: gateway-MCU + UART-expander-chips

```
                    ┌─────────────────────────────┐
                    │   Gateway-MCU (ESP32 anbe-   │
                    │   falet — se begrundelse)     │
                    │                                │
                    │   - WiFi (indbygget)           │
                    │   - Ethernet PHY (W5500 SPI,    │
                    │     valgfri tilvalg)            │
                    │   - Modbus TCP data-server × 8   │
                    │   - REST management-API (§4.2)   │
                    │     auth + firewall (§4.3) + OTA │
                    │   - SPI-master til expandere    │
                    └──────────────┬─────────────────┘
                                   │ SPI (delt bus, separate CS-linjer)
                 ┌─────────────────┼─────────────────┐
                 ▼                 ▼                 ▼
         ┌───────────────┐ ┌───────────────┐ ┌───────────────┐
         │ MAX14830       │ │ MAX14830       │ │  (evt. flere) │
         │ (4× UART/SPI)  │ │ (4× UART/SPI)  │ │                │
         └───┬─┬─┬─┬─────┘ └───┬─┬─┬─┬─────┘ └───────────────┘
             │ │ │ │             │ │ │ │
     Pr. kanal: RS485 (SP3485) + RS232 (MAX3232) dual-populeret,
     kun ÉN aktiv ad gangen, valgt via expander-GPIO (§2.2.1)
             │ │ │ │             │ │ │ │
             ▼ ▼ ▼ ▼             ▼ ▼ ▼ ▼
          Kanal 1-4          Kanal 5-8
```

**Komponentvalg (forslag, ikke bindende):**

| Komponent | Forslag | Begrundelse |
|---|---|---|
| Gateway-MCU | **ESP32** (samme familie som PLC'en, genkendelig toolchain) | Har **indbygget WiFi** — dækker den lokale bootstrap-opsætning (§3.4) uden ekstra WiFi-modul/-kompleksitet — og kan valgfrit udvides med Ethernet via samme W5500-chip PLC'en allerede bruger (`ETHERNET_W5500_ENABLED`), hvis et kabelbaseret link foretrækkes. Kun ÉT internt UART-behov på gateway-MCU'en (debug-konsol) — FEAT-408s risiko gælder ikke her, da al Modbus-UART-trafik går over SPI til expanderne, ikke over interne UART-perifierer (se §2.2.1 for hvorfor det er en forudsætning, ikke kun en anbefaling, når RS485 OG RS232 skal kunne vælges pr. kanal). |
| UART-expander | Modulopbygget efter ønsket kanaltal — se §2.2.2 (fx SC16IS750 pr. kanal, SC16IS752 pr. 2 kanaler, eller MAX14830 pr. 4 kanaler) | Velafprøvede, industri-standard SPI-til-UART-broer med egen 16550-kompatibel FIFO — isolerer interrupt-belastningen fra gateway-MCU'ens egen kerne. Vælg en familie med **mindst 1 ekstra GPIO-pin pr. UART-kanal** (MAX14830 har dette indbygget) — bruges til RS485/RS232-modevalg, §2.2.1 |
| RS485-transceiver | SP3485/MAX3485 (3.3V, half-duplex, DE/RE-styret) pr. kanal | Samme transceiver-familie som PLC'ens eksisterende onboard-design, kendt adfærd |
| RS232-transceiver | MAX3232/MAX3222 (3.3V-side, indbygget ladningspumpe til ±5-13V RS232-niveauer) pr. kanal | Standardvalg til 3.3V-MCU'er der skal tale RS232 — vælg en variant med enable/shutdown-pin (fx `SHDN`), nødvendig for §2.2.1's modevalg |
| Ethernet (valgfrit tilvalg) | W5500 (SPI) — **samme chip som PLC'en allerede understøtter** | Genbrug af eksisterende driver-viden på tværs af de to boards, for installationer der foretrækker kabel frem for WiFi |
| Isolering (anbefalet, ikke krav) | Digital isolator (ADuM-serie) eller isoleret DC/DC pr. kanal | Feltbusser i industrielt miljø er ofte udsat for støj/jordsløjfer — isolering pr. kanal forhindrer at én dårlig feltbus-installation kan forstyrre resten af boardet eller gateway-MCU'en |
| Status-LED | 1× pr. kanal (aktivitet) + 1× WiFi/link + 1× fejl | Diagnostik uden at skulle læse logs — vigtigt her, da boardet bevidst ikke har nogen driftsmæssig egen UI (§0) |
| Beskyttelse | TVS-dioder på hver RS485 A/B-linje, PTC-sikring | Standard feltbus-beskyttelse mod transienter |

### 2.2.1 Per-kanal RS485/RS232-valg

**Krav (§0's "single pane of glass"-princip anvendt på hardware):** modvalget (RS485 eller RS232) skal kunne sættes fra PLC'ens System-side ligesom alt andet — IKKE kræve fysisk adgang til boardet (jumper, DIP-switch, eller udskiftning af et daughter-board). Det er en bevidst dyrere hardware-beslutning end alternativerne, men konsistent med resten af designets kerneprincip.

**Elektrisk design:** RS485 og RS232 er fundamentalt forskellige fysiske lag (differentiel ~5V half-duplex med retningsstyring vs. single-ended ±5-13V fuld-duplex uden retningsstyring) — det kan IKKE løses med ét enkelt "universal"-transceiver-chip. Løsningen er **dual-populering pr. kanal**: både SP3485 (RS485) og MAX3232 (RS232) er monteret på hver kanal, men kun ÉN er elektrisk aktiv ad gangen:

- **TX-siden:** begge transceiveres driver-input fodres fra samme UART-expander-TX-pin; kun den valgte transceiver har sin driver aktiveret (RS485: `DE` høj; RS232: `SHDN`/enable aktiv) — den anden er tri-state og påvirker ikke bussen.
- **RX-siden:** begge transceiveres receiver-output samles til expanderens RX-pin via en simpel 2:1 analog-mux (fx en enkelt logic-mux-IC), styret af samme GPIO som vælger TX-siden — kun den aktive transceivers modtagne data når frem til UART'en.
- **Modevalg-GPIO:** bruger den ekstra GPIO-pin pr. UART-kanal på expander-chippen (§2.2's komponentvalg) — ét bit pr. kanal, sat af firmwaren ud fra `mode`-feltet i kanal-konfigurationen (§4.2), ingen fysisk handling nødvendig.
- **Forskel i firmware-adfærd:** i RS485-mode toggler kanal-tasken `DE/RE` omkring hver transmission (half-duplex, §3.2); i RS232-mode er begge retninger altid aktive (fuld-duplex, punkt-til-punkt) — ingen DE/RE-toggling. Modbus RTU's egen frame-timing (t3.5-mellemrum) gælder uændret i begge tilstande, da det er en protokol-regel, ikke en fysisk-lag-regel.
- **Semantisk forskel:** en RS485-kanal er typisk en multi-drop-bus (flere slaver på samme fysiske par); en RS232-kanal er per definition punkt-til-punkt (netop ÉT tilsluttet apparat) — `mbx scan` (§5.1) er stadig teknisk muligt i RS232-mode, men kun meningsfuldt mod det ene forventede slave-ID.
- **Connector:** brug ét fælles 5-polet terminalblok pr. kanal (`A`, `B`, `TXD`, `RXD`, `GND`) fremfor separate connectorer pr. mode — `A`/`B` bruges kun i RS485-mode, `TXD`/`RXD` kun i RS232-mode, `GND` er fælles.

**Billigere alternativ (hvis fuld fjern-konfigurerbarhed ikke er et krav):** dual-populering + GPIO-mux kan udelades til fordel for et simpelt jumper- eller DIP-switch-valg pr. kanal, eller helt faste, fabriksbestemte kanal-typer (nogle kanaler RS485, andre RS232, valgt ved bestilling/montering). Billigere i komponenter og PCB-areal, men bryder "single pane of glass"-princippet for netop dette ene valg — anbefales KUN hvis omkostning vejer tungere end konsistensen, og bør i så fald dokumenteres tydeligt som en bevidst undtagelse i overleveringen til PLC-siden (§5.2's UI skal stadig vise den fabriksbestemte mode, blot som read-only).

### 2.2.2 Variabelt kanaltal (1-8) — hardware-bestykning + auto-detektion, ikke en firmware-grænse

**Princip:** boardets kanaltal (1-8, §1.4) bestemmes af hvor mange UART-expander-kanaler der fysisk er monteret — **hardware-UART-styring**, ikke et konfigurationstal i firmwaren. Samme firmware-binary kører uændret på tværs af alle varianter (vigtigt for §4.2's OTA — kun ÉT firmware-image at vedligeholde og udrulle, uanset boardets fysiske størrelse).

**Modulær bestykning efter ønsket kanaltal:**

| Ønsket kanaltal | Forslag til bestykning | Kommentar |
|---|---|---|
| 1 | 1× SC16IS750 (1 UART/SPI) | Mindste, billigste variant — stadig samme PCB-design/footprint-familie, blot de fleste positioner ubestykkede |
| 2 | 1× SC16IS752 (2 UART/SPI) | |
| 4 | 1× MAX14830 (4 UART/SPI) ELLER 2× SC16IS752 | |
| 6 | 1× MAX14830 + 1× SC16IS752 | |
| 8 | 2× MAX14830 (§2.2's diagram) | Fuld udgave |

Alle varianter bruger **samme SPI-bus med op til 8 dedikerede CS-linjer** (én pr. kanal-position, §2.2's diagram) — PCB'en routes fuldt til 8 kanaler uanset hvilken variant der samles; kun komponenterne (expander-chip + transceiver-par pr. §2.2.1 + connector) udelades for de ubestykkede positioner. Dette holder PCB-layoutet til ÉT design for alle varianter, hvilket er billigere i den samlede produktudvikling end 5 separate layouts.

**Auto-detektion ved boot (firmwaren, ikke installatøren, afgør kanaltallet):**
1. Ved opstart forsøger firmwaren en simpel identifikations-transaktion (læs af expander-chippens interne scratch-/ID-register over SPI) på hver af de 8 CS-linjer, i rækkefølge.
2. En CS-linje der svarer korrekt (kendt chip-signatur/forventet ekko-værdi) → kanalen findes fysisk, markeres aktiv.
3. En CS-linje der timer ud eller returnerer ugyldige data (ingen komponent monteret, floating bus) → kanalen findes ikke, markeres permanent inaktiv for resten af boardets levetid (indtil næste boot).
4. Resultatet gemmes som `active_channels` (§4.2.1's `GET /api/status`) — det ER den autoritative kilde til boardets reelle kanaltal, ikke noget PLC-siden selv skal vide eller konfigurere på forhånd. `GET /api/channels` returnerer kun entries for de faktisk detekterede kanaler; `GET`/`PUT /api/channels/{n}` for `n > active_channels` svarer `404`.
5. **Ingen fysisk mærkning/typeskilt-parsing nødvendig** — PLC'en opdager selv et boards reelle kanaltal ved første "Test forbindelse" (§5.2), præcis samme UX uanset hvilken hardware-variant der er tale om.

**Konsekvens for PLC-sidens UI (§5.2):** kanal-listen for et board skal rendere dynamisk ud fra `active_channels`, IKKE en hardkodet antagelse om 8 rækker — et 2-kanals-board viser 2 rækker, ikke 8 hvoraf 6 er tomme/deaktiverede.

### 2.3 Strømbudget

8 samtidige kanaler (dual-populeret RS485+RS232 pr. §2.2.1, kun én transceiver aktiv ad gangen pr. kanal) + expander-chips + gateway-MCU + Ethernet-PHY: design til minimum 500mA @ 5V (mere med isolering). MAX3232-familiens indbyggede ladningspumpe trækker mærkbart mere strøm end en passiv RS485-driver, selv i idle — medregn dette i budgettet uanset om det er RS485- eller RS232-siden der reelt bruges, da begge transceivere sidder på bussen (kun én er aktiv, men den inaktive er ikke nødvendigvis strømløs afhængig af valgt IC-variant — tjek datablad for reel shutdown-strømafledning). Brug en switching-regulator, ikke lineær, af termiske grunde ved kontinuerlig drift.

---

## 3. Low-Level Design — Expansion Board Firmware

### 3.1 Overordnet struktur (spejler PLC'ens allerede-afprøvede mønster)

```
main.cpp
  ├─ provisioning.cpp/.h       — §3.4: WiFi-bootstrap (AP-mode + minimal lokal webside), KUN aktiv indtil forbundet — etablerer også management-API'ets auth-token og seeder firewall-allowlisten med PLC'ens IP
  ├─ net_driver.cpp/.h         — WiFi-init (og valgfrit Ethernet/W5500), DHCP/statisk IP, genopkobling
  ├─ modbus_tcp_server.cpp/.h  — 8× Modbus TCP-lyttesocket (data-plan, port 502-509, §4.1) — MBAP-parsing, PDU videresendes til kanal-task
  ├─ http_server.cpp/.h        — REST management-API (§4.2), autentificeret (Bearer-token, §4.4) — kanal-config, statistik, board-status, firewall-styring (§4.3), OTA (§4.2), samt provisioning-siden (§3.4, kun i AP-mode)
  ├─ firewall.cpp/.h           — IP-allowlist håndhævelse på Modbus TCP-lyttesocketsne (502-509) — tjekker peer-IP FØR accept(), regelsæt styret via REST (§4.3)
  ├─ ota_handler.cpp/.h        — modtager ny firmware som strømmet binær body (§4.2), skriver til inaktiv OTA-partition, verificerer, kræver reboot for aktivering — spejler `src/ota_handler.cpp` i PLC-repoet
  ├─ uart_expander.cpp/.h      — SPI-driver for MAX14830/SC16IS752, abstraherer 8 UART-kanaler bag samme interface som en almindelig UART
  ├─ modbus_channel.cpp/.h × 8 instanser (eller 1 fil, N instanser af samme struct)
  │     — ÉN FreeRTOS-task pr. kanal, hver med:
  │       - egen request-kø (FreeRTOS-kø, IKKE delt mellem kanaler — undgår enhver cross-channel race)
  │       - Modbus RTU frame-opbygning/CRC/parsing (kan læses direkte af `src/modbus_master.cpp` i PLC-repoet som reference — samme protokol, samme CRC16-algoritme, PDU'en er identisk med det Modbus TCP-serveren modtager)
  │       - egen statistik (total/success/timeout/crc/exception pr. kanal)
  └─ config.cpp/.h              — kanal-konfiguration + firewall-allowlist + auth-token, gemt i NVS/flash, overlever reboot — modtages KUN via management-API'et (§4.2) fra PLC'en, ingen lokal indtastning
```

### 3.2 Per-kanal task-model — direkte genbrug af et allerede-afprøvet mønster

Dette er **identisk arkitektur** til PLC'ens `mb_async.cpp` (den ÉN-motor-version, ikke den droppede Master #2) — blot ganget med 8 uafhængige instanser i stedet for 1. Hver kanal-task:

1. Venter (blokerende, semaphore) på en ny forespørgsel i sin kø — forespørgslen kommer nu fra **Modbus TCP-serverens** PDU-parsing (ikke fra en REST-handler, se §1.3/§4.1).
2. Udfører selve feltbus-transaktionen (send, vent på svar med timeout, CRC-check) — se `src/modbus_master.cpp:modbus_master_send_request()` i PLC-repoet for den fulde, hærdede implementering (håndterer bl.a. to-fase-timeout, exception-frames, adaptiv baud-override) der kan bruges næsten ord-for-ord som reference. **Kun i RS485-mode** (§2.2.1) toggles `DE/RE` omkring selve sendingen; i RS232-mode er begge retninger permanent aktive (fuld-duplex, punkt-til-punkt), og dette trin udelades — kanalens `mode`-felt (§4.2) afgør hvilken sti der køres, læst fra kanalens config ved task-start og ved hver rekonfiguration.
3. Lægger PDU-svaret tilbage til Modbus TCP-serveren, som pakker det i en MBAP-response-header og sender det på den ventende TCP-forbindelse.

**Vigtigt:** Hold FreeRTOS-stacks og køer 100% adskilte pr. kanal-task — INGEN delt tilstand mellem kanaler ud over selve SPI-bussen til expander-chippen (som i sig selv skal mutex-beskyttes, præcis som PLC'ens `g_modbus_uart_mutex`-mønster for den delte transceiver).

### 3.3 Timing/watchdog

- Hver kanal-task fodrer sin egen "sidste aktivitet"-timestamp; gateway-MCU'ens hovedtask overvåger alle aktive kanaler (`active_channels`, §2.2.2) og eksponerer "kanal N hænger" via kanalens statistik-endpoint (`GET /api/channels/{n}`, §4.2).
- Brug samme watchdog-filosofi som PLC'en (`watchdog_monitor.cpp`): en software-watchdog der nulstiller boardet ved reel hængning, med reset-årsag gemt til RTC/NVS så den kan aflæses efter genstart — se §3.6 for den fulde diagnostik-teknik dette muliggør (præcis den mekanisme der gjorde det muligt at diagnosticere FEAT-408s fejl).

### 3.4 Provisioning — den ENESTE lokale, egne konfiguration boardet har

**Princip (§0):** Alt driftsrelateret konfigureres fra PLC'en. Expansion-boardet har kun brug for lokal indgriben for at løse "hønen og ægget"-problemet: uden netværk kan intet API nås.

**Anbefalet flow (velkendt IoT-mønster):**
1. **Fabriksnyt/fabriksnulstillet board** starter i WiFi **AP-mode** (eget, midlertidigt access point, fx `ModbusExpansion-XXXX` hvor XXXX er en del af MAC-adressen) med en minimal, indbygget webside på en fast IP (fx `192.168.4.1`). Siden dækker **hele bootstrap-behovet i ét trin**: SSID + password (og evt. valgfri statisk IP/DHCP-valg) for produktionsnetværket, PLC'ens IP-adresse (seedes ind i firewall-allowlisten, §4.3), og et management-API-token som boardet selv genererer og viser på siden — kopieres én gang ind i PLC'ens System-side (§5.2), samme "vis nøglen én gang ved parring"-mønster som de fleste IoT-enheder bruger.
2. Ved gemt konfiguration genstarter boardet, forbinder til det angivne netværk, og forlader AP-mode permanent (indtil evt. fabriksnulstilling igen, fx via en fysisk knap holdt inde ved boot — samme mønster som langt de fleste IoT-enheder).
3. Herfra er boardet **udelukkende** styret via netværket: management-API'et (§4.2) kræver token fra nu af (§4.4), kun IP'er på firewall-allowlisten (§4.3) kan nå data-plan-portene, og PLC'en taler Modbus TCP til dataplan-portene (§4.1).
4. **Ingen anden lokal UI eksisterer** — intet web-dashboard, ingen CLI, ingen mulighed for at ændre kanal-config lokalt på boardet. Dette er bevidst: det forhindrer "config drift" (to steder der begge tror de har den autoritative konfiguration) og holder boardets egen angrebsflade og kodemængde minimal.
5. Fabriksnulstilling (fysisk knap, holdt X sekunder ved boot) rydder WiFi-credentials, management-API-token OG firewall-allowlist samtidig, og går tilbage til AP-mode — ét entydigt, forudsigeligt nulpunkt (kanal-konfiguration kan efter samme princip enten bevares eller ryddes, anbefaling: ryd begge).

### 3.4.1 Provisioning-websidens GUI — præcis felt-for-felt-specifikation

Dette er den ENESTE grafiske brugerflade boardet nogensinde selv viser (§0) — den skal derfor dække **alt** der kræves for at bringe et fabriksnyt board til fuldt driftsklar tilstand, i ét besøg, uden en efterfølgende "runde 2"-side. Siden serveres statisk fra boardets AP-mode-IP (`192.168.4.1`), som én enkelt, selvstændig HTML-side (samme "indlejret, gzippet HTML i firmwaren"-mønster som PLC'ens egne `include/generated_web/*_html_gz.h` — ingen eksterne CDN-afhængigheder, da boardet i AP-mode ikke har internetadgang). Design den mobilvenlig — installatøren står typisk ved et el-skab med en telefon, ikke en bærbar.

**Formularfelter (i rækkefølge):**

| # | Felt | Type | Påkrævet | Validering/adfærd |
|---|---|---|---|---|
| 1 | WiFi-netværk (SSID) | Dropdown (scannet liste) + "Andet…"-mulighed for skjult SSID | Ja | Boardet scanner tilgængelige netværk ved sidens indlæsning (samme UX som de fleste IoT-opsætningssider) |
| 2 | WiFi-adgangskode | Password-felt (med "vis"-øje-ikon) | Ja (med mindre åbent netværk) | Ingen client-side styrke-validering nødvendig — boardet opdager selv en forkert adgangskode ved forbindelsesforsøget (se fejltilstande nedenfor) |
| 3 | Netværkstype | Radioknapper: "DHCP (anbefalet)" / "Statisk IP" | Ja (default DHCP) | Ved "Statisk IP" foldes felt 4-6 ud |
| 4 | IP-adresse (kun ved statisk) | Tekstfelt | Betinget | Standard IPv4-format-validering |
| 5 | Subnetmaske (kun ved statisk) | Tekstfelt | Betinget | Standard IPv4-format-validering |
| 6 | Gateway (kun ved statisk) | Tekstfelt | Betinget | Standard IPv4-format-validering |
| 7 | PLC'ens IP-adresse | Tekstfelt | **Ja** | Seedes direkte ind i firewall-allowlisten (§4.3) — boardets data-plan-porte er ubrugelige indtil denne er sat korrekt. Kort hjælpetekst under feltet: "Find PLC'ens IP under System → Netværk" |
| — | **Forbind**-knap | Submit | — | Deaktiveres under indsendelse (undgå dobbelt-submit), viser spinner/statustekst |

**Efter vellykket forbindelse (ny sidevisning, samme AP endnu ikke lukket, eller en "sidste besked" hvis AP allerede er lukket — vælg det der er nemmest at implementere robust):**

| Element | Indhold |
|---|---|
| Statusbesked | "Forbundet til [SSID]. Boardets IP er nu: [IP]." |
| **Management-API-token** | Vises i klartekst, med en "kopiér"-knap, og en tydelig advarsel: **"Denne nøgle vises kun én gang. Kopiér den nu og indsæt den i PLC'ens System-side under 'Modbus Expansion Boards'."** (§5.2) — ingen "vis igen senere"-mulighed, hverken via denne side (som forsvinder når AP-mode lukker) eller via management-API'et selv (tokenet er write-only fra det øjeblik det er genereret, se §4.4) |
| Næste-skridt-tekst | Kort, nummereret: "1) Gå til PLC'ens System-side. 2) Tilføj et nyt board med IP + tokenet ovenfor. 3) Konfigurér kanalerne derfra." |

**Fejltilstande siden skal håndtere eksplicit (ikke bare en generisk "noget gik galt"):**
- Forkert WiFi-adgangskode → boardet forbliver i AP-mode, siden viser en tydelig fejl, formularen forbliver udfyldt (undtagen adgangskode-feltet af sikkerhedsvaner) så installatøren kun skal rette ét felt.
- Netværk fundet, men PLC-IP'en (felt 7) ikke kan pinges/nås fra det nye netværk ved forbindelsestest → tillad alligevel at gemme (boardet kan ikke vide om PLC'en er tændt endnu), men vis en advarsel, ikke en blokerende fejl.
- WiFi-timeout (SSID forsvinder, forkert kanal, osv.) → efter en rimelig timeout (fx 30 sek.), fald automatisk tilbage til AP-mode med en fejlbesked, i stedet for at hænge på "forbinder…" for evigt.

**Bevidst UDELADT fra denne side (jf. §0/§3.4 punkt 4):** boardets navn/label, kanal-konfiguration, baudrate/parity, firewall-liste udover PLC'ens ene IP, og alt andet driftsrelateret — det ville skabe en anden autoritativ kilde end PLC'ens System-side (§5.2) og er bevidst undladt, ikke glemt.

### 3.5 NVS-konfiguration og schema-versionering — en dyrekøbt lektion fra selve dette designs forhistorie

Under FEAT-408-forsøget (§0) blev et konkret, dyrt princip bekræftet i praksis: **en gemt config-schema-version er en envejs-dør, så snart mindst én enhed har gemt data i det schema.** PLC'en har et `CONFIG_SCHEMA_VERSION`-tal og en migrationsblok i `config_load.cpp`, der ved boot sammenligner den gemte version mod koden — men ruller koden tilbage til en TIDLIGERE schema-version end det en enhed allerede har gemt, tolkes den nyere, gemte data som "ukendt schema", og enheden falder tilbage til **fabriksdefaults** (WiFi, kanal-config, firewall-regler — ALT tabt).

**Konsekvens for expansion-boardets `config.cpp` (§3.1):**
- Implementér samme mønster fra dag 1: et eksplicit schema-version-tal gemt sammen med selve configen i NVS, og en migrationsfunktion der kun kører når den gemte version er ÆLDRE end koden forventer (aldrig omvendt).
- **Bump ALDRIG en schema-version ned igen**, heller ikke midlertidigt under udvikling — skal en fejlbehæftet feature rulles tilbage efter at være testet på rigtig hardware, behold schema-tallet og marker de tilføjede felter "reserveret, ubrugt" i stedet for at fjerne dem og sænke tallet (præcis den løsning PLC-siden selv endte med for `modbus_master2`-feltet, jf. §5.1's tabel).
- Dette bliver særligt vigtigt her, fordi **OTA (§4.2/§7) gør schema-migration til en løbende, produktions-relevant bekymring** frem for kun noget der sker under udvikling — enhver fremtidig firmware-opdatering, der tilføjer et nyt config-felt, skal følge denne regel, ellers mister en hel flåde af felt-installerede boards deres konfiguration ved den næste OTA.

### 3.6 Fejlsøgning ved hardware-bring-up — metoder der løste FEAT-408s hardware-blocker

Root cause-analysen der afslørede PSRAM-cache-erratummet (§2.1) krævede flere metodiske teknikker, som med stor sandsynlighed bliver nødvendige igen under expansion-boardets egen hardware-bring-up (§9, Fase 1-2). De dokumenteres her, så de ikke skal genopdages:

- **`RTC_NOINIT_ATTR`-checkpointing:** ESP32's RTC-hukommelse overlever software-/watchdog-reboot (nulstilles kun ved strøm-cyklus/deep-sleep), modsat normal RAM, som er væk efter en crash. Ved at skrive fremskridts-checkpoints til en `RTC_NOINIT_ATTR`-struct undervejs i en mistænkt kodesti, kan man efter en reboot læse PRÆCIS hvor langt koden nåede før den crashede — selv når selve crash-øjeblikket ikke er observeret direkte.
  - **Metodologisk faldgrube at undgå:** et checkpoint der også kan nås/overskrives af ANDEN kode end den specifikke sti man tester (fx en efterfølgende status-kommando der læser og dermed overskriver samme checkpoint) giver falsk-varierende resultater. Brug KUN checkpoints der udelukkende rammes af den ene kodesti under test.
- **Symboliseret backtrace:** et rå panic-backtrace (adresser) fra seriel-konsollen kan oversættes til funktionsnavne+linjenumre med `addr2line -e firmware.elf -pfiaC <adresser>` (for ESP32: `xtensa-esp32-elf-addr2line` i toolchain'en). **Kritisk forudsætning:** den lokale `.elf`-fil skal være PRÆCIS den samme build som er flashet på enheden — ellers giver symboliseringen forkerte eller meningsløse resultater. Anbefaling: brug samme to-lags versionering som PLC'en (semantisk version + auto-inkrementerende build-nummer, eksponeret via `GET /api/status`, §4.2), så en given flashet binary altid entydigt kan spores tilbage til den præcise lokale build.
- **Heap-korruptions-signaturen:** et crash INDE I `malloc()`/`free()` (fx et allokator-internt kald) fremfor på det sted i koden hvor fejlen faktisk introduceres, er klassisk heap-korruption (skrivning udenfor en allokeret buffer et helt andet sted) — ikke en logikfejl i den kode crashet peger på. Fejlen viser sig først, ikke-deterministisk, ved det næste malloc/free der tilfældigvis rammer den korrupte allokator-metadata.
- **Binær isolering:** når en fejl er svær at lokalisere, fjern én variabel ad gangen (task-oprettelse, en enkelt perifer-aktivering, specifikke GPIO-pins, samtidige perifierer) og gentest efter hver fjernelse, indtil roden er indsnævret til ét enkelt, isoleret kald — det var netop denne teknik der endegyldigt isolerede FEAT-408s fejl til `Serial1_inst.begin()` alene, uafhængigt af alle andre faktorer.

---

## 4. Protokol-design: kommandostruktur, data read/write og management-API

Grænsefladen er **hybrid** (§1.3): data-planet er ren Modbus TCP (§4.1), management-planet er et autentificeret REST/JSON-API (§4.2), og et board-lokalt IP-allowlist (§4.3) beskytter data-planet, som i sagens natur ikke kan autentificere sig selv.

Fejlkoder genbruges på tværs af begge planer — **1:1 med PLC'ens eksisterende `mb_error_code_t`** (se `include/types.h`), enten som rå Modbus exception-koder (data-planet) eller som `error_code`-feltet i management-API'ets JSON-fejlsvar:

| Værdi | Navn | Betydning |
|---|---|---|
| 0 | OK | Succes |
| 1 | TIMEOUT | Ingen svar fra slave |
| 2 | CRC_ERROR | Modtaget frame fejlede CRC |
| 3 | EXCEPTION | Modbus exception-response modtaget |
| 4 | MAX_REQUESTS_EXCEEDED | (reserveret) |
| 5 | NOT_ENABLED | Kanalen er ikke konfigureret/aktiveret |
| 6 | INVALID_SLAVE | Slave-ID udenfor 1-247 |
| 7 | INVALID_ADDRESS | Register-adresse ugyldig |
| 8 | BUS_BUSY | Kanalens UART-mutex ikke opnået (en anden transaktion kører) |
| 9 | CHANNEL_UNREACHABLE | *(ny, expansion-board-specifik)* SPI-kommunikation til UART-expanderen fejlede |

Hvert board lytter på **9 TCP-porte** på sin egen IP: 8 Modbus TCP data-porte (502-509, §4.1) + 1 HTTP management-port (fx 8080, §4.2) — sidstnævnte er et helt andet protokol-lag (HTTP, ikke Modbus) og er **ikke** omfattet af firewall-allowlisten (§4.3), da den beskytter sig selv via auth (§4.4).

### 4.1 Data-plan: Modbus TCP-gateway pr. kanal (port 502-509)

**Ingen ny protokol at designe** — dette er standard Modbus TCP (IEC 61158), uændret. Port `502 + (n-1)` for kanal `n` (1-8). PLC'en er Modbus TCP-master mod hver port, med samme FC01-FC06/FC16-semantik som den allerede bruger mod sin egen, onboard Master #1 — kun MBAP-headeren (Transaction ID, Protocol ID=0, Length, Unit ID) er ny i forhold til RTU-framing, triviel at parse (7 bytes, ingen CRC nødvendig da TCP selv garanterer integritet).

**RETTELSE (protokolfejl fundet ved analyse):** en tidligere version af dette afsnit påstod at `Unit ID` kunne sættes til en fast dummy-værdi fordi "slave-ID'et ligger i PDU'en" — det er faktuelt forkert. Hverken Modbus RTU's eller Modbus TCP's PDU (function code + data) indeholder noget slave-/enheds-ID; adressen ligger ALTID udenfor selve PDU'en (RTU: den indledende adresse-byte; TCP: `Unit ID`-feltet i MBAP-headeren). Korrekt, standard gateway-adfærd (præcis det Moxa/Advantech-gateways §1.3 selv refererer gør): **PLC'en sætter `Unit ID` til den ønskede fysiske slaves RTU-adresse (1-247, samme gyldighedsområde som `mb_error_code_t=6`, §4)**, og expansion-boardets kanal-task genbruger denne værdi 1:1 som adresse-byten i den udgående RTU-frame. Uden dette kan en RS485-kanal med flere slaver på samme bus (multi-drop, jf. §2.2.1) IKKE adresseres korrekt — kun ét slave-ID ville nogensinde kunne nås pr. kanal, hvilket modsiger designets egen multi-drop-antagelse og CLI/ST Logic-signaturen `MBX_READ_HOLDING(board, kanal, slave, addr)` (§5.1), som allerede forudsætter at `slave` rejser hele vejen ud til den fysiske bus.

Expansion-boardets kanal-task modtager PDU'en + det udpakkede `Unit ID` (→ RTU-adresse) fra Modbus TCP-serveren, udfører den fysiske RTU-transaktion (§3.2) med denne adresse, returnerer PDU-svaret samme vej (uden adresse-byte, jf. TCP's egen framing) — en normal, standard-kompatibel Modbus TCP-til-RTU-gateway, replikeret 8 gange pr. board.

**Timeout:** følger kanalens konfigurerede `timeout_ms` (§4.2's kanal-config) — PLC'ens Modbus TCP-klient sætter sin egen socket-timeout til `timeout_ms + margin`.

### 4.2 Management-plan: REST-API (port 8080)

**Auth:** alle endpoints (undtagen selve provisioning-siden, som kun findes i AP-mode, §3.4) kræver header `Authorization: Bearer <token>` — token etableret under provisionering, gemt i NVS på boardet og i PLC'ens System-side-config (samme mønster som PLC'ens egen ACL/RBAC-hemmeligheder). Se §4.4.

**Fejl-/svarformat**, samme stil som PLC'ens egen REST-API:
```json
{ "ok": false, "error_code": 6, "error": "invalid_slave", "message": "Slave-ID skal være 1-247" }
```

**Endpoints:**

| Metode | Sti | Beskrivelse |
|---|---|---|
| GET | `/api/status` | Board-status: `api_version` (se nedenfor), `fw_version`, `uptime_s`, `heap_free_kb`, `active_channels` (**hardware-detekteret ved boot, §2.2.2 — 1-8, IKKE altid 8**), per-kanal fejl-bitmap |
| GET | `/api/channels` | Liste af de faktisk tilstedeværende kanalers config+statistik (JSON-array, længde = `active_channels`, §2.2.2) |
| GET | `/api/channels/{n}` | Én kanals config+statistik (n=1..`active_channels`) — `n > active_channels` svarer `404` |
| PUT | `/api/channels/{n}/config` | Sæt kanalens fulde konfiguration **atomisk** — hele objektet skal med i ét kald (samme "aldrig felt-for-felt"-princip som tidligere, nu håndhævet ved at endpointet kræver alle felter, ikke PATCH-semantik) |
| POST | `/api/channels/{n}/reset-stats` | Nulstil én kanals tællere |
| POST | `/api/stats/reset` | Nulstil alle aktive kanalers tællere (`active_channels`, §2.2.2) |
| POST | `/api/reboot` | Blødt, kontrolleret reboot |
| GET | `/api/firewall` | Læs nuværende IP-allowlist (§4.3) |
| PUT | `/api/firewall` | Erstat hele allowlisten atomisk (§4.3 — valideringsregler) |
| POST | `/api/ota` | Upload ny firmware — rå binær body, **samme `--data-binary @firmware.bin`-mønster som PLC'ens egen OTA, IKKE multipart** (se `src/ota_handler.cpp` i denne repo for referenceimplementeringen) — skrives til inaktiv OTA-partition, verificeres, kræver eksplicit reboot (`POST /api/reboot`) for at aktivere |
| GET | `/api/ota/status` | Seneste OTA-forsøgs status (`idle`/`in_progress`/`success`/`failed` + fejlbesked) |
| POST | `/api/save` | Tving eksplicit gem til flash (normalt sker det automatisk ved hver config-skrivning — til fejlsøgning) |

**API-versionering (protokol-kontrakten, ikke firmware-versionen):** `GET /api/status` inkluderer et separat `api_version`-heltal (start ved `1`, bumpes KUN ved et brydende skift i selve endpoint-kontrakten — nyt påkrævet felt, ændret feltbetydning, fjernet endpoint — aldrig ved en ren tilføjelse af et nyt, valgfrit felt). Da både PLC- og board-firmware nu kan OTA'es uafhængigt af hinanden (§4.2/§7), er dette den eneste måde PLC-siden kan opdage et board der kører en ældre/nyere kontrakt end forventet, FØR det fejler uforklarligt på et manglende/uventet felt — samme "envejs-dør"-tankegang som §3.5's NVS-schema-regel, blot anvendt på selve grænsefladen. PLC-sidens `expansion_api_client.cpp` (§5.2) bør logge en tydelig advarsel (ikke en stille fejl) hvis et boards `api_version` ikke matcher det, klienten er skrevet imod.

**Eksempel — `GET /api/status`:**
```json
{
  "api_version": 1,
  "fw_version": "1.2.0",
  "fw_build": 47,
  "uptime_s": 86412,
  "heap_free_kb": 118,
  "active_channels": 8,
  "channel_error_bitmap": 0
}
```

**Eksempel — `GET /api/channels/3`:**
```json
{
  "channel": 3,
  "enabled": true,
  "mode": "rs485",
  "baudrate": 9600,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0,
  "status": "ok",
  "total_requests": 184213,
  "successful_requests": 184201,
  "timeout_errors": 9,
  "crc_errors": 2,
  "exception_errors": 1,
  "last_error_slave_id": 12,
  "last_error_address": 40010,
  "last_error_type": 1,
  "last_error_at_uptime_s": 86112
}
```
`last_error_at_uptime_s` (relativ til boardets egen `uptime_s`, ikke et absolut ur — boardet har ingen garanteret NTP-tid, §2.2) er den manglende brik der gør det muligt at korrelere en fejl på tværs af op til 64 kanaler i tid: PLC-siden kender selv `uptime_s` for samme poll og kan regne fejltidspunktet om til sin egen, NTP-synkroniserede kalendertid.

**Eksempel — `PUT /api/channels/3/config`:**
```json
{
  "enabled": true,
  "mode": "rs485",
  "baudrate": 9600,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0
}
```
Rå baudrate-værdi (ikke et encoded index som i et tidligere register-baseret udkast) — JSON har ingen af registerlayoutets 16-bit-begrænsninger, så indirektionen er ikke længere nødvendig; boardet validerer stadig mod samme gyldige sæt som PLC'ens `mb_is_valid_baudrate()`. `mode` er `"rs485"` eller `"rs232"` (§2.2.1) — styrer både den fysiske transceiver-valg-GPIO og om kanal-tasken toggler `DE/RE` (§3.2). Skift af `mode` for en kanal, der allerede har aktive transaktioner i kø, bør boardet håndtere ved at lade igangværende transaktioner færdiggøres på den GAMLE mode før omkobling — undgår at rive en transmission midt i et frame.

### 4.3 Firewall / IP-allowlist (beskytter data-planet, som ikke selv kan autentificere)

**Formål:** Modbus TCP (§4.1) har ingen protokol-auth — hvem som helst der kan nå en data-plan-port kan sende gyldige Modbus-forespørgsler. Et IP-allowlist håndhævet på selve TCP-forbindelsen (før noget Modbus-indhold overhovedet parses) er et andet forsvarslag, oveni netværkssegmentering (§4.4).

**Hvad det er (og ikke er):** et simpelt kilde-IP-allowlist på TCP `accept()`-niveau for portene 502-509 — IKKE en fuld stateful firewall (ingen NAT, ingen protokolinspektion, ingen outbound-filtrering). ESP32'en har hverken behov for eller ressourcer til mere end det — kald det aldrig "firewall" i kode/variabelnavne uden denne præcisering, så den næste udvikler ikke forsøger at bygge et netfilter-agtigt regelsæt.

**Hvordan det styres:** udelukkende via management-API'et (§4.2, autentificeret) — `GET`/`PUT /api/firewall`.

**Kritiske sikkerhedsregler for implementeringen (undgå at boardet kan bricke sig selv):**
1. **Allowlisten dækker KUN portene 502-509 (data-plan) — ALDRIG management-API-porten selv.** Management-API'et er allerede beskyttet af sin egen Bearer-token-auth (§4.4); hvis det også blev filtreret af allowlisten, kunne én fejlkonfigureret regel afskære PLC'en fra selv at kunne rette fejlen — kun en fysisk fabriksnulstilling ville kunne redde boardet igen.
2. **Allowlisten kan aldrig sættes til en tom liste via `PUT`** — boardet validerer at mindst 1 IP altid er i listen. Et forsøg på at sende en tom liste afvises med en tydelig fejl, fremfor at blive accepteret og utilsigtet spærre ALT.
3. **Allowlisten seedes under provisionering (§3.4) med PLC'ens IP**, indtastet manuelt på AP-mode-siden — boardet er derfor ALDRIG i en tilstand hvor data-planet er åbent for hele netværket efter provisionering, uden at nogen eksplicit har sat det sådan.
4. **Fabriksnulstilling rydder allowlisten** sammen med WiFi/token (§3.4, punkt 5) — konsistent "ét nulpunkt"-princip.

### 4.4 Autentificering og sikkerhed

- **Management-planet (REST, §4.2):** Bearer-token-auth på ALLE endpoints (§3.4 etablerer tokenet under provisionering) — samme mønster som PLC'ens egen REST-API (`src/api_handlers.cpp`s auth-makroer). Manglende/ugyldigt token besvares med `401`, samme konvention som PLC'en allerede bruger. Tokenet er systemets mest kritiske hemmelighed (kompromitteres det, kan en angriber ændre firewall-regler, kanal-config OG uploade vilkårlig firmware via OTA) — det bør derfor kunne roteres via samme fysiske AP-mode-mekanisme som resten af provisioneringen, ikke kun sættes én gang for altid.
- **Data-planet (Modbus TCP, §4.1):** ingen protokol-indbygget auth (industristandard-begrænsning, ikke en svaghed specifik for dette design) — beskyttet af to lag: netværkssegmentering (dedikeret VLAN, eller et direkte punkt-til-punkt-link mellem PLC og board) OG IP-allowlist-firewallet (§4.3). Ingen af de to alene er vandtæt (segmentering kan fejlkonfigureres i felten; en allowlist beskytter ikke mod IP-spoofing på samme segment) — sammen giver de reelt forsvar-i-dybden.
- **Forbindelses-begrænsning** (maks. N samtidige TCP-forbindelser pr. data-port) anbefales som robusthedsforanstaltning mod utilsigtet socket-pool-udmattelse fra en tilladt, men fejlkonfigureret klient.
- **Cleartext-forbehold:** management-API'et kører som udgangspunkt almindelig HTTP (matcher PLC'ens egen etablerede REST-stil) — Bearer-tokenet transporteres derfor i klartekst på netværket. Det er en accept­abel risiko UNDER FORUDSÆTNING AF at netværkssegmenteringen ovenfor faktisk overholdes; TLS er en mulig fremtidig hærdning, men ikke et krav for v1.

---

## 5. PLC-side integration — genbrug fra det droppede FEAT-408-arbejde

Dette afsnit beskriver arbejdet i **denne repo** (PLC-siden) — en separat, fremtidig feature, ikke en del af expansion-board-udviklerens opgave, men vigtig kontekst for at forstå hele systemet.

### 5.1 Kode-genbrug fra FEAT-408

Meget af Master #2-arbejdet, der blev rullet tilbage i FEAT-408, er **stadig værdifuldt** — kun det nederste transport-lag var problemet:

| Lag fra FEAT-408 | Genbrugeligt? | Hvordan |
|---|---|---|
| `mb_async2.cpp`'s prioritetskø, cache, adaptiv backoff, PENDING-recovery | ✅ Ja, næsten 1:1 | Samme design, men den ene funktion der i dag kalder `modbus_master2_send_request()` (UART) skal i stedet kalde en Modbus TCP-klient-funktion mod den relevante kanals TCP-port (§4.1) |
| `modbus_master2.cpp`'s FC01-FC06/FC16-frame-logik (PDU-opbygning + CRC) | ✅ Ja, næsten 1:1 for PDU-delen | Modbus TCP's PDU er BYTE-FOR-BYTE identisk med RTU's (kun rammen udenom er anderledes) — CRC beregnes ikke for TCP (erstattes af TCP's egen integritet), men selve FC-parameter-opbygningen genbruges direkte |
| `uart2_master_*`-transportlaget | ❌ Nej (var roden til hele FEAT-408-problemet) | Erstattes af en simpel TCP-socket-klient (+ MBAP-header i stedet for UART-RTU-framing) |
| CLI-mønsteret (`mb2 read/write/scan`) | ✅ Ja, som UX-skabelon | Ny kommandofamilie, f.eks. `mbx <board> <kanal> read/write/scan`, samme argumentparsing-stil |
| ST Logic-builtin-navnekonvention (`MB2_READ_HOLDING`) | ✅ Ja, som UX-skabelon | Ny familie, f.eks. `MBX_READ_HOLDING(board, kanal, slave, addr)` — to ekstra argumenter i signaturen (§1.4's board+kanal-koordinat, ikke bare ét kanal-tal, da der nu er op til 8 boards) |
| Aktivitetslog-integration (`MB_ACTIVITY_ROLE_MASTER2`) | ✅ Ja, konceptet | Udvid `mb_activity_role_t` eller `mb_activity_entry_t` med board+kanal-felter der dækker alle op til 64 eksterne kanaler under én rolle, i stedet for én enum-værdi pr. kanal |
| PersistConfig-udvidelse (`modbus_master2`-feltet, allerede i NVS schema 29 på migrerede enheder) | ✅ Ja, kan omdøbes/genfortolkes | Feltet ligger allerede i layoutet (se `include/types.h`) — kan udvides/omfortolkes til en liste af op til 8 (navn, IP, token)-tripler (§1.4), uden endnu en schema-migration for selve feltets eksistens (blot dets indre fortolkning) |
| PLC'ens egen REST-API-stil (`src/api_handlers.cpp`, auth-makroer, JSON-fejl-format) | ✅ Ja, direkte som skabelon | Management-API-klienten (§4.2) mod hvert board bør ligne PLC'ens eget REST-lag så tæt som praktisk muligt — samme udvikler-erfaring på begge sider af integrationen |

**Bemærk:** ovenstående tabel dækker primært **data-planet** (Modbus TCP-transaktioner, §4.1) — det er her `mb_async.cpp`'s kø/cache/backoff-design giver mening at genbruge (§5.1.1). **Management-planet (REST, §4.2)** er lavfrekvent, konfigurations-/menneske-initieret trafik (kanal-config, firewall-regler, OTA, status-poll hvert par sekunder) og har IKKE brug for samme kø-infrastruktur — en simpel, synkron REST-klient med retry+backoff ved fejl er tilstrækkeligt, adskilt fra data-planets asynkrone kø.

### 5.1.1 Cache/kø-design — konkrete, hærdede lektioner der SKAL genbruges, ikke genopdages

`mb_async.cpp`'s design er ikke tilfældigt — hvert element blev tilføjet som svar på en konkret, observeret fejl i produktion (fuld historik i `BUGS_INDEX.md`). Disse gælder **endnu stærkere** for expansion-board-integrationens data-plan end for PLC'ens egen, enkelt-bus Master #1, fordi der her er op til **64 samtidige kanaler** (8 boards × 8) at holde styr på — langt større overflade for de samme klasser fejl. De skal implementeres i PLC-sidens nye `modbus_expansion.cpp` (data-plan-klienten), ikke i expansion-boardets egen (bevidst simple, "dumme") kanal-task (§1.2/§3.2 — al prioriterings-/cache-intelligens hører hjemme på PLC-siden):

> **Ufravigelig invariant for `MBX_*`-builtins (skærpet, ikke kun underforstået af "genbrug koden" nedenfor):** præcis som `MB_READ_HOLDING` m.fl. i dag, må `MBX_READ_HOLDING(board, kanal, slave, addr)` m.fl. **ALDRIG blokere synkront på et live netværkskald til et board** — kaldet skal returnere en cachet værdi eller `PENDING` øjeblikkeligt, og selve netværksforespørgslen kører asynkront i baggrundstasken (nøjagtig samme mønster som `MB_*` i dag, §1.2). Dette er VIGTIGERE her end for Master #1, ikke mindre: netværkslatens til et eksternt board er i sagens natur højere og mere variabel end lokal RS485-timing (TCP-håndtryk, en langsom/uopnåelig switch, et board der er midt i en OTA-reboot, §7). Blokerer et ST-Logic-kald reelt på et sådant netværkssvar, fryser det HELE scan-cyklussen for den pågældende Logic-slot — samme fejlklasse projektet allerede har lagt betydelig arbejde i at eliminere for Master #1 (BUG-338/340 nedenfor). En implementering der springer dette over ved først at opdage det i produktion, ikke i design, ville gentage præcis den fejltype dette dokument selv advarer imod.

| Lektion | Hvorfor den findes (kort) | Hvorfor den er endnu vigtigere med op til 64 kanaler |
|---|---|---|
| **Dedup + cache-TTL** — spring en ny forespørgsel over hvis en frisk-nok værdi allerede er cachet, eller en forespørgsel for samme adresse allerede er PENDING | Undgår at spamme en fysisk bus med overlappende forespørgsler til samme register fra flere samtidige ST Logic-læsninger | Med 64 kanaler er der proportionalt 64× flere adresser at holde styr på — uden dedup vokser trafikken til expansion-boardene unødigt |
| **3-niveaus prioritetskø** — skriv (0) > frisk læsning (1) > refresh-læsning (2) | Garanterer skrivninger (data-integritet) aldrig sulter bag et boombardement af læsninger | Med flere boards konkurrerer flere kanalers trafik om samme kø-kapacitet på PLC-siden — prioritering forhindrer at ét board/én kanals travle polling forsinker en kritisk skrivning på et andet |
| **BUG-333: PENDING-recovery** — enhver cache-entry markeret PENDING skal *garanteret* kunne komme ud af den tilstand igen, selv hvis den underliggende forespørgsel tabes (queue-eviction) — via (a) eksplicit oprydning ved eviction og (b) en periodisk sweep der tvangsredder entries der har været PENDING urimeligt længe | Uden dette "dør" en adresse permanent efter det første tabte request — kun en reboot kan rette det | Med 64× flere adresser er sandsynligheden for at ramme denne race markant højere — en enkelt manglende recovery-mekanisme kan langsomt "lamme" en voksende del af registerrummet over tid |
| **Adaptiv per-slave backoff** — konsekvente timeouts fra samme (board, kanal, slave) øger ventetiden eksponentielt (fx 50ms→...→2000ms loft), succeser reducerer den gradvist | Forhindrer at én ikke-svarende slave monopoliserer kø-/bus-kapacitet ved at blive forsøgt igen og igen for intet | Med op til 64 feltbusser er der markant større sandsynlighed for at MINDST én ekstern slave er midlertidigt eller permanent offline — uden backoff spilder det uforholdsmæssigt meget af PLC-sidens samlede kø-kapacitet |
| **BUG-338/340: tidsbegrænset mutex + separat BUS_BUSY-fejlkode** — synkron (CLI/ST Logic direkte kald) og asynkron (baggrundstask) adgang til samme ressource skal koordineres med en mutex der HAR en timeout (aldrig `portMAX_DELAY`), og en fejlkode der adskiller "kunne ikke få adgang" fra "fik adgang, men intet svar" | Uden denne skelnen ser en ren kontentions-fejl identisk ud som en rigtig timeout — umuligt at fejlsøge korrekt | Med flere samtidige TCP-forbindelser (én pr. board) er der reelt MERE samtidighed at koordinere end på PLC'ens enkelt-bus Master #1 i dag |
| **Kooperativ pause, ikke `vTaskSuspend()`** — en task der skal midlertidigt vige (fx for en `mbx scan`) tjekker selv et pause-flag ØVERST i sin løkke, aldrig midt i en transaktion | Et hårdt suspend kan ramme tasken mens den holder en mutex og låse den permanent | Uændret vigtigt — princippet er størrelsesuafhængigt, men fejlen bliver dyrere at ramme jo flere kanaler der er afhængige af samme baggrundstask |

**Implementeringsanbefaling:** start `modbus_expansion.cpp` som en tæt, ordret port af `mb_async.cpp`/`mb_async2.cpp`'s struktur (samme funktionsnavne, samme kommentarer, samme `MB_PENDING_STALE_FACTOR`/`MB_BACKOFF_*`-konstanter) med KUN transport-laget udskiftet (§4.1's Modbus TCP-klient i stedet for UART) og nøglerne udvidet fra `(slave_id, address, fc)` til `(board, kanal, slave_id, address, fc)`. At genskrive kø/cache-logikken fra bunden risikerer at genindføre fejl der allerede er fundet og rettet én gang.

### 5.2 UX-integration — "single pane of glass" (§0's kerneprincip, konkretiseret)

**Krav:** Brugeren skal ALDRIG besøge en anden webside/UI for at drifte expansion-boardet. Al konfiguration sker i PLC'ens eksisterende System-side, som allerede huser tilsvarende hardware-/modul-konfiguration (Ethernet, ACL, RBAC — se `web/system.html` i denne repo for det etablerede kort-baserede UI-mønster).

**Nyt kort i `web/system.html`: "Modbus Expansion Boards"**

1. **Board-liste** (0-8 rækker, "Tilføj board"-knap):
   - Navn/label (fritekst, til visning — fx "Skab 3, RS485-panel")
   - IP-adresse (eller hostname)
   - Management-token (indtastes én gang ved tilføjelse — kopieret fra boardets AP-mode-side, §3.4 — vises maskeret bagefter, samme UX-mønster som andre hemmeligheder i denne repos UI)
   - "Test forbindelse"-knap → autentificeret `GET /api/status` mod boardets management-port, viser firmware-version/uptime/aktive kanaler inline
   - "Opdatér firmware"-knap → uploader en `.bin`-fil via `POST /api/ota`, viser fremgang via periodisk poll af `GET /api/ota/status`, tilbyder reboot når status er `success`
   - "Fjern board"-knap
2. **Pr. board, kanal-liste (dynamisk 1-8, ud fra boardets rapporterede `active_channels`, §2.2.2 — IKKE en hardkodet 8-rækker-liste)**, hver med:
   - Enable/disable-checkbox → del af `PUT /api/channels/{n}/config` ved "Gem"
   - **RS485/RS232-vælger** (radioknapper eller dropdown, §2.2.1) — bestemmer om de resterende felter viser RS485-relevante labels (fx "half-duplex") eller ej; DE/RE er internt, ikke noget brugeren ser
   - Baudrate/parity/stop-bits/timeout — samme dropdown/input-felter som det eksisterende "Modbus Master"-korts config-sektion, genbrugt visuelt
   - Live status: forbundet/ikke-forbundet, seneste fejl, request-tæller (poller `GET /api/channels/{n}` periodisk — samme UX-mønster som dashboardets eksisterende polling af `/api/metrics`)
   - "Gem"-knap pr. kanal (eller én samlet "Gem alle kanaler på dette board") → ét `PUT /api/channels/{n}/config`-kald pr. kanal, bevidst atomisk, aldrig felt-for-felt
3. **Pr. board, "Firewall"-sektion** (kollapset som standard):
   - Viser nuværende IP-allowlist (`GET /api/firewall`) — PLC'ens egen IP er altid til stede efter provisionering (§3.4)
   - "Tilføj IP"/"Fjern IP" → `PUT /api/firewall` med den fulde, opdaterede liste (klient-side validering forhindrer at sende en tom liste, som backend alligevel ville afvise, §4.3)
4. **Ingen "avanceret" sektion udover ovenstående, ingen genvej til at logge ind på et board selv** — bevidst, for at holde ét sted som eneste sandhed.

**Dashboard-integration:** Et nyt kort (eller en udvidelse af det eksisterende "Modbus Master"-kort) der viser alle tilsluttede boards' kanaler kompakt — samme layout-mønster som denne repos øvrige dashboard-kort (`data-card-id`-konventionen, se `web/dashboard.html`).

**CLI/ST Logic:** som beskrevet i §5.1's tabel — brugeren kan fortsat bruge CLI/ST Logic til at læse/skrive gennem expansion-boardenes kanaler, præcis som med den onboard Master #1, blot med to ekstra parametre (board, kanal). Firewall/OTA-styring er bevidst KUN i web-UI'en (§5.2, punkt 3-4), ikke i CLI/ST Logic — det er engangs-/sjældne administrative handlinger, ikke noget et ST-program skal kunne trigge.

**Vigtig konsekvens af dette princip for expansion-board-udvikleren:** boardets API (§4) er IKKE en brugervendt administrations-grænseflade — det er en maskine-til-maskine-kontrakt. Der er derfor **ingen grund til at bygge nogen UI omkring det på expansion-boardet selv** (udover den minimale WiFi-bootstrap-side, §3.4) — al UX-polering (labels, validering, hjælpetekst, visuel status, IP↔navn-mapping) hører hjemme i PLC'ens `web/system.html`, som allerede har den infrastruktur.

**Anbefalet PLC-side arbejde (rækkefølge):**
1. Ny `modbus_expansion.cpp`/`.h` — Modbus TCP-klient(er) mod boardenes data-porte, genbruger kø/cache-koden fra §5.1.1 som skabelon.
2. Ny, simpel `expansion_api_client.cpp`/`.h` — autentificeret REST-klient mod boardenes management-API (§4.2), synkron/lav-frekvent, ingen kø-infrastruktur nødvendig.
3. Nyt "Modbus Expansion Boards"-kort i `web/system.html` (§5.2) — dette ER hele brugerfladen for alle boards, både for opsætning og løbende drift.
4. CLI/ST Logic-eksponering som beskrevet i §5.1's tabel (kun data-planet).
5. Dashboard-kort for driftsovervågning.
6. **Prometheus-eksport:** ny linjer i `api_handler_metrics()`s `build_metrics_text()` for expansion-board-kanalstatistik (total/success/timeout/crc/exception pr. board+kanal), samme navngivningsmønster som de eksisterende `modbus_master_*`-metrics — uden dette forbliver de nye kanaler usynlige for eksisterende Prometheus/Grafana-opsætninger, i modsætning til alt andet i PLC'en.

### 5.3 Governance — eksisterende projektregler der IKKE må springes over for dette arbejde

Fundet ved en kritisk analyse af dette dokument mod PLC-projektets egne, allerede etablerede regler (§0-§0.1's afhængigheds-afsnit dækker KODE-afhængigheder — dette afsnit dækker PROCES-afhængigheder, som er lige så bindende):

- **RBAC/ACL-gating (mangler i designet indtil nu):** ACL/RBAC nævnes flere steder i dette dokument som UI-mønster-forbillede, men INGEN steder er det defineret hvilken RBAC-rolle der kræves for at (a) se/kopiere et boards management-token, (b) redigere firewall-allowlisten (§4.3), eller (c) trigge en OTA-opdatering (§4.2/§5.2). Da OTA er udpeget som systemets mest sikkerhedskritiske enkelt-funktion (§8), skal disse tre handlinger som minimum kræve samme RBAC-niveau som PLC'ens egen systemkonfiguration/OTA — IKKE være tilgængelige for enhver bruger der kan se System-siden. Dette skal fastlægges konkret som en del af Fase 6 (§9), ikke overlades til implementeringstidspunktet.
- **SECURITY_INDEX.md:** CLAUDE.md kræver eksplicit at dette tjekkes FØR enhver web/API/CLI/Modbus-protokol/ST-Logic-ændring. §5's arbejde er præcis en sådan ændring (nye REST-endpoints, ny CLI-kommandofamilie, nye ST-builtins) — implementatoren af §5 SKAL læse `SECURITY_INDEX.md` først, og opdatere den med eventuelle nye fund (fx konsekvenserne af punktet ovenfor), præcis som enhver anden web/API-ændring i denne repo.
- **`docs/manual/`-synkronisering:** CLAUDE.md kræver at den brugervendte manual holdes i sync "i samme commit" som kodeændringer, for ethvert nyt endpoint, ny CLI-kommando, eller ændret default. §5 introducerer alt tre: et nyt UI-kort, en ny CLI-kommandofamilie (`mbx`), og nye ST Logic-builtins (`MBX_*`). Relevante kapitler i `docs/manual/` (bl.a. CLI-referencen og REST API-referencen, jf. `docs/manual/B_REST_API_Reference.md`-mønsteret) skal opdateres i samme commit som koden, ikke som efterfølgende oprydning.
- **CLI-navnekollision — en allerede-kendt faldgrube fra denne repos egen historik:** `normalize_alias()` i `cli_parser.cpp` gav FEAT-408 to selvstændige bugs (manglende regel for "MODBUS-MASTER2"/"MB2" faldt igennem til en case-sensitiv rå streng; en eksisterende regel normaliserede "role" til "ROLES" uventet). Før `mbx`-kommandofamilien implementeres, skal den nye alias-regel eksplicit testes mod eksisterende regler i `normalize_alias()` for uventede kollisioner — ikke antages fri af den type fejl bare fordi navnet er nyt.

---

## 6. Ydelse & timing-budgetter

| Parameter | Mål | Begrundelse |
|---|---|---|
| Modbus TCP-transaktion (data-plan, enkelt kanal) | < 10ms overhead udover selve Modbus-transaktionens egen timeout | Kun MBAP-header (7 bytes) + PDU, ingen JSON — samme størrelsesorden som PLC'ens eksisterende RTU-transaktioner plus én TCP-roundtrip på lokalt LAN |
| 8 kanaler samtidig, ét board | Ingen ekstra overhead ved flere kanaler — hver har egen TCP-forbindelse + egen kanal-task, kører fuldt parallelt | Garanteret af per-kanal-task-arkitekturen (§3.2) — 8 samtidige forbindelser er ikke dyrere end 1 |
| Op til 8 boards samtidig | Skalerer lineært, ingen delt flaskehals mellem boards | Hvert board er en uafhængig IP-destination — PLC-sidens kø/cache-lag (§5.1.1) håndterer allerede N uafhængige "bus-enheder" i sit design |
| Management-API-kald (REST, §4.2) | < 100ms | HTTP+JSON-overhead er accepteret her, fordi kaldene er lavfrekvente (config-ændringer, periodisk status-poll) — modsat data-planet, hvor JSON bevidst er fravalgt (§1.3) |
| Board-status-poll interval (PLC-side, `GET /api/status`) | 5-10 sekunder | Hyppig nok til hurtig fejl-detektion, sjælden nok til ikke at belaste netværket |
| OTA-upload | Ingen hård tidsgrænse — typisk 10-60 sekunder for en firmware-binary på lokalt LAN | Sjælden, brugerinitieret handling — UI'en (§5.2) viser fremgang, ikke en fast deadline |
| Kanal-task stack | ≥ 4096 bytes (samme som PLC'ens `MB_ASYNC_TASK_STACK`) | Matcher allerede-afprøvet dimensionering fra denne kodebase |

---

## 7. Fejlhåndtering & robusthed

- **Expansion-board genstart:** Statusløst design — PLC'en registrerer manglende svar fra `GET /api/status`, markerer boardets kanaler "unreachable" (`mb_error_code_t = 9`), og genskriver kanal-konfigurationen (`PUT /api/channels/{n}/config`) til alle boardets aktive kanaler når boardet svarer igen (ingen antagelse om at boardet husker sin "session" — kun dens PERSISTEREDE flash-config, som er dens egen sag).
- **Netværkstab:** Samme mønster — PLC-siden markerer et boards kanaler "unreachable" efter N mistede status-polls, retry med backoff (genbruger samme adaptive-backoff-idé som §5.1.1's per-slave-backoff, blot anvendt på board-niveau i stedet for slave-niveau).
- **Delvis kanal-fejl:** Én kanals fysiske RS485-problem (fx en fysisk afbrudt bus) må ALDRIG blokere de andre 7 kanaler på samme board, og slet ikke kanaler på andre boards — garanteret af den fuldstændigt adskilte per-kanal-task-arkitektur (§3.2) og hvert boards uafhængige IP (§1.4).
- **Fejlet OTA:** en fejlet/korrupt firmware-upload må ALDRIG bricke boardet — standard "dual-partition + rollback"-mønster (skriv til inaktiv partition, verificér før aktivering, behold evnen til at falde tilbage til forrige fungerende partition ved boot-fejl efter en opdatering), samme princip som PLC'ens egen `src/ota_handler.cpp`.
- **Watchdog på begge sider:** Se §3.3.

---

## 8. Sikkerhedsovervejelser

- **Management-planet (REST, §4.2)** er beskyttet af Bearer-token-auth fra dag 1 (§4.4) — token udstedes under provisionering (§3.4) og skal behandles som en hemmelighed på niveau med PLC'ens egne ACL-adgangskoder i PLC-sidens konfigurationslager.
- **Data-planet (Modbus TCP, §4.1)** har fortsat ingen protokol-auth (§4.4) — beskyttet af netværkssegmentering OG et board-lokalt IP-allowlist (§4.3), som forsvar-i-dybden mod netop det scenarie hvor segmenteringen fejler eller omgås i felten (en almindelig fejlkilde i virkelige installationer).
- WiFi-provisioning-AP'et (§3.4) er stadig den eneste reelt "åbne" angrebsflade (et fabriksnyt board før første opsætning) — en simpel adgangskode (ikke åbent netværk) og en **automatisk timeout** ud af AP-mode (fx 10 minutter uden modtaget konfiguration) anbefales.
- **Forbindelses-begrænsning** på Modbus TCP-portene (§4.4) bevares som robusthedsforanstaltning mod utilsigtet socket-pool-udmattelse.
- **OTA (§4.2) er den mest sikkerhedskritiske enkelt-funktion** — en kompromitteret Bearer-token giver ikke bare adgang til config, men til at erstatte hele boardets firmware. Der er ingen ekstra beskyttelse udover selve token-auth'en for dette specifikke endpoint i v1 — overvej signeret firmware (verificér en signatur før aktivering af den uploadede binary) som en fremtidig hærdning, hvis boardet forventes drevet i miljøer udenfor et fuldt tillidsforhold til PLC-siden.
- **Token-rotation:** da tokenet er den centrale hemmelighed for hele management-planet (config + firewall + OTA), bør det kunne roteres/regenereres via samme fysiske AP-mode-mekanisme som resten af provisioneringen (§3.4), ikke kun sættes én gang for altid ved fabriksopsætning.

---

## 9. Implementeringsfaser (til expansion-board-udvikleren)

**Ét expansion-board først, derefter PLC-integration, derefter skalering til flere boards — ikke omvendt.** Cache/kø-lektionerne i §5.1.1 er nemmest at bygge korrekt mod ét board med kendt, stabil adfærd; multi-board-understøttelse (§1.4) tilføjes bagefter uden at røre selve kø-designet, da hvert board blot er "endnu en IP" set fra PLC-siden.

**Anbefaling fra PLC-projektets egen erfaring:** før en let, append-only bug/feature-log (samme ånd som PLC-repoets `BUGS_INDEX.md`) i det nye repo fra Fase 1 — én kort entry pr. væsentlig beslutning eller fejl, inkl. HVORFOR, ikke kun HVAD. Det var netop en sådan log der gjorde det muligt præcist at rekonstruere FEAT-408s fulde fejlfindingsforløb og begrunde rollback-beslutningen (§0) måneder senere.

1. **Fase 1 — Hardware-bring-up:** Gateway-MCU + WiFi + ÉN UART-expander-chip (2-4 kanaler) på breadboard/prototype. Verificér SPI-kommunikation til expander-chippen, verificér én RS485-kanal kan tale Modbus RTU til en kendt slave (brug fx PLC'ens egen `mb scan`/`mb read`-kommandoer som referenceimplementering af "hvordan ser en korrekt Modbus RTU-master-transaktion ud").
2. **Fase 2 — Alle 8 kanaler (ét board):** Udvid til fuld hardware (2× expander-chip), verificér alle 8 kanaler kan køre SAMTIDIGT uden krydsforstyrrelse (parallel test på alle 8 mod 8 forskellige test-busser, eller mod samme testbus-adresse-range på isolerede busser). **Verificér også auto-detektionen (§2.2.2)** med en bevidst delvist bestykket testopstilling (fx kun 2 af 8 positioner monteret) — bekræft `active_channels` rapporterer præcis 2, og at `GET`/`PUT` mod kanal 3-8 konsekvent svarer `404`, ikke en falsk "0 fejl"-status for ikke-eksisterende kanaler.
3. **Fase 3 — Provisioning:** Implementér WiFi-bootstrap + token-udstedelse + firewall-seed (§3.4). Test at et fabriksnyt board kan bringes på produktionsnetværket, og at management-API'et bagefter kræver det udstedte token og afviser alt andet.
4. **Fase 4 — Data-plan (ét board):** Implementér Modbus TCP-serveren for de 8 data-porte (§4.1). Test med et standard Modbus TCP-testværktøj (fx `mbpoll`) mod en kendt fysisk slave pr. kanal.
5. **Fase 5 — Management-API (ét board):** Implementér REST-endpoints for status/kanal-config/firewall/OTA (§4.2). Test med `curl`/Postman: skriv kanal-config via `PUT`, læs den tilbage via `GET`, bekræft identisk; test firewall-endpointet afviser en tom liste; test en gyldig OTA-upload aktiveres korrekt efter reboot, og en korrupt upload afvises uden at bricke boardet.
6. **Fase 6 — PLC-side integration (i denne repo, separat feature), ÉT board:** Byg `modbus_expansion.cpp` (data) og `expansion_api_client.cpp` (management) som beskrevet i §5.1, med kø/cache-designet fra §5.1.1 genbrugt tæt efter `mb_async.cpp`/`mb_async2.cpp`'s struktur (ikke genopfundet fra bunden) — test grundigt mod ÉT board først, inkl. bevidst fejl-injektion (afbryd en slave midt i drift) for at verificere backoff/PENDING-recovery reelt virker som i originalen. Byg derefter det nye "Modbus Expansion Board"-kort i `web/system.html` (§5.2), stadig for ét board.
7. **Fase 7 — Multi-board (op til 8):** Udvid PLC-sidens konfiguration og UI (§1.4/§5.2) til en liste af boards. Verificér kø/cache-koden fra Fase 6 kræver INGEN ændring udover at nøglerne udvides med et board-felt (§5.1.1's anbefaling) — hvis det gør, er det et signal om at board-dimensionen blev "hardkodet" et sted i Fase 6 og bør rettes der, ikke omgås her.
8. **Fase 8 — Robusthedstest (fuld skala):** Afbryd netværksforbindelsen til ét board midt i drift, verificér de øvrige boards er upåvirkede og PLC-siden håndterer det tabte board gracefuldt; kortslut/afbryd én RS485-kanal, verificér de andre 7 på samme board (og alle kanaler på andre boards) upåvirkede; genstart et board midt i drift, verificér automatisk re-konfiguration fra PLC'en; forsøg at nå data-portene fra en IP UDENFOR allowlisten, bekræft afvisning; kør alle op til 64 kanaler samtidig ved realistisk pollingfrekvens i en længere periode (jf. §10's 24-timers-kriterium) og verificér ingen af §5.1.1's kendte fejlklasser (voksende PENDING-lager, ubegrænset backoff-akkumulering, mutex-sult) opstår under vedvarende belastning.

---

## 10. Acceptance-kriterier (test før overlevering til PLC-integration)

- [ ] WiFi-provisioning (§3.4): et fabriksnyt board kan sættes på et produktionsnetværk, få udstedt et management-token og få seedet firewall-allowlisten, udelukkende via AP-mode-flowet, uden andre lokale indtastninger.
- [ ] Kanal-auto-detektion (§2.2.2): på mindst to forskellige hardware-varianter (fx 2-kanals og 8-kanals bestykning, samme firmware-binary på begge) rapporterer `GET /api/status` det korrekte `active_channels`-tal, og kald mod ikke-eksisterende kanal-indekser svarer konsekvent `404`.
- [ ] Alle 8 kanaler kan konfigureres uafhængigt (forskellig baudrate/parity pr. kanal) udelukkende via `PUT /api/channels/{n}/config` fra en autentificeret, ekstern klient (simulerer PLC'en), og verificeres ved `GET` tilbage.
- [ ] Management-API'et afviser ALLE kald uden gyldigt Bearer-token med `401`, på tværs af samtlige endpoints (§4.2).
- [ ] Alle 8 Modbus TCP data-porte (502-509) svarer korrekt til en standard Modbus TCP-klient, med korrekt FC01-FC06/FC16-semantik og MBAP-framing.
- [ ] Alle 8 kanaler kan udføre en Modbus RTU-transaktion SAMTIDIGT (målt: start alle 8 indenfor samme 10ms-vindue via 8 parallelle TCP-forbindelser, verificér alle svarer indenfor deres respektive timeout uden krydskontaminering af data).
- [ ] En TCP-forbindelse til en data-plan-port (502-509) fra en IP UDENFOR firewall-allowlisten afvises ved `accept()`, uden at nå Modbus-parsing; et forsøg på at `PUT` en tom allowlist afvises med en tydelig fejl.
- [ ] En gyldig firmware uploadet via `POST /api/ota` aktiveres korrekt efter `POST /api/reboot`; en korrupt/ugyldig upload afvises og boardet forbliver funktionsdygtigt på den hidtidige firmware.
- [ ] Fysisk afbrydelse af én kanals RS485-bus midt i drift påvirker IKKE de andre 7 kanaler (0 fejl på dem i samme periode).
- [ ] Expansion-boardet overlever 24 timers kontinuerlig drift ved høj pollingfrekvens (alle 8 kanaler, 100ms mellem forespørgsler) uden heap-fragmentering/hukommelseslæk (mål `heap_free` ved start og efter 24t, forskellen bør være < 5%).
- [ ] Genstart af expansion-boardet midt i drift: en ekstern klient (simulerer PLC'en) opdager det indenfor 2× status-poll-intervallet, og boardet accepterer fornyet kanal-konfiguration korrekt uden manuel indgriben.
- [ ] `GET /api/status` svarer indenfor 100ms under fuld belastning på alle 8 data-kanaler.
- [ ] Ingen vej findes til at ændre kanal-konfiguration, firewall-regler eller firmware UDEN om det autentificerede management-API'et (§4.2) eller WiFi-provisioning-flowet (§3.4) — bekræfter "ingen egen driftsbrugerflade"-princippet, §0/§3.4/§5.2.

---

## Appendiks A: Filer i denne repo der er nyttige som reference

| Fil | Hvad den viser |
|---|---|
| `src/modbus_master.cpp` | Fuld, hærdet Modbus RTU master-protokol-implementering (CRC, framing, to-fase-timeout, exception-håndtering). PDU-opbygningen (FC01-FC06/FC16 request/response-formatering) er direkte genbrugelig som reference for BÅDE expansion-boardets RTU-side (§3.2) OG dens Modbus TCP-server (§4.1) — kun rammen udenom (RTU CRC+adresse vs. TCP MBAP) er forskellig, PDU'en er identisk |
| `src/mb_async.cpp` | Prioritetskø + cache + adaptiv backoff-design — reference for PLC-side genbrug af data-planet (§5.1.1) |
| `include/types.h` (`mb_error_code_t`, `modbus_master_config_t`) | De datastrukturer kontrakten i §4 er bevidst designet til at matche |
| `src/api_handlers.cpp` | PLC'ens etablerede REST-API-stil (JSON, auth-makroer, fejl-response-format) — **master-reference for hele expansion-boardets management-API** (§4.2-4.4: endpoints, Bearer-token-mønster, fejlformat), ikke kun provisioning |
| `src/ota_handler.cpp` | PLC'ens egen OTA-implementering (rå binær body, dual-partition, verifikation før aktivering) — direkte skabelon for expansion-boardets `POST /api/ota` (§4.2/§7) |
| `include/constants.h` (`CONFIG_SCHEMA_VERSION`) + `src/config_load.cpp` (migrationsblok) | Reference-implementering af schema-versionering og engangs-migration ved boot — mønsteret §3.5 anbefaler genbrugt for expansion-boardets `config.cpp` |
| `src/watchdog_monitor.cpp` | Reset-årsag-persistering til RTC/NVS og software-watchdog-mønster — reference for §3.3/§3.6 |
| `web/system.html` | Det eksisterende kort-baserede UI-mønster (Ethernet/ACL/RBAC-kort) det nye "Modbus Expansion Board"-kort (§5.2) skal følge visuelt og strukturelt |
| `BUGS_INDEX.md` (søg "FEAT-408") | Den fulde historik, fejlfinding og root-cause-analyse der begrunder hele dette design — samtidig selve eksemplet på den løbende bug/feature-log §9 anbefaler at føre i det nye repo |

**Ekstern reference (ikke i denne repo):** Modbus TCP/MBAP-header-formatet er fastlagt af Modbus Organization's officielle specifikation ("MODBUS Messaging on TCP/IP Implementation Guide") — 7-byte header (Transaction ID, Protocol ID, Length, Unit ID) foran den uændrede PDU. Adskillige gratis, MIT/BSD-licenserede Modbus TCP-biblioteker findes til både ESP32 og STM32 hvis udvikleren foretrækker det frem for at skrive MBAP-parsing fra bunden.
