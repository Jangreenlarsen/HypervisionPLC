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
Tokenet er kortlivet (glidende 30-minutters inaktivitets-timeout, [§10.3](10_Sikkerhed_og_Adgangsstyring.md#103-standard-credentials-skal-aendres)) — genhent ved en 401 midt i en langvarig integration.

**`basic`-tilstand (ældre/valgfri adfærd) — direkte Basic-Auth på hvert kald, intet login-trin nødvendigt:**
```bash
curl -u admin:modbus123 http://192.168.1.100/api/status
```
```python
r = requests.get("http://192.168.1.100/api/status", auth=("admin", "modbus123"))
```

> Skift `admin`/`modbus123` til jeres egne credentials — se [§3.6](03_Installation_og_Foerste_Opstart.md#haerdning-efter-installation). Send **aldrig** produktions-credentials over almindelig HTTP på et utillidsfuldt netværk; brug HTTPS ([kapitel 10](10_Sikkerhed_og_Adgangsstyring.md)).

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

## 7.7 Node-RED / SCADA-integration

Se [`../API_HELLO_WORLD_GUIDE.md`](../API_HELLO_WORLD_GUIDE.md) for en trin-for-trin-gennemgang med et komplet eksempel (ST-program + GPIO + REST-kald), og [`../SSE_USER_GUIDE.md`](../SSE_USER_GUIDE.md) for real-time push-integration i stedet for polling.

---

[← 6. Modbus-interface](06_Modbus_Interface.md) · [Indeks](00_INDEKS.md) · Næste: [8. ST Logic-programmering →](08_ST_Logic_Programmering.md)
