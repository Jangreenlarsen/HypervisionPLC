# 7. REST API

[← 6. Modbus-interface](06_Modbus_Interface.md) · [Indeks](00_INDEKS.md) · Næste: [8. ST Logic-programmering →](08_ST_Logic_Programmering.md)

---

## 7.1 Grundlæggende

- **Format:** JSON over HTTP(S)
- **Base-URL:** `http://<enhedens-ip>/api/...` (eller `https://` hvis TLS er aktiveret, se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md))
- **Versionering:** endpoints kan tilgås både uden præfiks (`/api/status`) og med eksplicit versionspræfiks (`/api/v1/status`) — de er ækvivalente i dag, men brug `/api/v1/` i ny integrationskode for fremtidssikring, hvis der senere indføres et `/api/v2/`.
- **CORS:** `Access-Control-Allow-Origin: *` sættes konsekvent — API'et kan kaldes direkte fra browser-JavaScript på en anden origin.

## 7.2 Autentificering

De fleste endpoints kræver login (samme brugerdatabase som webdashboardet — se [kapitel 10](10_Sikkerhed_og_Adgangsstyring.md) for RBAC/roller). Tre niveauer bruges internt:

| Niveau | Betydning |
|--------|-----------|
| Ingen auth | Et lille antal endpoints er bevidst åbne (fx `/api/metrics` til Prometheus-scraping) |
| Auth krævet (læs) | Gyldigt login, enhver rolle — bruges til de fleste `GET`-endpoints |
| Auth + skriverettighed | Gyldigt login **og** write-privilegie på kontoen — bruges til alle `POST`/`DELETE`-endpoints der ændrer noget |

