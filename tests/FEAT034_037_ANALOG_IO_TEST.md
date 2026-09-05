# FEAT-034/035/036/037: Analog I/O — Testprocedure

**Dato:** 2026-09-03
**Build:** v7.9.10.2
**Udføres af:** Bruger (kræver fysisk adgang til ES32D26-enheden — kan ikke automatiseres fra sandkassen)
**Forudsætning:** v7.9.10.2 er flashet

---

## ⚠️ Test 0 — Migrations-sikkerhed (udfør FØRST, før noget andet)

Denne feature tilføjede felter til den gemte config (schema 19→20). Efter BUG-339 (v7.9.9.0's tilsvarende fejl nulstillede hele configen inkl. WiFi) er dette den vigtigste test — udføres før noget som helst andet berøres.

- [ ] **0.1** Noter jeres nuværende WiFi SSID, RBAC-brugere (`show config rbac` el. lign.) og evt. dashboard-layout FØR reboot — som backup hvis noget skulle gå galt.
- [ ] **0.2** Flash v7.9.10.2, reboot.
- [ ] **0.3** `show version` → bekræft `7.9.10.2`.
- [ ] **0.4** `show wifi` → bekræft SSID og IP er **uændret** fra før flash.
- [ ] **0.5** `show config rbac` (eller tilsvarende) → bekræft brugere/roller er **uændrede**.
- [ ] **0.6** `save` → tjek output-linjen `Schema: v20 CRC: 0x.... Size: N bytes` — noter `N` (bruges i Test 8).
- [ ] **0.7** `reboot` én gang til, gentag 0.3-0.5 → bekræfter at schema 20 nu er den STABILE, gemte tilstand (ikke kun en migreret RAM-kopi).

**Hvis NOGET af dette fejler:** stop og rapportér straks — flash IKKE videre, og undlad at køre `save` igen (for at bevare evt. gendannelsesmuligheder).

---

## Test 1 — Boot og grundlæggende status

- [ ] **1.1** Seriel boot-log viser `Analog I/O: OK` mellem `GPIO: OK` og `UART: OK`.
- [ ] **1.2** `show analog` kører uden fejl og viser alle 10 kanaler (Vi1-4, Ii1-4, AO1-2), alle `enabled=off` som udgangspunkt.
- [ ] **1.3** Registrene i outputtet matcher: Vi1-4 → HR 0-7, Ii1-4 → HR 8-15, AO1-2 → HR 16-17.

## Test 2 — Spændingsindgang (brug Vi2 eller Vi4 — ADC1, upåvirket af WiFi)

- [ ] **2.1** `set analog vi2 enabled on` → bekræftelse vises.
- [ ] **2.2** `save` + `reboot` (register-allokering sker ved boot).
- [ ] **2.3** Påfør en kendt spænding (multimeter) på Vi2, fx 5,00V.
- [ ] **2.4** `show analog` → noter `raw` (mV) for Vi2.
- [ ] **2.5** Beregn korrekt scale: `scale = 500 / raw_mv` (500 = 5,00V i ×100-format). Eksempel: hvis raw=1650mV ved 5V, så `scale = 500/1650 ≈ 0.303`.
- [ ] **2.6** `set analog vi2 scale 0.303` (brug jeres beregnede værdi).
- [ ] **2.7** `show analog` → værdien for Vi2 bør nu vise ~5,00V.
- [ ] **2.8** Skift til en anden kendt spænding (fx 8,00V) og bekræft værdien følger proportionalt — hvis ikke, er `offset` også nødvendig (nulpunktsfejl).

## Test 3 — Strømindgang (Ii1-4)

- [ ] **3.1** Samme procedure som Test 2, men med en kendt strømkilde (4-20mA) på fx Ii1.
- [ ] **3.2** Ved 4mA bør raw_mv være tæt på 0 (loopets nulpunkt) — hvis ikke, justér `offset`.
- [ ] **3.3** Ved 20mA bør værdien vise tæt på 20,00mA efter kalibrering.

## Test 4 — ADC2/WiFi-interaktion (Vi1 eller Vi3)

- [ ] **4.1** `set analog vi1 enabled on` + `save` + `reboot`.
- [ ] **4.2** Med WiFi **tilsluttet**: `show analog` → Vi1 skal vise "WiFi aktiv, ADC2 utilgængelig" og IKKE opdatere raw/værdi.
- [ ] **4.3** `GET /api/analog` → Vi1's `wifi_blocked` skal være `true`, `raw_mv`/`value` skal være `-1`.
- [ ] **4.4** (Valgfrit, kræver Ethernet-only opsætning) Med WiFi **slukket** (kun Ethernet): Vi1 skal læse normalt.

## Test 5 — Analog udgang (AO1 eller AO2)

- [ ] **5.1** `set analog ao1 enabled on` + `save` + `reboot`.
- [ ] **5.2** Fra dashboardet: indtast en værdi (fx 5.00) i AO1-feltet, klik "Sæt".
- [ ] **5.3** Mål den faktiske spænding på AO1-udgangen med multimeter.
- [ ] **5.4** Sammenlign mod forventet — kalibrér `scale`/`offset` for AO1 hvis nødvendigt (samme metode som Test 2, men omvendt: juster til output MATCHER setpoint).
- [ ] **5.5** DAC er 8-bit (256 trin) — bekræft opløsningen er acceptabel til jeres formål (~0,04V trin ved 0-10V fuld skala).
- [ ] **5.6** Skift `set ao1 mode current` → bekræft advarslen om at tjekke kalibrering vises, og at output-området nu er 4-20mA (kræver ny kalibrering, se Test 3's metode).

## Test 6 — Dashboard

- [ ] **6.1** Med alle kanaler `enabled=off`: "Analog I/O"-kortet er skjult.
- [ ] **6.2** Med mindst én kanal aktiveret: kortet vises med korrekt værdi (opdateres hvert 3. sekund via `/api/metrics`).
- [ ] **6.3** AO-setpoint-feltet virker fra dashboardet (allerede testet i 5.2, bekræft blot UI-flowet er problemfrit).

## Test 7 — REST API (valgfrit, hvis curl/Postman er til rådighed)

- [ ] **7.1** `GET /api/analog` → gyldig JSON, alle felter til stede.
- [ ] **7.2** `POST /api/analog` med `{"channel":"vi2","scale":0.303}` → `{"status":"ok",...}`.
- [ ] **7.3** `POST /api/analog` med ukendt kanal → HTTP 400 med fejlbesked.

## Test 8 — Ressourceforbrug

- [ ] **8.1** `sizeof(PersistConfig)` fra Test 0.6 — bekræft væksten er som forventet (~110 bytes mere end før v7.9.10.0, altså tæt på den tidligere målte størrelse + 110).
- [ ] **8.2** `show status` el. lign. → bekræft ledig heap ikke er faldet unaturligt meget efter aktivering af flere analoge kanaler.

---

## Rapportér tilbage

For hver fejlet test: hvilket trin, hvad der skete i stedet for det forventede, og gerne rå CLI/API-output. Test 0 er vigtigst — meld status på den uanset udfald.
