# SSE (Server-Sent Events) — Brugervejledning

**Version:** v7.6.0 | **Dato:** 2026-03-30

---

## Indhold

1. [Hvad er SSE?](#hvad-er-sse)
2. [Arkitektur](#arkitektur)
3. [Forudsætninger](#forudsætninger)
4. [Endpoint reference](#endpoint-reference)
5. [Authentication](#authentication)
6. [Topics og filtrering](#topics-og-filtrering)
7. [Event-typer og JSON-format](#event-typer-og-json-format)
8. [curl eksempler](#curl-eksempler)
9. [JavaScript / Browser](#javascript--browser)
10. [Node-RED integration](#node-red-integration)
11. [Python klient](#python-klient)
12. [CLI konfiguration](#cli-konfiguration)
13. [Fejlfinding](#fejlfinding)
14. [Begrænsninger og ydeevne](#begrænsninger-og-ydeevne)

---

## Hvad er SSE?

Server-Sent Events (SSE) er en standard HTTP-baseret teknologi der giver serveren mulighed for at pushe data til klienten i realtid over en langvarig HTTP-forbindelse. I modsætning til polling (hvor klienten gentagne gange spørger efter nye data) sender SSE kun data **når noget ændrer sig**.

**Fordele:**
- Ingen polling — lavere netværkstrafik og CPU-belastning
- Standard HTTP — virker gennem firewalls og proxies
- Automatisk reconnect i browsere (EventSource API)
- Letvægt — én TCP-forbindelse pr. klient

**Typiske use-cases:**
- Live dashboards med register-/coil-værdier
- Node-RED flows med realtidsopdateringer
- SCADA-integration via server-push
- Overvågning af counter- og timer-tilstande

---

## Arkitektur

SSE kører som en **separat raw TCP-server** på sin egen port (konfigurerbar, default: HTTP-port + 1). Denne arkitektur sikrer at SSE-streams ikke blokerer REST API'en.

```
Port 80 (REST API)          Port 1800 (SSE)
  ┌──────────────┐           ┌───────────────────┐
  │   esp_httpd   │           │  Raw TCP Server    │
  │  56+ REST URIs│           │  Acceptor Task     │
  │  + WebSocket  │           │    ├─ Client 1     │
  └──────────────┘           │    ├─ Client 2     │
         │                    │    └─ Client 3     │
         └──── Delt adgang ──┘
           counter_engine
           timer_engine
           registers[]
```

Hver tilsluttet klient får sin egen FreeRTOS-task med uafhængig change-detection.

---

## Forudsætninger

1. **SSE skal være aktiveret** i konfigurationen (`set sse enable`)
2. **SSE-porten** skal være korrekt konfigureret (`set sse port <port>`)
3. **API authentication** skal være konfigureret (brugernavn/password)
4. Klienten skal understøtte HTTP Basic Auth med base64-encoding
5. ESP32 skal have netværksforbindelse (WiFi eller Ethernet)

---

## Endpoint reference

### SSE Event Stream (kører på SSE-porten)

```
GET http://<ip>:<sse_port>/api/events?subscribe=<topics>&hr=<addrs>&ir=<addrs>&coils=<addrs>&di=<addrs>
```

### SSE Status (kører på REST API-porten, port 80)

```
GET http://<ip>/api/events/status
```

Returnerer JSON med SSE-subsystemets tilstand:

```json
{
  "sse_enabled": true,
  "sse_port": 1800,
  "max_clients": 3,
  "active_clients": 1,
  "check_interval_ms": 100,
  "heartbeat_ms": 15000,
  "topics": ["counters", "timers", "registers", "system"],
  "endpoint": "http://<ip>:1800/api/events?subscribe=<topics>"
}
```

---

## Authentication

SSE bruger HTTP Basic Authentication. Credentials skal sendes som base64-encodet `brugernavn:password` i `Authorization`-headeren.

### Base64-encoding

Formatet er: `base64("brugernavn:password")`

**Eksempel:** `api_user:ChangeMe123!` → `YXBpX3VzZXI6ITIzUGFzc3dvcmQ=`

Du kan generere base64-strengen med:

```bash
# Linux/macOS
echo -n "api_user:ChangeMe123!" | base64

# Windows PowerShell
[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("api_user:ChangeMe123!"))

# Python
python -c "import base64; print(base64.b64encode(b'api_user:ChangeMe123!').decode())"
```

### Header-format

```
Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=
```

> **Bemærk:** Hvis authentication er deaktiveret på ESP32 (`set http auth disable`), kan Authorization-headeren udelades.

---

## Topics og filtrering

### Subscribe-parameter

| Topic | Beskrivelse | Events der sendes |
|-------|-------------|-------------------|
| `counters` | Counter-ændringer | `counter` events |
| `timers` | Timer-ændringer | `timer` events |
| `registers` | Register/coil-ændringer | `register` events |
| `system` | Systeminfo | `heartbeat` events |
| `all` | Alle ovenstående | Alle event-typer |

Flere topics kan kombineres med komma: `subscribe=counters,timers,registers`

### Register watch-lists

Når `registers` er inkluderet i subscribe, kan du specificere præcist hvilke adresser der skal overvåges:

| Parameter | Beskrivelse | Adresseområde | Max pr. type |
|-----------|-------------|----------------|--------------|
| `hr` | Holding Registers | 0-255 | 32 |
| `ir` | Input Registers | 0-255 | 32 |
| `coils` | Coils (output) | 0-255 | 32 |
| `di` | Discrete Inputs | 0-255 | 32 |

### watch_all mode (subscribe=all)

Når du bruger `subscribe=all` **uden** eksplicitte adresse-parametre (hr/ir/coils/di), aktiveres **watch_all mode**. I denne mode overvåges **alle** 256 HR + 256 IR + 256 coils + 256 DI automatisk. Du behøver ikke angive specifikke adresser.

```bash
# watch_all mode — overvåger ALLE registre, coils og DI
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=all"
```

Hvis du tilføjer eksplicitte adresser, bruges den normale watch-list i stedet:

```bash
# Watch-list mode — kun de specificerede adresser
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=all&coils=40-47"
```

### Adresseformat

- **Enkeltadresser:** `hr=0,5,10`
- **Intervaller:** `hr=0-15`
- **Blandet:** `hr=0,5,10-15`

**Default ved `subscribe=registers`:** Hvis ingen adresser angives, overvåges HR 0-15.
**Default ved `subscribe=all`:** Alle adresser overvåges (watch_all mode).

---

## Event-typer og JSON-format

Alle events følger standard SSE-format:

```
event: <event_type>
data: <json_payload>

```

(Bemærk den tomme linje efter `data:` — den markerer slutningen på et event.)

### `connected` — Forbindelsesbekræftelse

Sendes umiddelbart efter succesfuld tilslutning.

```
event: connected
data: {"status":"connected","topics":"0x0f","max_clients":3,"active_clients":1,"port":1800,"watching":{"hr":16,"ir":0,"coils":8,"di":3}}
```

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `status` | string | Altid `"connected"` |
| `topics` | string | Bitmask af aktive topics (hex) |
| `max_clients` | int | Maks samtidige klienter |
| `active_clients` | int | Antal tilsluttede klienter |
| `port` | int | SSE-porten |
| `watching.hr/ir/coils/di` | int | Antal overvågede adresser pr. type |

### `counter` — Counter-ændring

Sendes når en counters værdi eller enabled-tilstand ændres.

```
event: counter
data: {"id":1,"value":12345,"enabled":true,"running":true}
```

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `id` | int | Counter-ID (1-8) |
| `value` | uint64 | Aktuel tællerværdi |
| `enabled` | bool | Om counteren er aktiveret |
| `running` | bool | Om counteren aktivt tæller |

### `timer` — Timer-ændring

Sendes når en timers output eller enabled-tilstand ændres.

```
event: timer
data: {"id":1,"enabled":true,"mode":"ASTABLE","output":true}
```

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `id` | int | Timer-ID (1-8) |
| `enabled` | bool | Om timeren er aktiveret |
| `mode` | string | `ONESHOT`, `MONOSTABLE`, `ASTABLE`, `INPUT_TRIGGERED` |
| `output` | bool | Aktuel output-tilstand |

### `register` — Register/coil-ændring

Sendes når en overvåget adresse ændrer værdi.

```
event: register
data: {"type":"coil","addr":40,"value":1}
```

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `type` | string | `hr`, `ir`, `coil`, `di` |
| `addr` | int | Register-/coil-adresse |
| `value` | int | Ny værdi (0-65535 for hr/ir, 0-1 for coil/di) |

### `heartbeat` — Keepalive

Sendes periodisk (default hver 15. sekund) for at holde forbindelsen i live.

```
event: heartbeat
data: {"uptime_ms":123456,"heap_free":102400,"sse_clients":1}
```

| Felt | Type | Beskrivelse |
|------|------|-------------|
| `uptime_ms` | uint32 | System-uptime i millisekunder |
| `heap_free` | uint32 | Ledig heap-hukommelse i bytes |
| `sse_clients` | int | Antal aktive SSE-klienter |

---

## curl eksempler

Alle eksempler bruger base64-encodet authentication. Erstat IP-adresse og port med dine egne værdier.

### Tjek SSE-status (REST API, port 80)

```bash
curl -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  http://10.1.32.20/api/events/status
```

### Stream alle topics

```bash
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=all"
```

### Stream kun registers (specifikke coils og DI)

```bash
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=registers&coils=40-47&di=40-42"
```

### Stream counters og timers

```bash
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=counters,timers"
```

### Stream kun system (heartbeat)

```bash
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=system"
```

### Holding registers + coils blandet

```bash
curl -N -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
  "http://10.1.32.20:1800/api/events?subscribe=registers&hr=0,5,10-15&coils=0-7&ir=0-3&di=0-3"
```

### Test uden auth (skal returnere 401)

```bash
curl -N "http://10.1.32.20:1800/api/events?subscribe=all"
```

> **Tip:** `-N` (--no-buffer) er vigtigt for SSE-streams — det deaktiverer curl's output-buffering så events vises med det samme. Tryk `Ctrl+C` for at stoppe streamen.

---

## JavaScript / Browser

### EventSource API (anbefalet)

> **Bemærk:** Standard `EventSource` understøtter ikke custom headers. Brug en polyfill som [eventsource](https://www.npmjs.com/package/eventsource) eller send credentials via URL hvis nødvendigt.

```javascript
// Med eventsource-polyfill der understøtter headers
import { EventSource } from 'eventsource';

const url = 'http://10.1.32.20:1800/api/events?subscribe=all';
const es = new EventSource(url, {
  headers: {
    'Authorization': 'Basic ' + btoa('api_user:ChangeMe123!')
  }
});

// Forbindelsesbekræftelse
es.addEventListener('connected', (e) => {
  const data = JSON.parse(e.data);
  console.log('SSE forbundet:', data);
});

// Counter-ændringer
es.addEventListener('counter', (e) => {
  const data = JSON.parse(e.data);
  console.log(`Counter ${data.id}: ${data.value} (${data.running ? 'kører' : 'stoppet'})`);
});

// Timer-ændringer
es.addEventListener('timer', (e) => {
  const data = JSON.parse(e.data);
  console.log(`Timer ${data.id}: ${data.mode} → output=${data.output}`);
});

// Register-ændringer
es.addEventListener('register', (e) => {
  const data = JSON.parse(e.data);
  console.log(`${data.type}[${data.addr}] = ${data.value}`);
});

// Heartbeat
es.addEventListener('heartbeat', (e) => {
  const data = JSON.parse(e.data);
  console.log(`Heartbeat — uptime: ${data.uptime_ms}ms, heap: ${data.heap_free}`);
});

// Fejlhåndtering
es.onerror = (err) => {
  console.error('SSE fejl:', err);
};
```

### Fetch API (alternativ uden polyfill)

```javascript
async function connectSSE(ip, port, user, pass) {
  const auth = btoa(`${user}:${pass}`);
  const url = `http://${ip}:${port}/api/events?subscribe=all`;

  const response = await fetch(url, {
    headers: { 'Authorization': `Basic ${auth}` }
  });

  const reader = response.body.getReader();
  const decoder = new TextDecoder();
  let buffer = '';

  while (true) {
    const { done, value } = await reader.read();
    if (done) break;

    buffer += decoder.decode(value, { stream: true });
    const parts = buffer.split('\n\n');
    buffer = parts.pop();

    for (const part of parts) {
      const eventMatch = part.match(/^event: (.+)$/m);
      const dataMatch = part.match(/^data: (.+)$/m);
      if (eventMatch && dataMatch) {
        const event = eventMatch[1];
        const data = JSON.parse(dataMatch[1]);
        console.log(`[${event}]`, data);
      }
    }
  }
}

connectSSE('10.1.32.20', 1800, 'api_user', 'ChangeMe123!');
```

---

## Node-RED integration

### Metode 1: Native HTTP i Function-node (anbefalet)

Denne metode bruger Node.js' built-in `http`-modul og kræver ingen ekstra nodes.

**Function node setup:**
- **Setup tab:** Tilføj `libs = [{var: "http", module: "http"}]`
- **On Message tab:**

```javascript
const opts = {
  hostname: '10.1.32.20',
  port: 1800,
  path: '/api/events?subscribe=registers&coils=40-47&di=40-42',
  headers: {
    'Authorization': 'Basic ' + Buffer.from('api_user:ChangeMe123!').toString('base64')
  }
};

const req = http.get(opts, (res) => {
  let buf = '';
  res.on('data', (chunk) => {
    buf += chunk.toString();
    const parts = buf.split('\n\n');
    buf = parts.pop();
    for (const part of parts) {
      const eventMatch = part.match(/^event: (.+)$/m);
      const dataMatch  = part.match(/^data: (.+)$/m);
      if (eventMatch && dataMatch) {
        try {
          const data = JSON.parse(dataMatch[1]);
          node.send({ payload: {
            event: eventMatch[1],
            type: data.type || eventMatch[1],
            addr: data.addr,
            value: data.value,
            id: data.id,
            raw: data
          }});
        } catch(e) {
          node.warn('SSE parse error: ' + e.message);
        }
      }
    }
  });

  res.on('end', () => {
    node.warn('SSE forbindelse lukket — genstarter om 5s');
    setTimeout(() => { node.send({ payload: { event: 'reconnect' }}); }, 5000);
  });
});

req.on('error', (err) => {
  node.error('SSE fejl: ' + err.message);
  setTimeout(() => { node.send({ payload: { event: 'reconnect' }}); }, 5000);
});
```

### Metode 2: Importerbar Node-RED flow

Se filen `tests/sse_nodered_native.json` — kan importeres direkte i Node-RED via menu → Import.

---

## Python klient

```python
import requests
import base64

IP = "10.1.32.20"
PORT = 1800
USER = "api_user"
PASS = "ChangeMe123!"

auth = base64.b64encode(f"{USER}:{PASS}".encode()).decode()
url = f"http://{IP}:{PORT}/api/events?subscribe=all"
headers = {"Authorization": f"Basic {auth}"}

# Tjek SSE status først
status = requests.get(f"http://{IP}/api/events/status", headers=headers)
print("SSE Status:", status.json())

# Start SSE stream
response = requests.get(url, headers=headers, stream=True)
print(f"Forbundet (HTTP {response.status_code})")

event_type = None
for line in response.iter_lines(decode_unicode=True):
    if line.startswith("event: "):
        event_type = line[7:]
    elif line.startswith("data: "):
        data = line[6:]
        print(f"[{event_type}] {data}")
    elif line == "":
        event_type = None
```

**Installation:** `pip install requests`

---

## CLI konfiguration

### Vis SSE-status

```
show sse
```

Output eksempel:
```
=== SSE Configuration ===
  Enabled:         Yes
  Port:            1800
  Max clients:     3
  Active clients:  1
  Check interval:  100 ms
  Heartbeat:       15000 ms

  Connected clients:
    Slot 0: 10.1.32.100 (uptime: 45s)
```

### Konfigurationskommandoer

| Kommando | Beskrivelse | Default |
|----------|-------------|---------|
| `set sse enable` | Aktiver SSE-server | enabled |
| `set sse disable` | Deaktiver SSE-server | — |
| `set sse port <port>` | Sæt SSE-port (0 = auto) | 0 (HTTP+1) |
| `set sse max-clients <n>` | Maks samtidige klienter (1-5) | 3 |
| `set sse interval <ms>` | Change-detection interval (50-5000) | 100 |
| `set sse heartbeat <ms>` | Heartbeat interval (1000-60000) | 15000 |
| `set sse disconnect all` | Afbryd alle SSE-klienter | — |
| `set sse disconnect <slot>` | Afbryd specifik klient | — |

**Husk:** Kør `save` efter konfigurationsændringer og `reboot` for at anvende portændringer.

---

## Fejlfinding

### Problem: Ingen data modtages

1. **Tjek SSE-status:**
   ```bash
   curl -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" http://10.1.32.20/api/events/status
   ```
   Bekræft at `sse_enabled` er `true` og at porten er korrekt.

2. **Tjek netværk:**
   ```bash
   curl -v "http://10.1.32.20:1800/"
   ```
   Bekræft at porten er tilgængelig.

3. **Tjek authentication:**
   Kør uden auth — du skal få HTTP 401:
   ```bash
   curl -N "http://10.1.32.20:1800/api/events?subscribe=all"
   ```

4. **Tjek at data ændrer sig:** SSE sender kun events ved ændringer. Prøv at ændre en coil via REST API:
   ```bash
   curl -X POST -H "Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ=" \
     -H "Content-Type: application/json" \
     -d '{"value":1}' \
     http://10.1.32.20/api/registers/coils/40
   ```

### Problem: Forbindelsen afbrydes

- **Heartbeat timeout:** Hvis ingen events sendes i 15 sekunder, sender serveren et heartbeat. Hvis selv dette fejler, lukkes forbindelsen.
- **Max klienter nået:** Check `active_clients` i `/api/events/status`. Max 3 (konfigurerbar op til 5).
- **Lav hukommelse:** SSE kræver ~12 KB heap. Tjek `heap_free` i heartbeat-events.

### Problem: HTTP 401 Unauthorized

- Tjek at base64-strengen er korrekt encodet
- Tjek at brugernavn og password matcher ESP32-konfigurationen
- Tjek at `Authorization: Basic <base64>` headeren er korrekt formateret

### Problem: HTTP 503 Service Unavailable

- Max antal SSE-klienter er nået
- Brug `show sse` på CLI for at se aktive klienter
- Brug `set sse disconnect all` for at rydde gamle forbindelser

---

## Begrænsninger og ydeevne

| Parameter | Værdi | Konfigurerbar |
|-----------|-------|---------------|
| Max samtidige klienter | 3 (1-5) | Ja |
| Change-detection interval | 100ms / 10 Hz (50-5000ms) | Ja |
| Heartbeat interval | 15s (1-60s) | Ja |
| Max overvågede adresser pr. type | 32 | Nej (compile-time) |
| Understøttede registertyper | HR, IR, Coils, DI | Nej |
| Default watch (ingen params) | HR 0-15 | Nej |
| Stack pr. SSE-forbindelse | 6 KB | Nej |
| Heap-forbrug (SSE-server) | ~12 KB | — |
| Min. ledig heap for ny klient | 10 KB | Nej |
| Reconnect cooldown | 500ms | Nej |

### Performance-anbefalinger

- **Begræns watch-lists:** Overvåg kun de adresser du har brug for — det reducerer CPU-belastning
- **Brug topics:** Abonner kun på relevante topics (f.eks. `registers` i stedet for `all`)
- **Hold klient-antal lavt:** Hver klient koster ~6 KB stack + change-detection overhead
- **Interval-tuning:** 100ms (10 Hz) er fint til de fleste formål. Øg til 200-500ms hvis systemet er belastet

---

## Hurtig-reference

```bash
# Base64 auth-streng
AUTH="Authorization: Basic YXBpX3VzZXI6ITIzUGFzc3dvcmQ="

# Status-tjek
curl -H "$AUTH" http://10.1.32.20/api/events/status

# Fuld stream
curl -N -H "$AUTH" "http://10.1.32.20:1800/api/events?subscribe=all"

# Kun coils 40-47
curl -N -H "$AUTH" "http://10.1.32.20:1800/api/events?subscribe=registers&coils=40-47"

# Counters + timers
curl -N -H "$AUTH" "http://10.1.32.20:1800/api/events?subscribe=counters,timers"

# System heartbeat
curl -N -H "$AUTH" "http://10.1.32.20:1800/api/events?subscribe=system"
```