**Hvilke credential-typer der reelt accepteres afhænger af enhedens `auth_mode`** (`none`/`basic`/`bearer`, se [§10.3.1](10_Sikkerhed_og_Adgangsstyring.md#1031-auth-metode-none--basic--bearer-feat-397h-fra-v79420)) — **`bearer` er standard fra v7.9.42.0**, både for fabriksnye og opgraderede enheder:

**`bearer`-tilstand (standard) — login først, brug tokenet som Bearer:**
```bash
# 1) Login (POST /api/login accepterer ALTID Basic-encodede credentials, uanset auth_mode)
TOKEN=$(curl -s -u admin:modbus123 -X POST http://192.168.1.100/api/login | python3 -c "import sys,json;print(json.load(sys.stdin)['token'])")

# 2) Brug tokenet som Bearer på almindelige endpoints — direkte -u Basic-Auth her giver 401 i bearer-tilstand
curl -H "Authorization: Bearer $TOKEN" http://192.168.1.100/api/status
```
```python
import requests
r = requests.post("http://192.168.1.100/api/login", auth=("admin", "modbus123"))
token = r.json()["token"]
r = requests.get("http://192.168.1.100/api/status", headers={"Authorization": f"Bearer {token}"})
print(r.json())
```
```javascript
// Node-RED / JavaScript
const loginResp = await fetch('http://192.168.1.100/api/login', {
  method: 'POST',
  headers: { Authorization: 'Basic ' + Buffer.from('admin:modbus123').toString('base64') }
});
const { token } = await loginResp.json();
fetch('http://192.168.1.100/api/status', { headers: { Authorization: `Bearer ${token}` } });
```
Tokenet er kortlivet (glidende 30-minutters inaktivitets-timeout, [§10.3](10_Sikkerhed_og_Adgangsstyring.md#103-standard-credentials--skal-ændres)) — genhent ved en 401 midt i en langvarig integration.

**`basic`-tilstand (ældre/valgfri adfærd) — direkte Basic-Auth på hvert kald, intet login-trin nødvendigt:**
```bash
curl -u admin:modbus123 http://192.168.1.100/api/status
```
```python
r = requests.get("http://192.168.1.100/api/status", auth=("admin", "modbus123"))
```

> Skift `admin`/`modbus123` til jeres egne credentials — se [§3.6](03_Installation_og_Foerste_Opstart.md#36-hærdning-efter-installation). Send **aldrig** produktions-credentials over almindelig HTTP på et utillidsfuldt netværk; brug HTTPS ([kapitel 10](10_Sikkerhed_og_Adgangsstyring.md)).

## 7.3 Rate limiting

Alle `/api/*`-kald er begrænset af en **token bucket** pr. klient-IP: op til 8 samtidige klient-IP'er trackes, hver med op til **30 requests i burst** og **10 requests/sekund** vedvarende genopfyldning. Overskrides grænsen, svares `429 Too Many Requests`. Design jeres integration til at bakke af og prøve igen ved 429, ikke til at antage ubegrænset gennemløb.

## 7.4 Endpoint-oversigt (kategorier)

Fuld liste med metode, auth-krav og beskrivelse: [**Appendiks B: REST API-reference**](B_REST_API_Reference.md). Kategorierne:

| Kategori | Eksempler | Beskrivelse |
|----------|-----------|-------------|
| System/Status | `/api/status`, `/api/version`, `/api/config` | Runtime-status og fuld konfiguration |
| Registre & Coils | `/api/registers`, `/api/coils`, `/api/gpio/{pin}` | Direkte læs/skriv-adgang til register-lageret |
| Modbus Slave/Master | `/api/modbus/slave`, `/api/modbus/master`, `/api/modbus/master/rw` | Konfiguration + manuel Master-adgang |
| Modbus Aktivitetslog | `/api/modbus/activity` | Wire-level trafiklog (se [§4.2](04_Web_Dashboard_og_Monitor.md)) |
| Eksterne registre | `/api/ext/rtu/{slave}/hr/{addr}`, `/api/ext/mbx/{board}/{kanal}/{slave}/coils/{addr}` | Læs/skriv registre på RS485-slaver og expansion boards med samme form som de interne (FEAT-486, se §7.7) |
| Modbus Expansion Board | `/api/expansion/boards`, `/api/expansion/boards/{id}/channels/{n}/read` | CRUD + diagnostisk læs/skriv mod eksterne expansion-boards (se [§6.7](06_Modbus_Interface.md#67-modbus-expansion-boards-feat-409)) |
| ST Logic | `/api/logic`, `/api/logic/{id}/source`, `/api/logic/{id}/debug` | Programmer, kildekode, debugger, bindings |
| Tællere/Timere | `/api/counters`, `/api/timers` | Status og styring |
| Netværk | `/api/wifi`, `/api/ethernet`, `/api/ntp` | Netværkskonfiguration |
| Sikkerhed | `/api/users`, `/api/rbac` | Brugere og roller |
| Backup/Restore | `/api/system/backup`, `/api/system/restore` | Fuld konfigurationseksport/-import |
| OTA | `/api/system/ota`, `/api/system/ota/status` | Firmwareopdatering |
| Alarmer | `/api/alarms`, `/api/alarms/ack` | Systemhændelseslog (auto-genereret ved fx lav heap, høj fejlrate) |
| Hændelses-/registerlog | `/api/syslog` | Config/reboot/login-events + hvem ændrede hvilket register/coil (se [§4.2](04_Web_Dashboard_og_Monitor.md)) |
| Overvågning | `/api/metrics`, `/api/events` (SSE) | Prometheus-metrics, real-time push |

## 7.5 Eksempel: læs og skriv et holding-register

```bash
# Læs holding-register 100
curl -u admin:modbus123 http://192.168.1.100/api/registers/holding/100

# Skriv værdien 42 til holding-register 100
curl -u admin:modbus123 -X POST http://192.168.1.100/api/registers/holding/100 \
     -H "Content-Type: application/json" -d '{"value": 42}'
```

Se den fulde REST-signatur (præcise felter, alternative typer som DINT/REAL) i [Appendiks B](B_REST_API_Reference.md).

## 7.6 Eksempel: fjernstyret Modbus Master via REST

I stedet for at skrive et ST-program kan Master-siden også fjernstyres direkte via REST — praktisk til ad-hoc-integrationer eller når man vil lade et eksternt system (Node-RED, en cloud-service) drive pollingen:

```bash
curl -u admin:modbus123 -X POST http://192.168.1.100/api/modbus/master/rw \
     -H "Content-Type: application/json" \
     -d '{"op":"read","type":"holding","slave":1,"addr":0}'
```

Læsninger er **asynkrone** — svaret kommer enten fra cachen med det samme (`"status":"ok","source":"cache"`), eller som `"status":"pending"` hvis værdien først skal hentes fra bussen; poll samme endpoint igen for at hente resultatet. Se [kapitel 6](06_Modbus_Interface.md#65-modbus-master--konfiguration) for baggrund om cache/kø-mekanismen.

## 7.7 Eksterne registre (RS485-slaver og expansion boards) — FEAT-486

SCADA og andre systemer kan læse og skrive registre på enhederne bag PLC'en med samme slags URL'er som de interne registre:

| Kilde | URL |
|---|---|
| Slave på PLC'ens egen RS485-bus (Modbus Master skal være aktiveret) | `/api/ext/rtu/{slave}/{type}/{addr}` |
| Slave bag et expansion board | `/api/ext/mbx/{board}/{kanal}/{slave}/{type}/{addr}` |

`{type}` er `hr`, `ir`, `coils` eller `di`; `{kanal}` er `A`-`H` eller `1`-`8`; adresser 0-65535.

**Læsning (GET)** — query-parametre, alle valgfrie:

| Parameter | Betydning |
|---|---|
| `count=N` | Antal værdier (højst 16 registre pr. kald) |
| `type=uint\|int\|dint\|dword\|real` | Fortolkning af HR/IR; 32-bit-typer bruger 2 registre (high word først, som de interne) |
| `wait=ms` | Vent op til `ms` (max 2000) på et friskt svar fra bussen |
| `max_age=ms` | Hvor gammel en cache-værdi må være, før den hentes igen (standard 1000) |

```bash
curl -u admin:… "http://10.1.1.30/api/ext/mbx/1/D/1/coils/0?count=4&wait=500"
# {"src":"mbx","board":1,"channel":"D","slave":1,"type":"coils","address":0,"format":"bool",
#  "value":true,"status":"ok","age_ms":12,"count":4,"values":[true,false,false,true],
#  "states":["ok","ok","ok","ok"],"ages_ms":[12,12,12,12]}
```

Læsninger går gennem **samme kø og cache som ST Logic** — PLC'ens webserver taler aldrig selv med bussen. Uden `wait` får du den seneste kendte værdi med det samme, og kaldet sætter en opfriskning i kø; første læsning af en ny adresse giver derfor `"value":null,"status":"pending"`. En SCADA, der poller fast, har værdien fra 2. kald. Med `wait` venter kaldet på svaret, men webserveren er optaget imens — brug korte ventetider. `status` er `ok`, `pending` eller `error`.

**Skrivning (POST)** — kun `hr` og `coils`, kræver skriverettighed og logges i hændelsesloggen:

```bash
curl -u admin:… -X POST "http://10.1.1.30/api/ext/rtu/90/hr/10?wait=500" -d '{"value":1234}'
curl -u admin:… -X POST "http://10.1.1.30/api/ext/rtu/90/hr/20" -d '{"value":21.5,"type":"real"}'
curl -u admin:… -X POST "http://10.1.1.30/api/ext/mbx/1/D/1/coils/0" -d '{"values":[true,false,true,false]}'
```

`{"value":…}` skriver ét register/én coil (FC06/FC05; `dint`/`dword`/`real` = 2 registre med FC16); `{"values":[…]}` skriver op til 16 (FC16/FC15). Svaret har `"result":"queued"`, eller med `wait` slavens resultat `ok`/`error`/`timeout` (HTTP 502 ved fejl). Flere-register-skrivninger til et expansion board kan ikke bekræftes og svarer altid `queued`.

**Begrænsninger:** cachen deles med ST Logic (32 poster for RS485, 48 for expansion boards). Overvåger SCADA mange flere registre end det, skubber de hinanden og ST's værdier ud af cachen, og hver læsning bliver til bustrafik. Hold antallet af eksterne adresser nede, eller læs sammenhængende holding registers med `count` (ét FC03-kald).

## 7.8 Node-RED / SCADA-integration

Se [`../../archive/docs/API_HELLO_WORLD_GUIDE.md`](../../archive/docs/API_HELLO_WORLD_GUIDE.md) for en trin-for-trin-gennemgang med et komplet eksempel (ST-program + GPIO + REST-kald), og [`../../archive/docs/SSE_USER_GUIDE.md`](../../archive/docs/SSE_USER_GUIDE.md) for real-time push-integration i stedet for polling.

---

[← 6. Modbus-interface](06_Modbus_Interface.md) · [Indeks](00_INDEKS.md) · Næste: [8. ST Logic-programmering →](08_ST_Logic_Programmering.md)
