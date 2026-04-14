# Modbus Master Async Cache — Funktionsbeskrivelse

**Version:** v7.9.7.1 (2026-04-14)
**Filer:** `mb_async.h`, `mb_async.cpp`, `st_builtin_modbus.cpp`

---

## Overblik

Modbus Master Cache er et asynkront read-through cache-system der eliminerer blokering i ST Logic og CLI ved Modbus RTU kommunikation. Systemet kører på en dedikeret FreeRTOS task (Core 0), mens ST Logic kører på Core 1.

```
┌─────────────────────────────────────────────────────────┐
│  Core 1 (ST Logic / CLI / HTTP)                         │
│                                                         │
│   MB_READ_REG(slave, addr)                              │
│       │                                                 │
│       ├── Cache HIT (VALID) → returnér cached værdi     │
│       │   + køer refresh i baggrunden (PRIO_REFRESH)    │
│       │                                                 │
│       └── Cache MISS (EMPTY) → returnér 0               │
│           + køer fresh read (PRIO_READ_FRESH)           │
│                                                         │
│   MB_WRITE_REG(slave, addr, val)                        │
│       └── Køer write (PRIO_WRITE) → returnér straks     │
│                                                         │
├─────────────── spinlock ────────────────────────────────┤
│                                                         │
│  Core 0 (mb_async_task — FreeRTOS baggrundstask)        │
│                                                         │
│   Priority Queue → dequeue højeste prioritet            │
│       │                                                 │
│       ├── Adaptive backoff check (skip hvis slave i     │
│       │   cooldown efter gentagne timeouts)             │
│       │                                                 │
│       ├── UART TX/RX (Modbus RTU request/response)      │
│       │                                                 │
│       └── Opdatér cache entry med resultat              │
│           (VALID + værdi eller ERROR + fejlkode)        │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

---

## Priority Queue (v7.9.7)

Køen bruger 3 prioritetsniveauer. Højeste prioritet behandles først:

| Prioritet | Navn | Beskrivelse | Eksempel |
|-----------|------|-------------|----------|
| **0** (højest) | `WRITE` | Skrive-operationer (FC05/FC06/FC16) | `MB_WRITE_REG(1, 100, 42)` |
| **1** | `READ_FRESH` | Første læsning — ingen cached værdi endnu | `MB_READ_REG(1, 200)` når addr 200 aldrig er læst |
| **2** (lavest) | `READ_REFRESH` | Cache-opdatering — klient har allerede en værdi | `MB_READ_REG(1, 200)` når addr 200 har cached værdi |

### Eviction ved fuld kø

Når køen er fuld (standard: 32 pladser):

1. Find den **nyeste** entry med **lavest** prioritet (højeste tal)
2. Hvis den har lavere prioritet end den nye request → erstat den
3. Hvis samme prioritet → den nye request droppes (den er nyest)
4. Hvis den nye request har lavest prioritet → droppes

**Effekt:** Writes går altid igennem. Fresh reads foretrækkes over refreshes. Ved høj belastning droppes cache-refreshes — klienter beholder deres eksisterende cached værdier lidt længere.

### Deduplication

- **Reads:** Hvis en cache entry allerede er `PENDING`, køes der ikke en ny request (deduplication)
- **Writes:** Hvis cache allerede viser samme værdi som `VALID`, skippes skrivningen

---

## Cache Entries

Hver cache entry indeholder:

| Felt | Beskrivelse |
|------|-------------|
| `slave_id` | Modbus slave adresse (1-247) |
| `address` | Register/coil adresse (0-65535) |
| `req_type` | FC type (FC01-FC04 for reads) |
| `value` | Cached værdi (4 bytes: int32/float/bool) |
| `status` | `EMPTY` → `PENDING` → `VALID` / `ERROR` |
| `last_error` | Fejlkode fra sidste operation |
| `last_update_ms` | Tidspunkt for sidste opdatering |
| `last_fc` | Faktisk FC brugt (FC01-FC06, FC16) |

### Cache Status Flow

```
EMPTY ──(queue read)──> PENDING ──(success)──> VALID
                            │                    │
                            └──(error)──> ERROR  │
                                                 │
VALID ──(queue refresh)──> PENDING ──(success)──> VALID
                              │
                              └──(error)──> ERROR
