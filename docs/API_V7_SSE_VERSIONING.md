# API v7.0.3 — SSE Real-Time Events & API Versioning

**Version:** v7.0.3 | **Date:** 2026-03-18

---

## Overview

v7.0.x introduces two major API features:

1. **FEAT-023: Server-Sent Events (SSE)** — Push-based real-time updates, eliminates polling
2. **FEAT-030: API Versioning** — `/api/v1/*` prefix support for backward compatibility

**v7.0.1** adds configurable register watch lists for SSE — monitor specific HR, IR, coils and DI addresses.

Both features are fully backward-compatible. Existing `/api/*` endpoints work unchanged.

---

## FEAT-023: SSE Real-Time Events

### Architecture

SSE runs on a **raw TCP socket server** on a separate port (default: 81). Each client gets its own FreeRTOS task, enabling true multi-client support without blocking the main REST API.

```
Port 80 (main)          Port 81 (SSE)
  ┌──────────┐           ┌───────────────┐
  │  httpd   │           │ Raw TCP Server │
  │ REST API │           │ Acceptor Task  │
  │ 56+ URIs │           │   ├─ Client 1  │
  └──────────┘           │   ├─ Client 2  │
       │                 │   └─ Client 3  │
       └──── Shared ─────┘
         counter_engine
         timer_engine
         registers
```

### Endpoint

```
GET http://<ip>:81/api/events?subscribe=<topics>&hr=<addrs>&ir=<addrs>&coils=<addrs>&di=<addrs>
```

**Query Parameters:**
| Parameter | Values | Default |
|-----------|--------|---------|
| `subscribe` | `counters`, `timers`, `registers`, `system`, `all` | `all` |
| `hr` | Holding register addresses (0-159) | `0-15` |
| `ir` | Input register addresses (0-159) | none |
| `coils` | Coil addresses (0-255) | none |
| `di` | Discrete input addresses (0-255) | none |

Multiple topics can be comma-separated: `?subscribe=counters,timers`

**Address format** (v7.0.1+): Individual, ranges, or mixed — comma-separated:
- Individual: `hr=0,5,10`
- Range: `hr=0-15`
- Mixed: `hr=0,5,10-15`
- Max 32 addresses per type

### SSE Event Format

All events follow the standard SSE format:

```
event: <event_type>
data: <json_payload>

```

### Event Types

#### `connected` — Initial connection confirmation
```
event: connected
data: {"status":"connected","topics":"0x0f","max_clients":3,"active_clients":1,"port":81,"watching":{"hr":16,"ir":0,"coils":0,"di":0}}
```

#### `counter` — Counter value change
```
event: counter
data: {"id":1,"value":12345,"enabled":true,"running":true}
```
Triggered when any counter's value or enabled state changes.

#### `timer` — Timer state change
```
event: timer
data: {"id":1,"enabled":true,"mode":"ASTABLE","output":true}
```
Triggered when any timer's output or enabled state changes.

#### `register` — Register/coil change (v7.0.1: all 4 types)
```
event: register
data: {"type":"hr","addr":0,"value":999}
```

Supported `type` values:
| Type | Description | Address range |
|------|-------------|---------------|
| `hr` | Holding Register | 0-159 |
| `ir` | Input Register | 0-159 |
| `coil` | Coil | 0-255 |
| `di` | Discrete Input | 0-255 |

Triggered when a watched address changes value. Configure which addresses to watch via query parameters (`hr`, `ir`, `coils`, `di`). Default: HR 0-15.

#### `heartbeat` — Keepalive (every 15 seconds)
```
event: heartbeat
data: {"uptime_ms":123456,"heap_free":102400,"sse_clients":1}
```

### SSE Status Endpoint (Main API, Port 80)

```
GET http://<ip>/api/events/status
```

Response:
```json
{
  "sse_enabled": true,
  "sse_port": 81,
  "max_clients": 3,
  "active_clients": 0,
  "check_interval_ms": 100,
  "heartbeat_ms": 15000,
  "topics": ["counters", "timers", "registers", "system"],
  "endpoint": "http://<ip>:81/api/events?subscribe=<topics>"
}
```

### Configuration

All SSE settings are configurable via CLI and persisted in NVS:

