# Alarm Historik System — Teknisk Guide

## Oversigt

ESP32 Modbus PLC'en har et indbygget alarm-system der overvåger systemtilstand og kommunikationsfejl.
Alarmer vises i Web Monitor dashboardet under "Alarm Historik" og er tilgængelige via REST API.

## Arkitektur

### Ringbuffer

Alarm-loggen er en **ringbuffer** med plads til **32 entries**. Når bufferen er fuld, overskriver nye alarmer de ældste.
Bufferen ligger i RAM og nulstilles ved genstart — alarmer persisteres **ikke** i NVS.

```
alarm_log[32]          ← Fast-size array i RAM
alarm_log_head         ← Næste skrive-position (0-31)
alarm_log_count        ← Antal entries (max 32)
```

### Alarm Entry Struktur

Hver alarm indeholder:

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `timestamp_ms` | uint32 | `millis()` da alarmen blev oprettet |
| `epoch` | time_t | Unix epoch (kun hvis NTP er synkroniseret, ellers 0) |
| `message` | char[80] | Alarm-besked (max 79 tegn) |
| `severity` | uint8 | 0=INFO, 1=ADV (advarsel), 2=KRIT (kritisk) |
| `acknowledged` | bool | Om alarmen er kvitteret |
| `source_ip` | char[16] | Klient-IP (ved auth/privilege fejl) |
| `username` | char[32] | Brugernavn (ved auth/privilege fejl) |

### Tidsstempler

- **Uden NTP:** Alarmer viser uptime som "2d 03:15:42"
- **Med NTP:** Alarmer viser lokal tid som "2026-04-02 14:30:15" (følger ESP32 tidszone-indstilling)

---

## Akkumulerende Events (Delta-Detektion)

De fleste alarmer bruger et **akkumulerende delta-mønster** — de tæller ikke individuelle hændelser
men sammenligner tællere mellem check-intervaller.

### Hvordan det virker

```
Tidsakse:     t=0        t=3s       t=6s       t=9s       t=12s
CRC-tæller:   100        103        103        110        111
Delta:        —          +3         +0         +7         +1
Alarm:        —          nej(<5)    nej(0)     JA!(+7≥5)  nej(<5)
```

**Princippet:**
1. `alarm_check_thresholds()` kører hvert **3. sekund** (ved metrics-fetch)
2. For hver tæller gemmes forrige værdi i `alarm_prev_*` variabler
3. Delta beregnes: `delta = nuværende_tæller - forrige_tæller`
4. Alarm udløses kun hvis delta overstiger en **threshold**
5. Forrige værdi opdateres til nuværende

**Vigtigt:** Første check efter boot udløser aldrig alarm (`alarm_prev_* > 0` krav), da alle `alarm_prev_*` starter på 0.

### Alarm-besked format

Delta-alarmer viser **antal nye fejl** i perioden med `+N` notation:

```
"Modbus Slave CRC fejl +7 RX <- ID:1"     ← 7 nye CRC fejl siden sidst
"Modbus Master timeout +5 TX -> ID:100 @0" ← 5 nye timeouts mod slave 100
"HTTP auth failures +3"                     ← 3 nye auth fejl
"Write privilege denied +2"                 ← 2 nye privilege afvisninger
```

---

## Alle Alarm-Typer

### 1. Heap Kritisk Lav
| Egenskab | Værdi |
|----------|-------|
| **Severity** | KRIT (2) |
| **Trigger** | `heap_free < 20.000 bytes` |
| **Mønster** | Tilstandsbaseret — gentager hvert 3s mens betingelsen er opfyldt |
| **Besked** | `Heap kritisk lav` |

### 2. Heap Advarsel
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | `heap_free < 30.000 bytes` (men ≥ 20.000) |
| **Mønster** | Tilstandsbaseret — gentager hvert 3s |
| **Besked** | `Heap advarsel` |

