# PLC-integrations-manual: HypervisionPLC Extension Board

Denne manual dokumenterer expansion-boardets **fulde, faktisk implementerede** grænseflade, som den ser ud efter v0.12.0 (build 0015) — til brug når PLC-siden (`Modbus_server_slave_ESP32`-repoet, `modbus_expansion.cpp`/`expansion_api_client.cpp`, §5 i [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md)) skal implementeres. Alt heri er verificeret mod rigtig hardware, ikke kun designet — se [CHANGELOG.md](CHANGELOG.md) for de enkelte live-verifikationer.

**Forskel fra designdokumentet:** [EXPANSION_BOARD_DESIGN.md](EXPANSION_BOARD_DESIGN.md) beskriver den fulde, oprindelige vision (op til 8 kanaler, OTA, osv.). Denne manual beskriver kun det der **rent faktisk er bygget og testet** i denne repo lige nu — Variant A, 2 kanaler. Er der uoverensstemmelse, er DENNE fil den autoritative kilde for hvad et board faktisk gør i dag.

---

## Indhold

1. [Arkitektur i korte træk](#1-arkitektur-i-korte-træk)
2. [Første opsætning (provisionering)](#2-første-opsætning-provisionering)
3. [Modbus TCP — data-planet (høj-frekvent drift)](#3-modbus-tcp--data-planet-høj-frekvent-drift)
4. [REST management-API (port 8080)](#4-rest-management-api-port-8080)
5. [Fejlkoder-reference](#5-fejlkoder-reference)
6. [Anbefalet PLC-side-integrationsflow](#6-anbefalet-plc-side-integrationsflow)
7. [Kendte begrænsninger lige nu](#7-kendte-begrænsninger-lige-nu)

---

## 1. Arkitektur i korte træk

- Boardet har **2 faste kanaler** (Variant A, §2.0): kanal A (n=1) og kanal B (n=2), hver sin fysiske UART (RS485 pt., RS232-mode findes i config men er ikke hardware-testet).
- **To adskilte netværksgrænseflader**, forskellige trust-niveauer:
  - **Modbus TCP (port 502/503)** — data-planet, PLC'ens høj-frekvente driftsvej. INGEN protokol-auth (Modbus-standard-begrænsning) — beskyttet af ét fast `plc_ip`-permit (§4.3, se afsnit 3).
  - **REST/JSON (port 8080)** — management-planet. Kræver Bearer-token ELLER Basic Auth på ALLE endpoints uden undtagelse. IKKE begrænset af `plc_ip`-permittet — bevidst, så en fejlkonfigureret/skiftet PLC-IP aldrig kan spærre administratoren ude (se §4.3).
- Boardet har INGEN egen driftsbrugerflade. Al løbende konfiguration sker fra PLC-siden via REST-API'et. Den serielle CLI (USB, fysisk adgang) er UDELUKKENDE til første netværksopsætning + nødadgang.

---

## 2. Første opsætning (provisionering)

Sker over **seriel CLI (USB), aldrig over netværket** — dette er en bevidst, ufravigelig sikkerhedsbeslutning (§3.4). PLC-siden skal ikke og kan ikke automatisere dette skridt; det er en engangs-installatør-opgave pr. board.

Minimal kommandosekvens (se `help` for alle kommandoer):

```
wifi ssid "MitNetvaerk"
wifi pass "MitPassword123"
plc ip 10.1.1.153        # PLC'ens IP - se afsnit 3, kritisk for data-planet
connect
```

Efter `connect` lykkes:
- Boardet forbinder til WiFi, starter REST-API'et (port 8080) og Modbus TCP-serveren (port 502/503).
- Et management-API-token genereres automatisk (kun ved ALLERFØRSTE `connect`) og vises på skærmen — **dette er den værdi PLC-siden skal bruge som Bearer-token** (se afsnit 4.1). Det kan altid genses med `status`.
- `rest user`/`rest pass` kan sættes for at bruge Basic Auth i stedet for/ved siden af tokenet.

**Genopsætning/fejlsøgning:** `show`/`status` viser AL config i klartekst (adgangskoder, token) — dette er en bevidst undtagelse, kun gældende for denne serielle CLI (fysisk USB-adgang = allerede tillidsgrænsen). REST-API'et returnerer aldrig disse værdier.

---

## 3. Modbus TCP — data-planet (høj-frekvent drift)

Dette er den vej PLC'en skal bruge til NORMAL, høj-frekvent Modbus-drift — REST-API'ets `/read`/`/write` (afsnit 4.5) er kun til ad-hoc diagnose.

### 3.1 Port → kanal-mapping

| Port | Kanal |
|---|---|
| 502 | A (n=1) |
| 503 | B (n=2) |

### 3.2 Forbindelse og framing

- Standard Modbus TCP (MBAP-header): Transaction ID (ekko), Protocol ID (altid 0), Length, **Unit ID = den fysiske RTU-slaves adresse** (1-247) — IKKE en dummy-værdi. PLC'en sætter Unit ID til den slave den faktisk vil tale med på den fysiske bus bag den valgte kanal.
- PDU'en (function code + data) passerer **uændret** igennem — ingen registerombytning, ingen indirection. Adresser/values i requestet er nøjagtigt hvad der sendes til RTU-slaven.
- **Åbn ÉN vedvarende TCP-forbindelse pr. kanal og genbrug den** til alle transaktioner. Boardets TCP-server har ét sekventielt kø pr. kanal — mange kortvarige forbindelser (åbn/luk pr. request) er ikke understøttet godt og bør undgås (se BUGS.md v0.9.0.1 for baggrund; roden er rettet, men mange samtidige korte forbindelser er stadig ineffektivt).
- Understøttede function codes: **FC01, FC02, FC03, FC04, FC05, FC06, FC16**. Andet giver en gateway-exception (se nedenfor).

### 3.3 Fejlhåndtering — to forskellige slags "fejl"

1. **En Modbus-exception FRA slaven selv** (høj bit sat i function code-byten, fx `0x83` for en FC03-fejl) — relayes UÆNDRET. Det er slavens eget svar, ikke boardets.
2. **En gateway-exception FRA BOARDET** (samme byte-format, men genereret af boardet selv når det IKKE kunne gennemføre transaktionen):
   - **`0x0A` (Gateway Path Unavailable)** — kanalen er util-tilgængelig: deaktiveret (`enabled:false`, se afsnit 4.3), ugyldig adresse/PDU, eller kanalen optaget.
   - **`0x0B` (Gateway Target Device Failed to Respond)** — slaven svarede slet ikke (timeout) eller svarede forkert (CRC-fejl, forkert adresse i svaret).

PLC'ens Modbus TCP-klient bør derfor tolke ETHVERT svar med høj bit sat som en exception (standard Modbus-adfærd) — der er ingen måde at skelne "slavens egen exception" fra "boardets gateway-exception" i selve PDU'en (det er med vilje — begge er gyldige Modbus-exceptions for en standard-klient). Skal de skelnes, brug i stedet de diagnostiske REST-endpoints (afsnit 4.5), som eksplicit rapporterer forskellen.

### 3.4 `plc_ip`-permittet (§4.3)

- Boardet accepterer KUN TCP-forbindelser til port 502/503 fra ÉN, fast IP — den der blev sat med `plc ip <a.b.c.d>` under provisionering (afsnit 2).
- **Fejl-lukket, ikke fejl-åbent:** er `plc_ip` slet ikke sat, er data-planet lukket for ALLE.
- Afvises en forbindelse, sker det ved `accept()` — FØR noget Modbus-indhold overhovedet parses. Ingen respons sendes, forbindelsen lukkes blot.
- **Vigtigt for PLC-siden:** hvis PLC'en nogensinde skifter IP (ny DHCP-lease, ny netværkskonfiguration), SKAL boardets `plc_ip` opdateres via den serielle CLI igen (`plc ip <ny-ip>`, `save`). Der er intet REST-endpoint til dette — det er bevidst uden for netværkets rækkevidde (§4.3's begrundelse: en fejlkonfigureret/kompromitteret PLC må ikke selv kunne omdirigere data-planet-adgangen).
- Denne begrænsning gælder KUN port 502/503 — REST-API'et (port 8080) er upåvirket, uanset `plc_ip`.

---

## 4. REST management-API (port 8080)

Alle endpoints kræver autentificering (afsnit 4.1). Alle svar er `application/json`.

### 4.1 Autentificering

To metoder, konfigureret på boardet via `rest auth token|basic|both` (CLI):

- **Bearer-token:** `Authorization: Bearer <token>` — tokenet fra provisionering (afsnit 2). Anbefalet til PLC'en (maskinvenligt, ingen menneskelig indblanding nødvendig).
- **Basic Auth:** `Authorization: Basic <base64(bruger:kode)>` — sat via `rest user`/`rest pass`.

Manglende/forkert auth → `401`:
```json
{"ok":false,"error":"unauthorized","message":"Manglende eller ugyldig Authorization-header"}
```
Præsenteres en metode der er eksplicit slået fra (`rest auth`-kommandoen), er beskeden i stedet: `"Denne auth-metode er slaaet fra (se 'rest auth' i den serielle CLI)"`.

### 4.2 `GET /api/status`

```json
{
  "api_version": 1,
  "fw_version": "0.12.0",
  "fw_build": "0015",
  "uptime_s": 86412,
  "heap_free_bytes": 221856,
  "active_channels": 2,
  "provisioned": true,
  "wifi": {"connected": true, "ip": "10.1.1.229", "rssi_dbm": -62}
}
```
`wifi`-objektet er kun `{"connected":false}` hvis ikke forbundet (`ip`/`rssi_dbm` udelades da). `api_version` er en separat protokol-kontrakt-version (bumpes KUN ved brydende ændringer i selve API'et) — PLC-siden bør logge en advarsel, ikke fejle stille, hvis denne ikke matcher hvad klienten er skrevet imod.

### 4.3 `GET /api/channels` og `GET /api/channels/{n}`

`n` er **1-baseret**: 1=kanal A, 2=kanal B. `n` udenfor `1..active_channels` giver `404`.

`GET /api/channels` returnerer et JSON-array af nøjagtigt samme objekt-form som `GET /api/channels/{n}`, ét pr. aktiv kanal:

```json
{
  "channel": 1,
  "enabled": true,
  "mode": "rs485",
  "baudrate": 9600,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0,
  "status": "ok",
  "total_requests": 142,
  "successful_requests": 140,
  "timeout_errors": 2,
  "crc_errors": 0,
  "exception_errors": 0,
  "last_error_slave_id": 12,
  "last_error_address": 40010,
  "last_error_type": 1,
  "last_error_at_uptime_s": 86112
}
```
- `mode`: `"rs485"` eller `"rs232"`. `parity`: `"none"`/`"even"`/`"odd"`.
- `status`: `"disabled"` (enabled=false, uanset statistik) → `"error"` (seneste transaktion fejlede) → `"ok"`.
- Statistikken er **runtime-only** — nulstilles ved reboot, IKKE persisteret. Der findes intet reset-endpoint endnu (planlagt, ikke bygget — se afsnit 7).
- `last_error_type` er en `mb_error_code_t`-værdi — se afsnit 5.

### 4.4 `PUT /api/channels/{n}/config`

**Atomisk — ALLE felter er påkrævet i ét kald.** Mangler blot ét, eller er ét ugyldigt, afvises HELE requestet (`400`) uden nogen sideeffekt (uændret config).

Request-body (samme felter som i GET's svar, minus statistikken):
```json
{
  "enabled": true,
  "mode": "rs485",
  "baudrate": 19200,
  "parity": "none",
  "stop_bits": 1,
  "timeout_ms": 500,
  "inter_frame_delay_ms": 0
}
```
- Gyldige baudrates: 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200.
- `stop_bits`: 1 eller 2. `timeout_ms`: > 0.
- Svaret er den nye config + statistikken (samme form som `GET /api/channels/{n}`), `200` ved succes.
- Anvendes LIVE med det samme (ingen reboot nødvendig) og persisteres til flash — overlever en genstart. En igangværende transaktion på kanalen fuldføres altid på den GAMLE config før omkobling.
- Sæt `enabled:false` for at deaktivere en kanal helt — Modbus TCP-forespørgsler til den kanal afvises derefter øjeblikkeligt med gateway-exception `0x0A` (afsnit 3.3), uden at røre UART'en.

### 4.5 `POST /api/channels/{n}/read` — diagnostisk læsning

**Til ad-hoc test/fejlsøgning — IKKE beregnet til høj-frekvent drift** (brug Modbus TCP, afsnit 3, til det). Udfører ÉN synkron Modbus RTU-transaktion og returnerer resultatet direkte, uden at PLC'en selv skal åbne en Modbus TCP-forbindelse.

Request:
```json
{"function_code": 3, "slave_id": 9, "address": 0, "quantity": 2}
```
- `function_code`: 1 (Read Coils), 2 (Read Discrete Inputs), 3 (Read Holding Registers) eller 4 (Read Input Registers).
- `slave_id`: 1-247. `address`: 0-65535. `quantity`: 1-2000 (Modbus-spec'ens egne grænser pr. function code håndhæves af kanal-laget — en for stor `quantity` afvises som en kanal-fejl, ikke en synlig valideringsfejl her).

Svar ved succes (FC03/04 — 16-bit register-værdier):
```json
{"ok": true, "function_code": 3, "slave_id": 9, "address": 0, "quantity": 2, "values": [17942, 0]}
```
Svar ved succes (FC01/02 — bit-udpakkede 0/1-værdier, én pr. adresse):
```json
{"ok": true, "function_code": 1, "slave_id": 9, "address": 0, "quantity": 4, "values": [1, 0, 1, 0]}
```
Svar ved en Modbus-exception FRA SLAVEN (HTTP `200` — kaldet lykkedes, indholdet rapporterer fejlen):
```json
{"ok": false, "error": "modbus_exception", "slave_id": 9, "exception_code": 2}
```
Svar ved en kanal-/transportfejl (HTTP `502`):
```json
{"ok": false, "error_code": 1, "error": "channel_error", "message": "..."}
```

### 4.6 `POST /api/channels/{n}/write` — diagnostisk skrivning

Samme princip som `/read`. `function_code`: 5 (Write Single Coil), 6 (Write Single Register) eller 16 (Write Multiple Registers).

FC05 (coil — boolsk):
```json
{"function_code": 5, "slave_id": 9, "address": 3, "value": true}
```
FC06 (ét register):
```json
{"function_code": 6, "slave_id": 9, "address": 10, "value": 1234}
```
FC16 (flere registre, maks 32 pr. kald):
```json
{"function_code": 16, "slave_id": 9, "address": 0, "values": [1, 2, 3]}
```
Svar ved succes:
```json
{"ok": true, "function_code": 6, "slave_id": 9, "address": 10, "quantity": 1}
```
Exception-/fejl-svar: samme form som `/read` (afsnit 4.5).

**Bemærk:** ikke alle slave-devices understøtter alle function codes — nogle svarer med en ægte Modbus-exception (`Illegal Function`, kode 1), andre svarer slet ikke (giver en `502 channel_error` med `MB_TIMEOUT`). Begge er set og verificeret i praksis (se CHANGELOG.md v0.11.0).

### 4.7 OTA-firmwareopdatering

- **`POST /api/ota`** — rå binær body (`.bin`-filen direkte, IKKE multipart — samme mønster som `curl --data-binary @firmware.bin`). Skrives chunket til den inaktive OTA-partition og verificeres automatisk (checksum). Svar ved succes:
  ```json
  {"ok": true, "message": "Firmware uploadet og verificeret - kald POST /api/reboot for at aktivere", "bytes": 810257}
  ```
  Fejler uploadet (forkert magic byte, for stor til partitionen, afbrudt forbindelse, checksum-fejl), svares der med `400`/`500` og en beskrivende `message` — boardet forbliver upåvirket på den KØRENDE firmware.
- **`GET /api/ota/status`** — følg fremdriften: `{"state": "idle|in_progress|success|failed", "received": 0, "total": 0, "percent": 0, "error": ""}`.
- **`POST /api/reboot`** — **påkrævet efter en vellykket `/api/ota`** for reelt at aktivere den nye firmware. Et vellykket OTA-upload sætter KUN den nye firmware som boot-partition — boardet fortsætter uforstyrret på den gamle, kørende firmware indtil denne genstart eksplicit kaldes. Svarer `{"ok": true, "message": "Genstarter..."}` og genstarter ca. 500 ms senere.
- Kun ÉT OTA-upload ad gangen — et samtidigt forsøg giver `409 Conflict`.

---

## 5. Fejlkoder-reference

`mb_error_code_t`-værdier, brugt i `last_error_type` (afsnit 4.3) og `error_code` i `channel_error`-svar (afsnit 4.5/4.6):

| Værdi | Navn | Betydning |
|---|---|---|
| 0 | `MB_OK` | Ingen fejl |
| 1 | `MB_TIMEOUT` | Slaven svarede slet ikke indenfor kanalens `timeout_ms` |
| 2 | `MB_CRC_ERROR` | Svaret blev modtaget, men CRC'en stemte ikke |
| 3 | `MB_EXCEPTION` | (reserveret — Modbus-exceptions fra slaven relayes i dag som almindelige PDU'er, ikke via denne kode) |
| 4 | `MB_MAX_REQUESTS_EXCEEDED` | (reserveret, ikke i brug endnu) |
| 5 | `MB_NOT_ENABLED` | Kanalen er deaktiveret (`enabled:false`) |
| 6 | `MB_INVALID_SLAVE` | Svarets adresse-byte matchede ikke den forespurgte slave |
| 7 | `MB_INVALID_ADDRESS` | Ugyldig/ukendt function code eller PDU-længde i selve requestet |
| 8 | `MB_BUS_BUSY` | Kanalens interne kø var fuld (en anden transaktion optog den) |
| 9 | `MB_CHANNEL_UNREACHABLE` | Generisk intern fejl (buffer for lille, e.l.) |

Modbus-standard exception-koder (fra slaven ELLER boardets egen gateway, se afsnit 3.3):

| Kode | Navn |
|---|---|
| 0x01 | Illegal Function |
| 0x02 | Illegal Data Address |
| 0x03 | Illegal Data Value |
| 0x04 | Slave Device Failure |
| 0x0A | Gateway Path Unavailable (KUN fra boardets egen gateway) |
| 0x0B | Gateway Target Device Failed to Respond (KUN fra boardets egen gateway) |

---

## 6. Anbefalet PLC-side-integrationsflow

1. **Registrering/opsætning** (én gang pr. board, af installatøren): provisionér boardet (afsnit 2), notér IP + management-token, indtast begge i PLC'ens System-side ("Modbus Expansion Boards"-kortet, §5.2).
2. **Sundhedstjek** (periodisk, lav frekvens — fx hvert 30.-60. sekund): `GET /api/status`. Timeout/forbindelsesfejl → markér boardet "unreachable" i UI'en. Tjek `api_version` matcher forventet.
3. **Kanal-config-synk ved (gen)opstart af boardet** (opdaget via `GET /api/status`s manglende svar → fornyet svar, ELLER PLC'ens egen opstart): genskriv ALLE kanalers config via `PUT /api/channels/{n}/config` fra PLC'ens egen, gemte "sandhed" — boardet husker sin PERSISTEREDE config (den overlever reboot), men PLC-siden bør stadig eksplicit synkronisere efter enhver mistanke om boardets egen genstart, i tilfælde af at en administrator har ændret noget direkte på boardet (§4.2's designdokument-note).
4. **Normal drift (høj-frekvent):** Modbus TCP direkte (afsnit 3) — ÉN vedvarende forbindelse pr. kanal, genbrugt for alle transaktioner. IKKE REST-`/read`/`/write`.
5. **Ad-hoc diagnose/test fra UI'en** (fx en "test-forbindelse"-knap i PLC'ens web-UI): REST-`/read`/`/write` (afsnit 4.5/4.6) — lav frekvens, menneske-initieret.
6. **Fejlvisning:** kombinér `GET /api/channels/{n}`s statistik (afsnit 4.3) med Modbus TCP-lagets egne modtagne exceptions (afsnit 3.3) for et fuldt billede — statistikken viser TENDENSER (kanal X har mange timeouts), Modbus TCP-svarene viser DEN ENKELTE transaktions udfald.

---

## 7. Kendte begrænsninger lige nu

- **Kun 2 kanaler** (Variant A). Variant B (8 kanaler, SPI-expander) er designet (§2.2) men ikke bygget.
- **RS232-mode er ikke hardware-testet** — kun RS485 er verificeret med et rigtigt device. `mode:"rs232"` kan sættes via config, men er utestet i praksis.
- **Ingen `POST /api/channels/{n}/reset-stats`/`POST /api/stats/reset`** — statistikken (afsnit 4.3) kan kun nulstilles ved reboot.
- **OTA-uploadets fremdrift kan IKKE afbrydes** — en gang startet, kører uploadet til den lykkes eller fejler; der er intet "annullér"-endpoint.
- **Kanal B er ikke live-testet mod en rigtig slave** (kun kanal A har haft et fysisk device tilsluttet under udviklingen).
- **`active_channels` er altid 2, ikke auto-detekteret** — designdokumentets §2.2.2 (auto-detektion af bestykning) gælder kun Variant B.
- Se [FEATURES.md](FEATURES.md)'s "Planlagte features" for den fulde, opdaterede liste over hvad der mangler.
