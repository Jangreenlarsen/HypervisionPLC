# API v7.0.0 — SSE Real-Time Events & API Versioning

**Version:** v7.0.0 | **Build:** #1389 | **Date:** 2026-03-17

---

## Overview

v7.0.0 introduces two major API features:

1. **FEAT-023: Server-Sent Events (SSE)** — Push-based real-time updates, eliminates polling
2. **FEAT-030: API Versioning** — `/api/v1/*` prefix support for backward compatibility

Both features are fully backward-compatible. Existing `/api/*` endpoints work unchanged.

---

## FEAT-023: SSE Real-Time Events

### Architecture

SSE runs on a **dedicated httpd server** on a separate port (default: 81). This prevents the blocking SSE stream from blocking the main REST API on port 80.

```
Port 80 (main)     Port 81 (SSE)
  ┌──────────┐       ┌──────────┐
  │  httpd   │       │  httpd   │
  │ REST API │       │ SSE only │
  │ 56+ URIs │       │ 1 URI    │
  └──────────┘       └──────────┘
       │                   │
       └───── Shared ──────┘
         counter_engine
         timer_engine
         registers
```

### Endpoint

```
GET http://<ip>:81/api/events?subscribe=<topics>
```

**Query Parameters:**
| Parameter | Values | Default |
|-----------|--------|---------|
| `subscribe` | `counters`, `timers`, `registers`, `system`, `all` | `all` |

Multiple topics can be comma-separated: `?subscribe=counters,timers`

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
data: {"status":"connected","topics":"0x0f","max_clients":3,"active_clients":1,"port":81}
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

#### `register` — Holding register change
```
event: register
data: {"type":"hr","addr":0,"value":999}
```
Monitors holding registers 0-15. Triggered when value changes.

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

SSE port is configurable via `HttpConfig.sse_port`:

| Setting | Default | Description |
|---------|---------|-------------|
| `sse_port` | `0` (auto = main port + 1) | SSE server port. Set to specific port or 0 for auto |

**CLI:** `set http sse-port <port>` (future)
**API:** `POST /api/http` with `{"sse_port": 82}` (future)

### Limits & Performance

| Parameter | Value |
|-----------|-------|
| Max simultaneous clients | 3 |
| Change detection interval | 100ms (10 Hz) |
| Heartbeat interval | 15 seconds |
| Monitored HR range | 0-15 (16 registers) |
| Stack per SSE connection | 6 KB |
| Heap impact | ~12 KB (SSE server) |

### Usage Examples

**curl:**
```bash
curl -N -u api_user:password http://10.1.32.20:81/api/events?subscribe=counters,registers
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

**Node-RED:**
Use an HTTP Request node configured as SSE with Basic Auth to port 81.

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