### 3. Modbus Slave CRC Fejl
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | ≥ 5 nye CRC fejl i en 3s periode |
| **Mønster** | Akkumulerende delta |
| **Besked** | `Modbus Slave CRC fejl +N RX <- ID:X` |
| **Kontekst** | Slave ID i beskeden |

### 4. Modbus Master Timeout
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | ≥ 3 nye timeouts i en 3s periode |
| **Mønster** | Akkumulerende delta |
| **Besked** | `Modbus Master timeout +N TX -> ID:X @Y` |
| **Kontekst** | Slave ID + register-adresse fra seneste fejlede request |

### 5. HTTP Auth Failures (401)
| Egenskab | Værdi |
|----------|-------|
| **Severity** | KRIT (2) |
| **Trigger** | ≥ 3 nye auth fejl i en 3s periode |
| **Mønster** | Akkumulerende delta |
| **Besked** | `HTTP auth failures +N` |
| **Kontekst** | Klient-IP + brugernavn fra seneste fejlede login |

### 6. Write Privilege Denied (403)
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | ≥ 1 ny privilege-afvisning i en 3s periode |
| **Mønster** | Akkumulerende delta |
| **Besked** | `Write privilege denied +N` |
| **Kontekst** | Klient-IP + brugernavn der forsøgte skriveadgang |

### 7. SSE Server Mættet
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | `sse_active_clients >= sse_max_clients` |
| **Mønster** | Tilstandsbaseret med hysterese — alarm kun én gang, auto-clear ved ledig plads |
| **Besked** | `SSE server mættet N/M klienter` |

### 8. ST Logic Overrun Rate
| Egenskab | Værdi |
|----------|-------|
| **Severity** | ADV (1) |
| **Trigger** | Overrun rate > 5% (efter min. 100 cycles) |
| **Mønster** | Tilstandsbaseret — gentager hvert 3s |
| **Besked** | `ST Logic overrun rate > 5%` |

---

## Alarm-Mønstre Forklaret

### Akkumulerende Delta
Bruges til: CRC fejl, timeouts, auth failures, write denied.

Sammenligner en global tæller med dens forrige værdi. Rapporterer kun når **delta ≥ threshold**.
Dette forhindrer alarm-spam ved konstante, lave fejlrater.

```
Threshold-tabel:
  Modbus Slave CRC:     ≥ 5 per 3s   (tolererer sporadiske fejl)
  Modbus Master timeout: ≥ 3 per 3s   (lavere, da timeouts er mere kritiske)
  HTTP auth failures:    ≥ 3 per 3s   (forhindrer spam ved brute-force forsøg)
  Write privilege denied: ≥ 1 per 3s  (altid relevant — indikerer fejlkonfiguration)
```

### Tilstandsbaseret
Bruges til: Heap lav, ST Logic overruns.

Checker en betingelse hvert 3. sekund. Så længe betingelsen er opfyldt, logges en alarm **hver** 3s.
Kan resultere i mange gentagne alarmer ved vedvarende problemer.

### Tilstandsbaseret med Hysterese
Bruges til: SSE mættet.

Alarm logges kun **én gang** når tilstanden indtræder. Alarmen auto-clearer internt når tilstanden ophæves,
og en ny alarm kan derefter logges næste gang. Forhindrer spam ved oscillerende tilstande.

---

## Web Dashboard Visning

Alarm Historik-kortet i Monitor dashboardet:

- Spænder **2 kolonner** i grid-layout
- Viser op til **25 nyeste** alarmer (nyeste øverst)
- Opdateres hvert **5. sekund** (via `GET /api/alarms`)
- Scrollbar ved mere end ~10 alarmer (max-height: 320px)

### Kolonner

| Kolonne | Indhold |
|---------|---------|
| **Tid** | Lokal tid (NTP) eller uptime |
| **Sev.** | KRIT (rød), ADV (orange), INFO (grøn) |
| **Alarm** | Alarm-besked med delta (+N) |
| **Kilde / Detaljer** | IP-adresse, brugernavn, Modbus kontekst |
| **Kvit** | Rød dot (aktiv) / grå dot (kvitteret) |

