# FEAT-023 + FEAT-030 Test Results (v7.0.0)

**Date:** 2026-03-17
**Firmware:** v7.0.0 Build #1389
**Device:** ESP32 at 10.1.32.20 (Ethernet W5500)
**Auth:** Basic Auth (api_user)

## Summary: 40/40 PASS (100%)

## FEAT-030: API Versioning (28 tests)

### API Version Endpoint
| # | Test | Result |
|---|------|--------|
| 1 | GET /api/version | PASS |
| 2 | Version = 7.0.0 | PASS |

### /api/v1/* GET Routing (12 endpoints)
| # | Test | Result |
|---|------|--------|
| 3 | v1/status | PASS |
| 4 | v1/counters | PASS |
| 5 | v1/timers | PASS |
| 6 | v1/logic | PASS |
| 7 | v1/modules | PASS |
| 8 | v1/hostname | PASS |
| 9 | v1/debug | PASS |
| 10 | v1/wifi | PASS |
| 11 | v1/ethernet | PASS |
| 12 | v1/telnet | PASS |
| 13 | v1/system/watchdog | PASS |
| 14 | v1/system/backup | PASS |

### /api/v1/* POST/Write Operations
| # | Test | Result |
|---|------|--------|
| 15 | v1 write HR[0]=42 | PASS |
| 16 | v1 read HR[0]=42 (verify) | PASS |
| 17 | v1 restore HR[0]=0 | PASS |
| 18 | v1 write coil 0 | PASS |
| 19 | v1 read coil 0 | PASS |
| 20 | v1 restore coil 0 | PASS |
| 21 | v1 read IR[0] | PASS |
| 22 | v1 read DI[0] | PASS |

### /api/v1/* Wildcard Resource IDs
| # | Test | Result |
|---|------|--------|
| 23 | v1/logic/1 | PASS |
| 24 | v1/counters/1 | PASS |
| 25 | v1/timers/1 | PASS |
| 26 | v1/modbus/slave | PASS |

### Auth + Error Handling
| # | Test | Result |
|---|------|--------|
| 27 | v1 rejects unauthenticated | PASS |
| 28 | v1/nonexistent -> 404 | PASS |

### Backward Compatibility
| # | Test | Result |
|---|------|--------|
| 29 | /api/status (unversioned) | PASS |
| 30 | /api/counters (unversioned) | PASS |
| 31 | /api/timers (unversioned) | PASS |
| 32 | Discovery includes FEAT-023/030 | PASS |

## FEAT-023: SSE Real-Time Events (8 tests)

### SSE Status (port 80)
| # | Test | Result |
|---|------|--------|
| 33 | GET /api/events/status | PASS |
| 34 | SSE port=81 | PASS |
| 35 | SSE max_clients=3 | PASS |

### SSE Connection (port 81)
| # | Test | Result |
|---|------|--------|
| 36 | SSE connected event received | PASS |
| 37 | SSE data format correct | PASS |

### SSE Change Detection
| # | Test | Result |
|---|------|--------|
| 38 | SSE detects HR[0] change (555) | PASS |

### SSE Security + Filtering
| # | Test | Result |
|---|------|--------|
| 39 | SSE rejects unauthenticated | PASS |
| 40 | SSE topic filtering (counters=0x01) | PASS |

## Architecture Notes

- **SSE runs on dedicated httpd** (port 81, configurable via `sse_port`)
- Main API on port 80 remains fully responsive during SSE streaming
- SSE server has own FreeRTOS task — no blocking of main API
- Change detection: 10 Hz polling (100ms interval)
- Heartbeat keepalive every 15 seconds
- Max 3 simultaneous SSE clients
- Memory impact: ~12KB extra heap for SSE server

## Resource Usage
- **RAM:** 33.6% (110188 / 327680 bytes)
- **Flash:** 68.5% (1345833 / 1966080 bytes)
- **Free Heap:** ~102KB (with SSE server running)