```

### LRU Eviction

Når cache er fuld (standard: 32 entries):
- Den **ældste** entry (laveste `last_update_ms`) evictes
- `PENDING` entries springes over (aktiv request i gang)

---

## Adaptive Backoff

For at undgå at flooding af bussen med requests til slaves der ikke svarer:

| Event | Effekt |
|-------|--------|
| Timeout | Backoff fordobles (50 → 100 → 200 → ... → 2000ms max) |
| Success | Backoff reduceres med 100ms per success |

Når en slave har aktiv backoff:
- Requests til den slave **skippes** (ikke blokeret med delay)
- Cache entry sættes til `ERROR` med `MB_TIMEOUT`
- Andre slaves' requests behandles uberørt

Maks 8 slaves trackes i backoff-tabellen. Ved fuld tabel evictes slaven med lavest backoff.

---

## Konfiguration

| Parameter | Standard | Beskrivelse |
|-----------|----------|-------------|
| `MB_CACHE_MAX_ENTRIES` | 32 | Max unikke (slave, addr, fc) kombinationer |
| `MB_ASYNC_QUEUE_SIZE` | 32 | Max pending requests i priority queue |
| `MB_ASYNC_TASK_STACK` | 4096 | Background task stack (bytes) |
| `MB_ASYNC_TASK_PRIO` | 3 | Task prioritet (under WiFi, over idle) |
| `MB_ASYNC_TASK_CORE` | 0 | Kører på Core 0 (main loop = Core 1) |
| `cache_ttl_ms` | 0 | Cache TTL i ms (0 = aldrig expire) — konfigurérbar via CLI |

---

## Statistik & Monitoring

### Prometheus Metrics

| Metric | Type | Beskrivelse |
|--------|------|-------------|
| `modbus_master_cache_hits` | counter | Cache hit tæller |
| `modbus_master_cache_misses` | counter | Cache miss tæller |
| `modbus_master_cache_entries` | gauge | Aktive cache entries |
| `modbus_master_cache_hit_rate` | gauge | Hit rate i procent |
| `modbus_master_cache_utilization` | gauge | Cache slot utilization i procent |
| `modbus_master_queue_depth` | gauge | Aktuel kø-dybde |
| `modbus_master_queue_hwm` | gauge | Queue high watermark (max dybde set) |
| `modbus_master_queue_full_count` | counter | Requests droppet pga. fuld kø |
| `modbus_master_priority_drops` | counter | Requests droppet af priority eviction |
| `modbus_master_slave_status` | gauge | Per-slave cache entry status med labels |
| `modbus_master_slave_backoff` | gauge | Per-slave backoff status |

### CLI

```
mb stats         # Vis async cache statistik
mb cache         # Vis alle cache entries
mb reset         # Nulstil cache + statistik
```

### Dashboard

Modbus Master kortet på web dashboard viser:
- Cache entries / max + utilization procent
- Cache hit rate
- Queue depth / max + high watermark
- Queue full drops + priority drops
- Per-slave cache tabel (slave, addr, FC, status, age)
- Adaptive backoff tabel

---

## Thread Safety

Cache tilgås fra begge cores via `portMUX_TYPE mb_cache_spinlock`:
- **Core 1** (ST Logic): læser cache entries, sætter status til PENDING
- **Core 0** (async task): skriver cache entries, sætter status til VALID/ERROR

Priority queue bruger FreeRTOS mutex + counting semaphore:
- **Mutex**: beskytter queue array mod samtidige insert/dequeue
- **Semaphore**: signalerer consumer-task om nye items (blokerer max 100ms)

---

## Multi-Register Operationer

### FC03 Multi-Read (`MB_READ_REGS`)
- Læser N consecutive holding registers i én bus-transaktion
- Opdaterer N individuelle cache entries (en per adresse)

### FC16 Multi-Write (`MB_WRITE_REGS`)
- Skriver N consecutive holding registers via FC16
- Bruger ring-buffer pool (4 slots × 16 regs) for write-værdier
- Opdaterer N individuelle cache entries ved succes

---

## Typisk Dataflow

**Eksempel: ST Logic program læser 3 Modbus registers hvert 100ms**

```
Cycle 1: MB_READ_REG(1, 100) → MISS → queue FRESH, return 0
         MB_READ_REG(1, 101) → MISS → queue FRESH, return 0
         MB_READ_REG(1, 102) → MISS → queue FRESH, return 0

  [baggrund: bus read slave 1 addr 100 → cache VALID = 1234]
  [baggrund: bus read slave 1 addr 101 → cache VALID = 5678]
  [baggrund: bus read slave 1 addr 102 → cache VALID = 9012]

Cycle 2: MB_READ_REG(1, 100) → HIT → return 1234 + queue REFRESH
         MB_READ_REG(1, 101) → HIT → return 5678 + queue REFRESH
         MB_READ_REG(1, 102) → HIT → return 9012 + queue REFRESH
         (REFRESH har laveste prioritet — behandles efter writes og fresh reads)

  [baggrund: refresh slave 1 addr 100 → cache VALID = 1234 (uændret)]
  ...

Cycle N: Alle reads returnerer cached værdier instant (0ms)
         Baggrundstask opdaterer i sit eget tempo
```

**Resultat:** ST Logic cycle time er ~0ms for Modbus reads (i stedet for 50-500ms per read med synkron I/O).
