# FEAT-031: Firmware OTA via API — Implementeringsplan

**Oprettet:** 2026-03-27
**Status:** IMPLEMENTERET (2026-03-28)
**Build:** SUCCESS — RAM 33.7%, Flash 88.0% (204KB headroom i OTA partition)
**Prioritet:** HIGH
**Estimeret filer:** 6 nye/ændrede

---

## Oversigt

Remote firmware update via HTTP API uden fysisk adgang. Chunked upload til flash, med rollback-sikkerhed og web UI.

---

## Forudsætning: Partition Table (BREAKING CHANGE)

### Nuværende layout (INGEN OTA support)
```
phy_init    data  phy      0x9000    0x1000    (4KB)
app0        app   factory  0x10000   0x1E0000  (1.875MB single app)
nvs         data  nvs      0x1F0000  0x10000   (64KB)
spiffs      data  spiffs   0x200000  0x200000  (2MB)
```

### Ny OTA-layout (4MB flash)
```
phy_init    data  phy      0x9000    0x1000    (4KB)
otadata     data  ota      0xA000    0x2000    (8KB - krævet af ESP-IDF)
ota_0       app   ota_0    0x10000   0x1A0000  (1.625MB)
ota_1       app   ota_1    0x1B0000  0x1A0000  (1.625MB)
nvs         data  nvs      0x350000  0x10000   (64KB)
spiffs      data  spiffs   0x360000  0xA0000   (640KB - reduceret fra 2MB)
```

- Firmware er ~1.42MB -> 1.625MB partition giver ~200KB headroom
- **ALLE eksisterende enheder skal seriel-flashes en gang** for ny partition table
- SPIFFS reduceret fra 2MB til 640KB (verificer at ingen feature bruger stor SPIFFS)

---

## Fase 1: Partition Table + Build Config

| Fil | Handling |
|-----|---------|
| `partitions.csv` | Erstat med ny OTA-dual layout |
| `platformio.ini` | Tilføj `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` |
| `include/constants.h` | Tilføj OTA konstanter |

### Nye konstanter
```c
#define OTA_CHUNK_SIZE          4096      // Flash write chunk size
#define OTA_MAX_FIRMWARE_SIZE   0x1A0000  // Skal matche partition size (1.625MB)
#define OTA_REBOOT_DELAY_MS     2000      // Delay før reboot efter OTA
```

---

## Fase 2: OTA Handler (kerne-logik)

### Nye filer
- `include/ota_handler.h`
- `src/ota_handler.cpp`

### API Endpoints

#### POST /api/system/ota — Upload firmware
1. `CHECK_AUTH(req)` — eksisterende auth
2. Valider `Content-Length` <= partition size
3. `esp_ota_begin()` — åbn OTA partition
4. Loop: `httpd_req_recv(4KB)` -> `esp_ota_write()` (chunked, ingen fuld buffering)
5. Første chunk: valider magic byte `0xE9` + `esp_image_header_t`
6. `esp_ota_end()` — ESP-IDF verificerer SHA-256 automatisk
7. `esp_ota_set_boot_partition()` -> svar med success JSON -> reboot efter 2s delay

#### GET /api/system/ota/status — Poll progress
Returnerer JSON: `{ state, received, total, percent, error, new_version }`

#### POST /api/system/ota/rollback — Rul tilbage
Kalder `esp_ota_mark_app_invalid_rollback_and_reboot()`

### Progress tracking struct
```c
static struct {
    volatile uint8_t state;      // OTA_IDLE, OTA_RECEIVING, OTA_VERIFYING, OTA_DONE, OTA_ERROR
    volatile uint32_t received;  // bytes modtaget
    volatile uint32_t total;     // total forventet (content_len)
    char error_msg[64];          // sidste fejlbesked
    char new_version[32];        // version fra uploaded firmware
} ota_state;
```

### Sikkerhed
- Atomic "OTA in progress" flag — afvis concurrent uploads
- Chunk buffer allokeres med `malloc(4096)` + `free()` (ikke på stack)
- Socket timeout sættes til 60s under OTA upload

---

## Fase 3: Route Registration

| Fil | Handling |
|-----|---------|
| `src/http_server.cpp` | 3 nye API routes + 1 web page route |
| `src/api_handlers.cpp` | Tilføj til `/api` discovery listing |

### Nye routes
```c
POST /api/system/ota          -> api_handler_ota_upload
GET  /api/system/ota/status   -> api_handler_ota_status
POST /api/system/ota/rollback -> api_handler_ota_rollback
GET  /ota                     -> web_ota_handler
```

Handler-count: 72 -> 76 (max 80, OK)

---

## Fase 4: Web UI — OTA Upload Side

### Nye filer
- `include/web_ota.h`
- `src/web_ota.cpp`

### Features
- Serveret på `/ota`
- HTML som `PROGMEM` string (samme mønster som web_dashboard, web_system)
- Fil-input til `.bin` selection
- Upload med `XMLHttpRequest` + `xhr.upload.onprogress` progressbar
- Poll `/api/system/ota/status` hver 500ms under upload
- Rollback-knap
- Nuværende firmware version display (fra `/api/status`)
- Konsistent navigation (dashboard, editor, system, **OTA**)
- Login modal (samme mønster som andre sider)

