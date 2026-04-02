# NTP Tidssynkronisering & Tidszoner

## Oversigt

ESP32 Modbus RTU Server understøtter NTP (Network Time Protocol) tidssynkronisering med konfigurerbar POSIX tidszone. Tiden bruges til alarm-tidsstempler, web dashboard og log-filer.

---

## Hurtig opsætning (Danmark)

```
set ntp enable
set ntp server pool.ntp.org
set ntp tz CET-1CEST,M3.5.0,M10.5.0/3
save
```

Det er alt. Tiden synkroniseres automatisk og følger dansk sommer/vintertid.

---

## NTP CLI Kommandoer

| Kommando | Beskrivelse |
|----------|-------------|
| `set ntp enable` | Aktivér NTP synkronisering |
| `set ntp disable` | Deaktivér NTP |
| `set ntp server <hostname>` | Sæt NTP server (fx `pool.ntp.org`) |
| `set ntp timezone <POSIX TZ>` | Sæt tidszone (se tabel nedenfor) |
| `set ntp interval <minutter>` | Sync-interval (default: 60 min) |
| `show ntp` | Vis NTP status og konfiguration |
| `show time` | Vis aktuel tid |
| `save` | Gem konfiguration til NVS (påkrævet!) |

### API Endpoints

| Endpoint | Metode | Beskrivelse |
|----------|--------|-------------|
| `/api/ntp` | GET | Hent NTP config + synkroniseringsstatus |
| `/api/ntp` | POST | Opdatér NTP konfiguration (JSON body) |

---

## Forståelse af POSIX Tidszonestrenge

### Hvorfor ikke bare "CEST"?

ESP32 bruger POSIX TZ-format, som kræver **fuld specifikation** af:
- Vintertid-navn og offset
- Sommertid-navn
- Dato/klokkeslæt for skift mellem sommer og vinter

`set ntp tz CEST` virker **IKKE korrekt** — det sætter bare et navn uden offset og uden DST-regler. Resultatet er UTC+0, altså 1-2 timer forkert for Danmark.

### POSIX TZ Format

```
STDoffset[DST[offset],start,end]
```

| Del | Betydning | Eksempel |
|-----|-----------|----------|
| `STD` | Vintertid-navn (standardtid) | `CET` |
| `offset` | UTC-offset for vintertid (negativ = øst) | `-1` = UTC+1 |
| `DST` | Sommertid-navn (valgfrit) | `CEST` |
| `offset` | UTC-offset for sommertid (default: STD-1) | (udeladt = UTC+2) |
| `start` | Sommertid starter | `M3.5.0` |
| `end` | Vintertid starter | `M10.5.0/3` |

### Dato-format: `Mm.w.d/t`

| Del | Betydning |
|-----|-----------|
| `M` | Måned (1-12) |
| `w` | Forekomst af ugedagen i måneden (1-4 = 1.-4. forekomst, **5 = sidste**) |
| `d` | Ugedag (0 = søndag, 1 = mandag, ... 6 = lørdag) |
| `/t` | Klokkeslæt for skift (default: 02:00) |

> **OBS:** `w=5` betyder **"sidste forekomst"**, IKKE "uge 5". Uanset om måneden har 4 eller 5 af den pågældende ugedag, vælges altid den sidste. EU-reglerne bruger altid `5` fordi sommertid/vintertid skifter på den **sidste søndag** i marts/oktober.

### Eksempel: Danmark (`CET-1CEST,M3.5.0,M10.5.0/3`)

```
CET       → Vintertid hedder "CET"
-1        → CET = UTC+1
CEST      → Sommertid hedder "CEST" (automatisk UTC+2)
M3.5.0    → Sommertid starter: Marts, sidste søndag, kl. 02:00
M10.5.0/3 → Vintertid starter: Oktober, sidste søndag, kl. 03:00
```

Resultater:
- **Vinter (nov-mar):** CET = UTC+1
- **Sommer (mar-okt):** CEST = UTC+2

---

## Tidszoner — Kopiér & Indsæt

### Europa

| Land/Zone | CLI Kommando |
|-----------|-------------|
| **Danmark / Centraleuropa** | `set ntp tz CET-1CEST,M3.5.0,M10.5.0/3` |
| Tyskland, Frankrig, Italien, Spanien | `set ntp tz CET-1CEST,M3.5.0,M10.5.0/3` |
| Norge, Sverige | `set ntp tz CET-1CEST,M3.5.0,M10.5.0/3` |
| UK, Irland, Portugal | `set ntp tz GMT0BST,M3.5.0/1,M10.5.0` |
| Finland, Estland, Letland, Litauen | `set ntp tz EET-2EEST,M3.5.0/3,M10.5.0/4` |
| Grækenland, Rumænien, Bulgarien | `set ntp tz EET-2EEST,M3.5.0/3,M10.5.0/4` |
| Island | `set ntp tz GMT0` |
| Tyrkiet | `set ntp tz TRT-3` |
| Rusland (Moskva) | `set ntp tz MSK-3` |

### Nordamerika