| Setting | CLI Command | Default | Range | Description |
|---------|------------|---------|-------|-------------|
| `sse_enabled` | `set sse enable\|disable` | enabled | on/off | Enable/disable SSE server |
| `sse_port` | `set sse port <port>` | `0` (auto) | 0-65535 | SSE port (0 = HTTP port + 1) |
| `sse_max_clients` | `set sse max-clients <n>` | `3` | 1-5 | Max simultaneous SSE clients |
| `sse_check_interval_ms` | `set sse interval <ms>` | `100` | 50-5000 | Change detection polling interval |
| `sse_heartbeat_ms` | `set sse heartbeat <ms>` | `15000` | 1000-60000 | Keepalive heartbeat interval |

**CLI eksempler:**
```
show sse                     — Vis SSE status og konfiguration
set sse disable              — Deaktiver SSE server
set sse port 82              — Brug port 82
set sse max-clients 2        — Max 2 samtidige klienter
set sse interval 200         — 5 Hz change detection (200ms)
set sse heartbeat 30000      — 30s heartbeat
save                         — Gem til NVS
reboot                       — Anvend ændringer
```

**API:** `GET /api/events/status` returnerer aktuel konfiguration og runtime-status.

### Limits & Performance

| Parameter | Default | Configurable | Range |
|-----------|---------|-------------|-------|
| Max simultaneous clients | 3 | Yes | 1-5 |
| Change detection interval | 100ms (10 Hz) | Yes | 50-5000ms |
| Heartbeat interval | 15 seconds | Yes | 1-60 seconds |
| Max watched addresses per type | 32 | No (compile-time) | — |
| Watched register types | HR, IR, Coils, DI | No | — |
| Default watch (no params) | HR 0-15 | No | — |
| Stack per SSE connection | 5 KB | No | — |
| Heap impact | ~12 KB (SSE server) | — | — |
| Min free heap for new client | 10 KB | No | — |
| Reconnect cooldown | 500ms | No | — |

### Usage Examples

**curl — all registers default (HR 0-15):**
```bash
curl -N -u api_user:password "http://10.1.32.20:81/api/events?subscribe=counters,registers"
```

**curl — specific addresses (v7.0.1):**
```bash
curl -N -u api_user:password "http://10.1.32.20:81/api/events?subscribe=registers&hr=0,5,10-15&coils=0-7&ir=0-3&di=0-3"
```

**JavaScript (EventSource):**
```javascript
const url = 'http://10.1.32.20:81/api/events?subscribe=all';
const headers = new Headers({'Authorization': 'Basic ' + btoa('api_user:password')});

const es = new EventSource(url, { headers });

es.addEventListener('counter', (e) => {
  const data = JSON.parse(e.data);
  console.log(`Counter ${data.id}: ${data.value}`);
});

es.addEventListener('register', (e) => {
  const data = JSON.parse(e.data);
  console.log(`HR[${data.addr}] = ${data.value}`);
});

es.addEventListener('heartbeat', (e) => {
  const data = JSON.parse(e.data);
  console.log(`Uptime: ${data.uptime_ms}ms, Heap: ${data.heap_free}`);
});
```

**Node-RED (native http module i Function node):**
```javascript
// Function node — Setup tab: libs = [{var: "http", module: "http"}]
const opts = {
  hostname: '10.1.32.20', port: 81,
  path: '/api/events?subscribe=registers&hr=0-15&coils=0-7',
  headers: { 'Authorization': 'Basic ' + Buffer.from('api_user:ChangeMe123!').toString('base64') }
};

const req = http.get(opts, (res) => {
  let buf = '';
  res.on('data', (chunk) => {
    buf += chunk.toString();
    const parts = buf.split('\n\n');
    buf = parts.pop();
    for (const part of parts) {
      const eventMatch = part.match(/^event: (.+)$/m);
      const dataMatch = part.match(/^data: (.+)$/m);
      if (eventMatch && dataMatch) {
        try {
          const data = JSON.parse(dataMatch[1]);
          node.send({ payload: {
            event: eventMatch[1],
            register: data.addr,
            value: data.value,
            type: data.type || eventMatch[1]
          }});
        } catch(e) {}
      }
    }
  });
});
req.on('error', () => setTimeout(() => {}, 5000));
```

Se også: `tests/sse_nodered_native.json` — importerbar Node-RED flow.

---

## FEAT-030: API Versioning

### Version Endpoint

```
GET /api/version
```