---

## Fase 5: Boot Self-Validation (Rollback sikkerhed)

| Fil | Handling |
|-----|---------|
| `src/main.cpp` | Tilføj `esp_ota_mark_app_valid_cancel_rollback()` efter succesfuld init |
| `platformio.ini` | Tilføj `-DCONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1` build flag |

### Flow
1. OTA upload -> reboot til ny firmware
2. Ny firmware starter -> initialiserer alle subsystemer
3. Hvis init OK -> `esp_ota_mark_app_valid_cancel_rollback()` -> firmware bekræftet
4. Hvis crash under boot -> bootloader ruller automatisk tilbage til forrige partition

---

## Fase-rækkefølge og afhængigheder

```
Fase 1 (partition)  -->  Fase 2 (handler)  -->  Fase 3 (routes)
                          |  parallel              |
                         Fase 6 (constants)     Fase 5 (boot validation)
                          |  parallel
                         Fase 4 (web UI)
```

Fase 1 SKAL være først. Fase 2+4+6 kan paralleliseres. Fase 3+5 kræver Fase 2.

---

## Heap Impact Analyse

### Peak forbrug under OTA upload

| Komponent | Bytes | Varighed |
|-----------|-------|----------|
| `httpd_req_recv` chunk buffer | 4096 | Under upload (malloc/free) |
| `esp_ota_begin()` intern state | ~200 | Hele OTA sessionen |
| `ota_state` struct (progress) | ~108 | Permanent (static) |
| ESP-IDF flash write buffer | ~4096 | Under `esp_ota_write()` |
| HTTP response buffer | ~256 | Kortvarigt |
| **Total peak** | **~8.7KB** | |

### Sammenligning med eksisterende operationer

| Operation | Peak heap |
|-----------|-----------|
| `system_restore` | 32KB (fuld body i RAM) |
| ST Logic compile | 8KB+ (source + parser + compiler) |
| SSE client task | 5KB stack + buffers |
| **OTA upload** | **~8.7KB** (chunked) |

### Permanent overhead

| Komponent | Bytes | Type |
|-----------|-------|------|
| `ota_state` struct | ~108 | Static RAM (BSS) |
| 3 nye `httpd_uri_t` registreringer | ~144 | Static RAM |
| OTA handler kode | ~3-4KB | Flash (ikke RAM) |
| Web UI HTML (PROGMEM) | ~5-8KB | Flash (ikke RAM) |
| **Total permanent RAM** | **~252 bytes** | |

**Konklusion:** OTA er heap-venlig. ~8.7KB peak (midlertidig), ~252 bytes permanent.

---

## Risici og mitigering

| Risiko | Mitigering |
|--------|-----------|
| Firmware vokser over 1.625MB | Monitor størrelse, reducer SPIFFS yderligere |
| HTTP timeout ved langsom upload | Sæt socket timeout til 60s under OTA |
| Concurrent OTA requests | Atomic "OTA in progress" flag |
| SPIFFS reduceret til 640KB | Verificer ingen feature bruger stor SPIFFS |
| Første deployment | Dokumenter one-time seriel flash migration |
| Chunk buffer på stack (8KB stack) | Brug malloc/free i stedet |
| Flash writes blokerer ~20-40ms | Andre FreeRTOS tasks kører fint, kun HTTP task blokeret |

---

## Filer der oprettes/ændres

### Nye filer
1. `include/ota_handler.h` — OTA handler declarations
2. `src/ota_handler.cpp` — OTA kerne-logik (upload, status, rollback)
3. `include/web_ota.h` — Web OTA page handler declaration
4. `src/web_ota.cpp` — OTA web UI (PROGMEM HTML)

### Ændrede filer
5. `partitions.csv` — Ny OTA-dual partition layout
6. `platformio.ini` — Rollback build flag
7. `include/constants.h` — OTA konstanter
8. `src/http_server.cpp` — Route registration (4 nye routes)
9. `src/api_handlers.cpp` — API discovery listing
10. `src/main.cpp` — Boot self-validation

---

## Test-plan

1. [ ] Byg med ny partition table — verificer firmware < 1.625MB
2. [ ] Seriel flash til enhed med ny partition layout
3. [ ] Upload firmware via `curl -X POST -F "file=@firmware.bin" /api/system/ota`
4. [ ] Verificer progress via `GET /api/system/ota/status`
5. [ ] Verificer automatisk reboot efter upload
6. [ ] Verificer boot self-validation (firmware bekræftet)
7. [ ] Test rollback via `POST /api/system/ota/rollback`
8. [ ] Test fejlhåndtering: for stor fil, ugyldig .bin, timeout
9. [ ] Test concurrent upload afvisning
10. [ ] Test web UI upload + progressbar
11. [ ] Verificer heap-forbrug under OTA (`GET /api/status`)