| Land/Zone | CLI Kommando |
|-----------|-------------|
| US Eastern (New York) | `set ntp tz EST5EDT,M3.2.0,M11.1.0` |
| US Central (Chicago) | `set ntp tz CST6CDT,M3.2.0,M11.1.0` |
| US Mountain (Denver) | `set ntp tz MST7MDT,M3.2.0,M11.1.0` |
| US Pacific (Los Angeles) | `set ntp tz PST8PDT,M3.2.0,M11.1.0` |
| US Arizona (ingen DST) | `set ntp tz MST7` |

### Asien & Mellemøsten

| Land/Zone | CLI Kommando |
|-----------|-------------|
| Dubai, UAE | `set ntp tz GST-4` |
| Indien | `set ntp tz IST-5:30` |
| Bangkok, Vietnam | `set ntp tz ICT-7` |
| Singapore, Malaysia | `set ntp tz SGT-8` |
| Kina (Beijing) | `set ntp tz CST-8` |
| Japan, Korea | `set ntp tz JST-9` |

### Oceanien

| Land/Zone | CLI Kommando |
|-----------|-------------|
| Australien Eastern (Sydney) | `set ntp tz AEST-10AEDT,M10.1.0,M4.1.0/3` |
| Australien Central (Adelaide) | `set ntp tz ACST-9:30ACDT,M10.1.0,M4.1.0/3` |
| Australien Western (Perth) | `set ntp tz AWST-8` |
| New Zealand | `set ntp tz NZST-12NZDT,M9.5.0,M4.1.0/3` |

### Ingen tidszone (UTC)

```
set ntp tz UTC0
```

---

## Anbefalede NTP Servere

| Server | Beskrivelse |
|--------|-------------|
| `pool.ntp.org` | Global NTP pool (anbefalet) |
| `dk.pool.ntp.org` | Dansk NTP pool |
| `de.pool.ntp.org` | Tysk NTP pool |
| `time.google.com` | Google NTP |
| `time.cloudflare.com` | Cloudflare NTP |
| `ntp.ubuntu.com` | Ubuntu NTP |

---

## Fejlfinding

### Tid er forkert (offset fejl)

**Symptom:** Tiden er X timer forkert.

**Årsag:** Forkert eller manglende tidszone.

**Løsning:**
```
show ntp              # Check timezone felt
set ntp tz CET-1CEST,M3.5.0,M10.5.0/3
save
```

### "Venter på sync..."

**Symptom:** NTP viser "Venter på sync" i lang tid.

**Tjek:**
1. Er netværk oppe? `ping pool.ntp.org` (eller tjek WiFi/Ethernet)
2. Er DNS konfigureret? (DHCP giver normalt DNS automatisk)
3. Er port 123/UDP åben i firewall?

### Tid springer ved reboot

**Symptom:** Tid vises korrekt, men efter reboot er den forkert igen.

**Årsag:** `save` ikke udført efter `set ntp`.

**Løsning:**
```
set ntp tz CET-1CEST,M3.5.0,M10.5.0/3
save                  # VIGTIGT!
```

### Sommertid-skift virker ikke

**Symptom:** Tid er korrekt om vinteren, men 1 time forkert om sommeren (eller omvendt).

**Årsag:** Tidszone sat uden DST-regler (fx `set ntp tz CET-1` uden CEST-del).

**Løsning:** Brug fuld POSIX-streng med DST-regler:
```
set ntp tz CET-1CEST,M3.5.0,M10.5.0/3
save
```

---

## Komplet opsætning fra bunden

```
# 1. Aktivér NTP
set ntp enable

# 2. Sæt NTP server
set ntp server dk.pool.ntp.org

# 3. Sæt tidszone (Danmark)
set ntp tz CET-1CEST,M3.5.0,M10.5.0/3

# 4. (Valgfrit) Ændr sync-interval (default: 60 min)
set ntp interval 30

# 5. Gem til NVS
save

# 6. Verificér
show ntp
show time
```

Forventet output:
```
[NTP]
  enabled:  true
  server:   dk.pool.ntp.org
  timezone: CET-1CEST,M3.5.0,M10.5.0/3
  synced:   true
  sync_count: 1
  local_time: 2026-04-02T14:30:15

Lokal tid: 2026-04-02 14:30:15 CEST
```

---

## API Eksempler

### Hent NTP status (GET)
```bash
curl http://192.168.1.100/api/ntp
```

Response:
```json
{
  "enabled": true,
  "server": "dk.pool.ntp.org",
  "timezone": "CET-1CEST,M3.5.0,M10.5.0/3",
  "sync_interval_min": 60,
  "synced": true,
  "sync_count": 5,
  "local_time": "2026-04-02T14:30:15",
  "epoch": 1775221815,
  "last_sync_age_ms": 120000
}
```

### Konfigurér NTP (POST)
```bash
curl -X POST http://192.168.1.100/api/ntp \
  -H "Content-Type: application/json" \
  -H "Authorization: Basic dXNlcjpwYXNz" \
  -d '{"enabled":true,"server":"pool.ntp.org","timezone":"CET-1CEST,M3.5.0,M10.5.0/3","sync_interval_min":60}'
```

---

**Version:** v7.8.3.1
**Sidst opdateret:** 2026-04-02
