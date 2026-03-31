# HyberFusion PLC — Security Guide

**Version:** v7.6.2 | **Date:** 2026-03-30

## Oversigt

HyberFusion PLC understøtter et multi-layer sikkerhedsmodel med:
- **RBAC (Role-Based Access Control)** — op til 8 brugere med roller og privilegier
- **HTTP Basic Authentication** — for REST API og Web UI
- **SSE Authentication** — for Server-Sent Events streams
- **CLI Authentication** — for Serial og Telnet adgang
- **Rate Limiting** — beskyttelse mod brute-force og DoS

---

## 1. RBAC Multi-User System

### Roller (Bitmask)

| Rolle     | Bit  | Adgang                                |
|-----------|------|---------------------------------------|
| `api`     | 0x01 | REST API endpoints                    |
| `cli`     | 0x02 | CLI kommandoer (Serial/Telnet/Web)    |
| `editor`  | 0x04 | ST Logic Editor web side              |
| `monitor` | 0x08 | Dashboard + SSE event streams         |
| `all`     | 0x0F | Alle roller kombineret                |

### Privilegier

| Privilege    | Beskrivelse                                  |
|--------------|----------------------------------------------|
| `read`       | Kun læse-adgang (show, help, ping, who)       |
| `write`      | Kun skrive-adgang                             |
| `read/write` | Fuld adgang (standard for nye brugere)        |

### Eksempler

```
# API-only bruger med fuld adgang
set user api_user password ChangeMe123! roles api privilege read/write

# Monitor bruger (kun dashboard + SSE, ingen skriveadgang)
set user monitor_user password ViewOnly123 roles monitor privilege read

# Fuld admin
set user admin password AdminPass123! roles all privilege read/write

# CLI-only tekniker med læseadgang
set user tech password Tech2026! roles cli privilege read

# Slet bruger
delete user old_user

# Vis alle brugere
show users
```

### Auto-aktivering
- RBAC aktiveres automatisk når første bruger oprettes
- RBAC deaktiveres automatisk når sidste bruger slettes
- Uden RBAC bruges legacy single-user auth (HttpConfig)

---

## 2. Authentication Flow

### HTTP API & Web UI
```
Request → Authorization: Basic <base64> → RBAC check → Role check → Handler
```

Hvis RBAC er disabled, bruges legacy `HttpConfig.username/password`.

### SSE (Server-Sent Events)
```
TCP connect → HTTP headers → Authorization: Basic <base64> → RBAC check
→ MONITOR rolle kræves → Stream startes
```

### CLI (Serial/Telnet)
- Serial: Ingen auth (fysisk adgang = trusted)
- Telnet: Login med telnet credentials
- Read-only brugere kan kun bruge: `show`, `help`, `?`, `ping`, `who`

---

## 3. Konfiguration via CLI

```
# Opret/opdatér bruger
set user <username> password <pass> roles <roles> privilege <priv>

# Vis brugere
show users

# Vis RBAC sektion i config
show config http

# Slet bruger
delete user <username>

# Gem til NVS (persistens)
save
```

### Rolle-syntaks
- Enkelt rolle: `api`, `cli`, `editor`, `monitor`
- Flere roller: `api,cli,monitor` (kommasepareret)
- Alle roller: `all`

### Privilege-syntaks
- `read` — kun læseadgang
- `write` — kun skriveadgang
- `read/write` eller `rw` — fuld adgang

---

## 4. Backup & Restore

RBAC brugerdata inkluderes i backup JSON:

```json
{
  "rbac": {
    "enabled": true,
    "users": [
      {
        "username": "api_user",
        "password": "ChangeMe123!",
        "roles": 1,
        "privilege": 3
      }
    ]
  }
}
```

**OBS:** Passwords gemmes i klartekst i backup-filen. Beskyt backup-filer!

---

## 5. Schema Migration

Ved opgradering fra v7.6.1 eller ældre (schema v14 → v15):
- Eksisterende `HttpConfig.username/password` migreres automatisk til `rbac.users[0]`
- Migreret bruger får `ROLE_ALL` + `PRIV_RW` (fuld admin)
- RBAC aktiveres automatisk
- Ingen brugerhandling kræves

---

## 6. Rate Limiting

HTTP API rate limiting beskytter mod brute-force:
- Konfigurerbar via `set rate-limit enable/disable`
- Standard: 100 requests/minut pr. IP
- 429 Too Many Requests returneres ved overskridelse

---

## 7. Sikkerhedsanbefalinger

1. **Aktivér RBAC** med mindst én admin-bruger
2. **Brug stærke passwords** (min. 8 tegn, bland store/små bogstaver, tal, specialtegn)
3. **Giv minimale roller** — princippet om mindste privilegium
4. **Aktivér TLS** for HTTPS (`set http tls enable`)
5. **Begræns netværksadgang** — brug firewall/VLAN til at isolere PLC-netværk
6. **Gem backup-filer sikkert** — de indeholder passwords i klartekst
7. **Aktivér rate limiting** (`set rate-limit enable`)
8. **Deaktivér ubrugte interfaces** (Telnet, WiFi, etc.)

---

## 8. Kendte Begrænsninger

- Passwords gemmes i klartekst i NVS og backup (ingen hashing)
- Ingen session tokens — Basic Auth sendes med hver request
- Max 8 brugere (hardware begrænsning, ESP32 RAM)
- Ingen audit log for login-forsøg
- SSE bruger raw TCP (ikke HTTPS) — credentials sendes ukrypteret medmindre TLS er aktiveret

---

**Se også:**
- [SSE User Guide](SSE_USER_GUIDE.md) — SSE authentication og brug
- [README.md](../README.md) — Projekt oversigt