Response:
```json
{
  "api_version": 1,
  "api_version_str": "v1",
  "firmware_version": "7.0.0",
  "build": 1389,
  "min_supported_api": 1,
  "versioned_prefix": "/api/v1",
  "unversioned_prefix": "/api"
}
```

### Versioned Endpoints

All existing endpoints are available under `/api/v1/`:

```
GET /api/v1/status          ← same as GET /api/status
GET /api/v1/counters        ← same as GET /api/counters
GET /api/v1/counters/1      ← same as GET /api/counters/1
POST /api/v1/registers/hr/0 ← same as POST /api/registers/hr/0
...
```

### How It Works

The v1 dispatcher uses **URI rewriting**:

1. Request arrives at `/api/v1/counters/1`
2. URI is rewritten in-place to `/api/counters/1`
3. Routing table matches to the correct handler
4. Handler executes normally
5. URI is restored

This means:
- Zero code duplication — existing handlers are reused
- Full feature parity between `/api/*` and `/api/v1/*`
- All HTTP methods supported (GET, POST, DELETE)
- Auth, CORS, and error handling work identically

### Supported Routes

The v1 dispatcher supports all endpoints including:

**Exact match routes:**
- `/api/v1/status`, `/api/v1/config`, `/api/v1/counters`, `/api/v1/timers`
- `/api/v1/logic`, `/api/v1/gpio`, `/api/v1/wifi`, `/api/v1/ethernet`
- `/api/v1/debug`, `/api/v1/modules`, `/api/v1/hostname`, `/api/v1/telnet`
- `/api/v1/system/reboot`, `/api/v1/system/save`, `/api/v1/system/load`
- `/api/v1/system/defaults`, `/api/v1/system/watchdog`
- `/api/v1/system/backup`, `/api/v1/system/restore`
- `/api/v1/http`, `/api/v1/logic/settings`
- `/api/v1/registers/hr/bulk`, `/api/v1/registers/coils/bulk`

**Wildcard routes (with ID/address):**
- `/api/v1/counters/{id}`, `/api/v1/timers/{id}`, `/api/v1/logic/{id}`
- `/api/v1/registers/hr/{addr}`, `/api/v1/registers/ir/{addr}`
- `/api/v1/registers/coils/{addr}`, `/api/v1/registers/di/{addr}`
- `/api/v1/gpio/{pin}`, `/api/v1/modbus/{slave|master}`
- `/api/v1/wifi/{connect|disconnect}`

### Error Handling

```
GET /api/v1/nonexistent → 404 {"error": "Endpoint not found in API v1", "status": 404}
GET /api/v1/status (no auth) → 401 {"error": "Authentication required", "status": 401}
```

### Migration Guide

**No migration needed.** Existing code using `/api/*` continues to work.

To adopt versioned endpoints:
1. Query `GET /api/version` to confirm API version
2. Prefix all API calls with `/api/v1/`
3. When v2 is released, gradually migrate to `/api/v2/` endpoints

---

## New Files

| File | Description |
|------|-------------|
| `include/sse_events.h` | SSE subsystem interface |
| `src/sse_events.cpp` | SSE server, event handler, change detection |

## Modified Files

| File | Changes |
|------|---------|
| `include/constants.h` | `PROJECT_VERSION` → "7.0.0", version history |
| `include/types.h` | `HttpConfig.sse_port` field added |
| `include/api_handlers.h` | New handler declarations |
| `src/api_handlers.cpp` | Version endpoint, v1 dispatcher, discovery update |
| `src/http_server.cpp` | SSE status + v1 URI registrations |
| `src/network_manager.cpp` | SSE server startup |
| `src/network_config.cpp` | Default `sse_port = 0` |
| `src/config_load.cpp` | Schema migration for `sse_port` |
| `src/main.cpp` | `sse_init()` call |

## Test Results

**40/40 PASS (100%)** — See [tests/FEAT023_FEAT030_TEST_RESULTS.md](../tests/FEAT023_FEAT030_TEST_RESULTS.md)

## Resource Impact

| Metric | v6.3.0 | v7.0.0 | Delta |
|--------|--------|--------|-------|
| RAM | 33.5% | 33.6% | +80 bytes |
| Flash | 68.4% | 68.5% | +1.4 KB |
| Free Heap | ~114 KB | ~102 KB | -12 KB (SSE server) |