### Kilde/Detaljer Parsing

Dashboard-JS'en parser alarm-beskeder for at vise rig kontekst:

- **`TX -> ID:X @Y`** → Viser "TX → Slave ID:X Reg:Y" (orange, for master-fejl)
- **`RX <- ID:X`** → Viser "RX ← Slave ID:X" (orange, for slave-fejl)
- **`source_ip`** → Viser klient-IP (blå)
- **`username`** → Viser brugernavn (lilla)

---

## REST API

### GET /api/alarms

Henter alle alarmer i loggen (ældste først).

**Response:**
```json
[
  {
    "timestamp_ms": 125000,
    "message": "Modbus Master timeout +5 TX -> ID:100 @0",
    "severity": 1,
    "acknowledged": false,
    "uptime": "0d 00:02:05",
    "epoch": 1743600615,
    "time": "2026-04-02 14:30:15",
    "source_ip": "192.168.1.50",
    "username": "operator"
  }
]
```

**Felter:**
- `severity`: 0=INFO, 1=ADV, 2=KRIT
- `uptime`: Altid inkluderet (formateret fra `timestamp_ms`)
- `epoch`/`time`: Kun inkluderet hvis NTP var synkroniseret da alarmen blev oprettet
- `source_ip`/`username`: Kun inkluderet for auth/privilege relaterede alarmer

### POST /api/alarms/ack

Kvitterer alle alarmer. Kræver autentificering.

**Response:**
```json
{"status": 200, "message": "All alarms acknowledged"}
```

---

## Kvittering

- **Kvittér-knap** i dashboardet sender `POST /api/alarms/ack`
- Kvittering sætter `acknowledged = true` på alle entries
- Kvitterede alarmer vises stadig i loggen med grå dot
- Kvittering sletter **ikke** alarmer — de forsvinder kun når ringbufferen roterer

---

## Begrænsninger

| Begrænsning | Beskrivelse |
|-------------|-------------|
| **Ingen persistens** | Alarmer ligger kun i RAM, nulstilles ved reboot |
| **32 entries max** | Ringbuffer — ældste alarmer overskries |
| **3s check-interval** | Korte bursts mellem checks kan samles i ét delta |
| **Heap-alarmer gentager** | Kan fylde loggen under vedvarende heap-pres |
| **Ingen e-mail/push** | Alarmer er kun synlige i web dashboard og API |
| **Enkelt brugernavn** | Ved multiple auth-fejl gemmes kun seneste brugernavn |

---

## Konfiguration

Alarm-systemet har ingen bruger-konfigurerbare indstillinger. Thresholds og check-interval er hardkodet:

| Parameter | Værdi | Fil |
|-----------|-------|-----|
| Ringbuffer størrelse | 32 entries | `api_handlers.cpp:87` |
| Besked max længde | 80 tegn | `api_handlers.cpp:88` |
| Check interval | 3.000 ms | `api_handlers.cpp:185` |
| Dashboard refresh | 5.000 ms | `web_dashboard.cpp` |
| Slave CRC threshold | ≥ 5 delta | `api_handlers.cpp:200` |
| Master timeout threshold | ≥ 3 delta | `api_handlers.cpp:214` |
| Auth failure threshold | ≥ 3 delta | `api_handlers.cpp:230` |
| Write denied threshold | ≥ 1 delta | `api_handlers.cpp:242` |
| Heap kritisk | < 20.000 bytes | `api_handlers.cpp:190` |
| Heap advarsel | < 30.000 bytes | `api_handlers.cpp:193` |
| ST Logic overrun | > 5% rate | `api_handlers.cpp:270` |

---

*Sidst opdateret: 2026-04-02 — v7.8.4.2*
