// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file expansion_api_client.cpp
 * @brief FEAT-409: PLC-side klient mod en "HypervisionPLC Extension Board"s
 * management-API. Se expansion_api_client.h for den fulde designbegrundelse.
 *
 * Udgående HTTP-arkitektur er BEVIDST kopieret fra src/ota_handler.cpp's
 * github_check_worker()-mønster (denne repos ENESTE tidligere udgående
 * HTTP-klient, se SECURITY_INDEX.md #19/#20): selve netværksarbejdet ligger
 * i en "_do_work()"-funktion der returnerer NORMALT (så lokale C++-objekter
 * som HTTPClient destrueres korrekt FØR task-entry'en kalder vTaskDelete()) —
 * BUG-364-klassen af fejl (mbedTLS/HTTPClient-objekter der aldrig frigøres
 * fordi vTaskDelete() rives ned midt i deres levetid) undgås dermed samme
 * sted som originalen fandt den.
 *
 * Forskel fra github-check: almindelig HTTP (ikke HTTPS) mod en LAN-lokal
 * enhed — ingen TLS-håndtryk, ingen CA-bundle. Se SECURITY_INDEX.md #20 for
 * hvorfor det er en forskellig, men stadig bevidst, tillidsmodel end
 * GitHub-kaldet (netværkssegmentering, ikke transportkryptering, er
 * grænsen her — samme model som expansion-boardets eget designdokument).
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "expansion_api_client.h"
#include "config_struct.h"
#include "network_config.h"
#include "debug.h"
#include "modbus_expansion.h"
#include "ethernet_driver.h"
#include "network_manager.h"

/* ============================================================================
 * BOARD CRUD
 * ============================================================================ */

// FEAT-409c: hele det validerede allow-list af kendte board-typer + et
// menneskelaesbart label pr. type (bruges af REST/web-UI's type-dropdown, se
// GET /api/expansion/board-types). Tilføj en ny {value,label}-linje her NÅR
// et nyt fysisk board-design faktisk findes (husk ALSO EXPANSION_BOARD_TYPE_*
// i constants.h) — se types.h's ExpansionBoard-designnote for hvorfor der
// ikke gættes på fremtidige typers detaljer på forhånd.
typedef struct { const char *value; const char *label; } ExpansionBoardTypeInfo;
static const ExpansionBoardTypeInfo EXPANSION_BOARD_KNOWN_TYPES[] = {
  // FEAT-466: værdien "modbus_2ch" bevares (gemt i NVS/backups); det faktiske kanaltal
  // (2, eller 4 med CJMCU-752) aflæses fra boardet selv (active_channels/board_type).
  { EXPANSION_BOARD_TYPE_MODBUS_2CH, "Modbus Expansion (RS485/RS232, 2 eller 4 kanaler)" },
};
#define EXPANSION_BOARD_KNOWN_TYPES_COUNT (sizeof(EXPANSION_BOARD_KNOWN_TYPES) / sizeof(EXPANSION_BOARD_KNOWN_TYPES[0]))

bool expansion_board_type_valid(const char *type) {
  if (!type || !*type) return false;
  for (size_t i = 0; i < EXPANSION_BOARD_KNOWN_TYPES_COUNT; i++) {
    if (strcmp(type, EXPANSION_BOARD_KNOWN_TYPES[i].value) == 0) return true;
  }
  return false;
}

bool expansion_board_type_list(uint8_t index, const char **out_value, const char **out_label) {
  if (index >= EXPANSION_BOARD_KNOWN_TYPES_COUNT) return false;
  if (out_value) *out_value = EXPANSION_BOARD_KNOWN_TYPES[index].value;
  if (out_label) *out_label = EXPANSION_BOARD_KNOWN_TYPES[index].label;
  return true;
}

// BUG-435: navne vises i web-GUI'et — afvis HTML-specialtegn og kontroltegn
static bool display_name_ok(const char *s) {
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x20 || c == 0x7F || c == '<' || c == '>' || c == '"' || c == 0x27 || c == '&') return false;
  }
  return true;
}

int expansion_board_add(uint8_t number, const char *board_type, const char *name, const char *ip_str, const char *token) {
  if (number < 1 || number > EXPANSION_BOARD_MAX) return -1;
  if (!name || !*name || !ip_str || !*ip_str || !token || !*token) return -1;
  if (strlen(name) >= EXPANSION_BOARD_NAME_MAX || !display_name_ok(name)) return -1;
  if (strlen(token) >= EXPANSION_TOKEN_MAX) return -1;
  if (!expansion_board_type_valid(board_type)) return -1;

  uint8_t i = number - 1;
  if (g_persist_config.expansion_boards[i].configured) {
    debug_printf("FEJL: Board nr %u er allerede i brug\n", number);
    return -1;
  }

  uint32_t ip;
  if (!network_config_str_to_ip(ip_str, &ip)) return -1;

  ExpansionBoard *b = &g_persist_config.expansion_boards[i];
  memset(b, 0, sizeof(*b));
  b->configured = 1;
  strncpy(b->name, name, EXPANSION_BOARD_NAME_MAX - 1);
  strncpy(b->token, token, EXPANSION_TOKEN_MAX - 1);
  strncpy(b->board_type, board_type, EXPANSION_BOARD_TYPE_MAX - 1);
  b->ip = ip;
  if (i >= g_persist_config.expansion_board_count) {
    g_persist_config.expansion_board_count = i + 1;
  }
  debug_printf("[OK] Expansion board nr %u ('%s', type=%s) tilfoejet\n", number, name, board_type);
  debug_println("NOTE: Use 'save' to persist to NVS");
  return i;
}

bool expansion_board_edit(uint8_t index, const char *name, const char *ip_str, const char *token, const char *board_type) {
  if (index >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[index].configured) return false;
  if (!name || !*name || strlen(name) >= EXPANSION_BOARD_NAME_MAX || !display_name_ok(name)) return false;
  uint32_t ip;
  if (!ip_str || !*ip_str || !network_config_str_to_ip(ip_str, &ip)) return false;
  if (board_type && *board_type && !expansion_board_type_valid(board_type)) return false;

  ExpansionBoard *b = &g_persist_config.expansion_boards[index];
  memset(b->name, 0, sizeof(b->name));
  strncpy(b->name, name, EXPANSION_BOARD_NAME_MAX - 1);
  b->ip = ip;
  if (token && *token) {
    if (strlen(token) >= EXPANSION_TOKEN_MAX) return false;
    memset(b->token, 0, sizeof(b->token));
    strncpy(b->token, token, EXPANSION_TOKEN_MAX - 1);
  }
  if (board_type && *board_type) {
    memset(b->board_type, 0, sizeof(b->board_type));
    strncpy(b->board_type, board_type, EXPANSION_BOARD_TYPE_MAX - 1);
  }
  debug_printf("[OK] Expansion board %u opdateret\n", index);
  debug_println("NOTE: Use 'save' to persist to NVS");
  return true;
}

bool expansion_board_remove(uint8_t index) {
  if (index >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[index].configured) return false;
  memset(&g_persist_config.expansion_boards[index], 0, sizeof(ExpansionBoard));
  // expansion_board_count er bevidst en simpel "hoejeste index+1"-taeller
  // (samme letvaegts moenster som acl_rule_count) — vi trimmer den her hvis
  // det fjernede slot var det sidste, ellers efterlader vi et hul (slots
  // scannes via .configured, ikke et taet 0..count-1-interval).
  if (index == g_persist_config.expansion_board_count - 1) {
    while (g_persist_config.expansion_board_count > 0 &&
           !g_persist_config.expansion_boards[g_persist_config.expansion_board_count - 1].configured) {
      g_persist_config.expansion_board_count--;
    }
  }
  debug_printf("[OK] Expansion board %u fjernet\n", index);
  debug_println("NOTE: Use 'save' to persist to NVS");
  return true;
}

int expansion_board_find_by_name(const char *name) {
  if (!name || !*name) return -1;
  for (uint8_t i = 0; i < EXPANSION_BOARD_MAX; i++) {
    if (g_persist_config.expansion_boards[i].configured &&
        strcasecmp(g_persist_config.expansion_boards[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}

bool expansion_board_ip_str(uint8_t index, char *out, size_t out_size) {
  if (index >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[index].configured) return false;
  if (!out || out_size < 16) return false;
  char buf[16];
  network_config_ip_to_str(g_persist_config.expansion_boards[index].ip, buf);
  strncpy(out, buf, out_size - 1);
  out[out_size - 1] = '\0';
  return true;
}

/* ============================================================================
 * ASYNC MANAGEMENT-API-KALD
 * ============================================================================ */

static const char *TAG = "EXP_API";

static SemaphoreHandle_t g_expansion_api_sem = NULL;
static ExpansionApiResult g_expansion_api_result;

// Sat af *_start()-funktionerne lige foer task'en spawnes — laeses KUN af
// expansion_api_do_work() paa selve baggrundstasken, kopieret fra
// PersistConfig paa kalde-tidspunktet (ikke laest paa ny fra den globale
// config inde i tasken) for at undgaa en race hvis brugeren redigerer/
// fjerner boardet mens et kald allerede er i gang.
struct ExpansionApiPendingRequest {
  uint8_t board_index;
  char    ip[16];
  char    token[EXPANSION_TOKEN_MAX];
  char    method[8];     // "GET", "POST", "PUT"
  char    path[64];      // fx "/api/status", "/api/channels/1/config"
  char    body[512];     // JSON request-body (tom streng for GET)
  char    kind[24];
};
static ExpansionApiPendingRequest g_pending;

// Nulstiller resultat-slottet og markerer det "in flight" for board_index/kind.
// Kalderen har allerede tjekket at intet andet kald er i gang.
static void expansion_api_claim_slot(uint8_t board_index, const char *kind) {
  memset(&g_expansion_api_result, 0, sizeof(g_expansion_api_result));
  g_expansion_api_result.valid = true;
  g_expansion_api_result.in_progress = true;
  g_expansion_api_result.board_index = board_index;
  strncpy(g_expansion_api_result.kind, kind, sizeof(g_expansion_api_result.kind) - 1);

  if (!g_expansion_api_sem) {
    g_expansion_api_sem = xSemaphoreCreateBinary();
  }
  xSemaphoreTake(g_expansion_api_sem, 0);  // dræn evt. gammelt signal fra et timeout'et forsøg
}

static bool expansion_api_begin(uint8_t board_index, const char *method, const char *path,
                                 const char *body, const char *kind) {
  if (board_index >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[board_index].configured) {
    debug_println("FEJL: Ukendt/ikke-konfigureret expansion board-index");
    return false;
  }
  if (g_expansion_api_result.in_progress) {
    debug_println("FEJL: Et andet expansion-board-kald er allerede i gang — vent til det er faerdigt");
    return false;
  }

  memset(&g_pending, 0, sizeof(g_pending));
  g_pending.board_index = board_index;
  network_config_ip_to_str(g_persist_config.expansion_boards[board_index].ip, g_pending.ip);
  strncpy(g_pending.token, g_persist_config.expansion_boards[board_index].token, sizeof(g_pending.token) - 1);
  strncpy(g_pending.method, method, sizeof(g_pending.method) - 1);
  strncpy(g_pending.path, path, sizeof(g_pending.path) - 1);
  if (body) strncpy(g_pending.body, body, sizeof(g_pending.body) - 1);
  strncpy(g_pending.kind, kind, sizeof(g_pending.kind) - 1);

  expansion_api_claim_slot(board_index, kind);
  return true;
}

// FEAT-442: raa socket-hjaelpere (defineret laengere nede, delt med OTA-relayet)
static int relay_connect(const char *ip);
static bool relay_send_all(int s, const char *buf, size_t len);
static int relay_read_response(int s, char *buf, size_t buf_size, char **body, int timeout_s);
static void expansion_restore_sequence(uint8_t i, char *summary, size_t cap);  // FEAT-462

// Selve netværksarbejdet — returnerer NORMALT (se filens toptekst for hvorfor
// det er en selvstændig funktion og ikke inline i task-entry'en nedenfor).
// FEAT-442: plain HTTP/1.1 over en raa lwIP-socket i stedet for Arduinos
// HTTPClient — den traak WiFiClientSecure, certifikat-bundtet, mbedTLS'
// TLS-klient og mbedtls_strerror-tabellen ind i firmwaren (ca. 40-50 KB
// flash), selv om boardet altid tales til over http://<ip>:8080.
static void expansion_api_do_work(void) {
  ExpansionApiResult *res = &g_expansion_api_result;
  int code = -1;

  if (strcmp(g_pending.kind, "restore") == 0) {  // FEAT-462: flere kald i ét
    expansion_restore_sequence(g_pending.board_index, res->response_json, sizeof(res->response_json));
    res->http_status = 200;
    res->transport_ok = true;
    return;
  }

  int s = relay_connect(g_pending.ip);
  if (s >= 0) {
    struct timeval io = { 3, 0 };  // som HTTPClient's tidligere 3 s timeout
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
    size_t blen = strlen(g_pending.body);
    char hdr[384];
    int hl = snprintf(hdr, sizeof(hdr),
                      "%s %s HTTP/1.1\r\n"
                      "Host: %s:8080\r\n"
                      "Authorization: Bearer %s\r\n"
                      "%s"
                      "Content-Length: %u\r\n"
                      "Connection: close\r\n\r\n",
                      g_pending.method, g_pending.path, g_pending.ip, g_pending.token,
                      blen ? "Content-Type: application/json\r\n" : "", (unsigned)blen);
    if (hl > 0 && hl < (int)sizeof(hdr) && relay_send_all(s, hdr, (size_t)hl) &&
        (blen == 0 || relay_send_all(s, g_pending.body, blen))) {
      // kun exp_api-tasken (een ad gangen). BUG-458: i PSRAM, ikke intern .bss
      static char *rbuf = NULL;
      const size_t RBUF_SIZE = 2048;
      if (!rbuf) rbuf = (char *)heap_caps_malloc(RBUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!rbuf) rbuf = (char *)malloc(RBUF_SIZE);
      char *body = NULL;
      code = rbuf ? relay_read_response(s, rbuf, RBUF_SIZE, &body, 3) : -1;
      if (code > 0 && body) {
        strncpy(res->response_json, body, sizeof(res->response_json) - 1);
        res->response_json[sizeof(res->response_json) - 1] = '\0';
      }
    }
    close(s);
  }

  res->http_status = code;
  if (code > 0) {
    res->transport_ok = true;
  } else {
    res->transport_ok = false;
    snprintf(res->response_json, sizeof(res->response_json),
             "{\"ok\":false,\"error\":\"transport_error\",\"message\":\"Boardet svarede ikke (netvaerksfejl %d) — tjek IP/token/at boardet er tændt\"}",
             code);
  }
  ESP_LOGI(TAG, "Board %u %s %s -> http=%d", g_pending.board_index, g_pending.method, g_pending.path, code);
}

// BUG-455: ÉN vedvarende worker-task, oprettet ved opstart (expansion_api_client_init),
// som vækkes pr. kald. Tidligere blev en task med 12 KB stak oprettet og
// slettet for HVERT kald — efter et stykke tids drift var den interne heap så
// fragmenteret (største blok ~5 KB), at xTaskCreate fejlede, og alle board-
// kald gav "Kunne ikke starte kald". Den rå socket-klient (FEAT-442) kræver
// langt mindre stak end HTTPClient gjorde.
#define EXP_API_WORKER_STACK 4096   // BUG-458: målt brug ~1,8 KB (status/channels-kald)
static SemaphoreHandle_t g_expansion_work_sem = NULL;
static TaskHandle_t g_expansion_worker = NULL;

/* FEAT-449: let sundhedstjek af boards uden data-trafik. Et board som ST ikke
 * bruger (ingen Modbus TCP-svar) stod tidligere som "Ingen data endnu" for
 * evigt, selv om det var tændt og i orden. Når workeren er ledig, hentes
 * GET /api/status fra ét sådant board ad gangen (round-robin). Boards med
 * friske data-plan-svar springes over — dér beviser trafikken allerede, at
 * boardet lever. Status + token valideres (kun HTTP 200 tæller som OK). */
#define EXP_HEALTH_PERIOD_MS     15000UL
#define EXP_HEALTH_BUF           2048    // FEAT-458: /api/channels med 4 kanaler ~1,5 KB
#define EXP_HEALTH_DATA_FRESH_MS 60000UL   // samme grænse som dashboardets EXP_DATA_FRESH_MS
static volatile uint32_t g_exp_health_ok_ms[EXPANSION_BOARD_MAX];
static volatile int16_t  g_exp_health_http[EXPANSION_BOARD_MAX];  // 0 = ikke tjekket, -1 = netværksfejl
static uint8_t g_exp_health_next = 0;

uint32_t expansion_api_board_health_ok_ms(uint8_t index) {
  return index < EXPANSION_BOARD_MAX ? g_exp_health_ok_ms[index] : 0;
}

int expansion_api_board_health_http(uint8_t index) {
  return index < EXPANSION_BOARD_MAX ? g_exp_health_http[index] : 0;
}

/* FEAT-458: hent boardets kanalliste (GET /api/channels) hvert
 * EXP_CHANFETCH_PERIOD_MS og husk hver kanals timeout_ms, så PLC'ens
 * transaktions-timeout følger boardets. Parses med simpel tekstsøgning
 * (kendt format: [{"channel":1,...,"timeout_ms":500,...},...]) — ingen
 * JSON-allokering i den interne heap. */
#define EXP_CHANFETCH_PERIOD_MS 300000UL
static uint32_t g_exp_chanfetch_ms[EXPANSION_BOARD_MAX];
static uint8_t g_exp_active_ch[EXPANSION_BOARD_MAX];     // FEAT-466: 0 = ukendt
static char g_exp_hw_type[EXPANSION_BOARD_MAX][12];      // FEAT-466: fx "4xRS485"

uint8_t expansion_board_active_channels(uint8_t i) { return i < EXPANSION_BOARD_MAX ? g_exp_active_ch[i] : 0; }
const char *expansion_board_hw_type(uint8_t i) { return i < EXPANSION_BOARD_MAX ? g_exp_hw_type[i] : ""; }

static void expansion_parse_channel_timeouts(uint8_t board_index, const char *body) {
  const char *p = body;
  while ((p = strstr(p, "\"channel\":")) != NULL) {
    p += 10;
    int ch = atoi(p);
    const char *next = strstr(p, "\"channel\":");
    const char *t = strstr(p, "\"timeout_ms\":");
    if (t && (!next || t < next) && ch >= 1) {
      long ms = atol(t + 13);
      if (ms > 0 && ms < 60000) modbus_expansion_set_board_timeout(board_index + 1, (uint8_t)ch, (uint16_t)ms);
    }
  }
}

/* Ét HTTP-kald mod boardet fra exp_api-tasken (method/path/valgfri JSON-body).
 * Svaret ligger i en PSRAM-buffer, der genbruges ved næste kald. */
static bool exp_http(uint8_t i, const char *method, const char *path, const char *body,
                     char **body_out, int *code_out) {
  char ip[16];
  char token[EXPANSION_TOKEN_MAX];
  network_config_ip_to_str(g_persist_config.expansion_boards[i].ip, ip);
  strncpy(token, g_persist_config.expansion_boards[i].token, sizeof(token) - 1);
  token[sizeof(token) - 1] = 0;
  static char *pbuf = NULL;  // kun exp_api-tasken; i PSRAM (BUG-458)
  if (!pbuf) pbuf = (char *)heap_caps_malloc(EXP_HEALTH_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int code = -1;
  char *resp = NULL;
  size_t blen = body ? strlen(body) : 0;
  int s = relay_connect(ip);
  if (s >= 0) {
    struct timeval io = { 3, 0 };
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
    char hdr[320];
    int hl = snprintf(hdr, sizeof(hdr),
                      "%s %s HTTP/1.1\r\nHost: %s:8080\r\nAuthorization: Bearer %s\r\n"
                      "%sContent-Length: %u\r\nConnection: close\r\n\r\n",
                      method, path, ip, token, blen ? "Content-Type: application/json\r\n" : "", (unsigned)blen);
    if (pbuf && hl > 0 && hl < (int)sizeof(hdr) && relay_send_all(s, hdr, (size_t)hl) &&
        (blen == 0 || relay_send_all(s, body, blen))) {
      code = relay_read_response(s, pbuf, EXP_HEALTH_BUF, &resp, 3);
    }
    close(s);
  }
  memset(token, 0, sizeof(token));
  *code_out = code;
  *body_out = resp;
  return code == 200 && resp != NULL;
}

static bool expansion_http_get(uint8_t i, const char *path, char **body_out, int *code_out) {
  return exp_http(i, "GET", path, NULL, body_out, code_out);
}

/* FEAT-462: seneste kendte opsætning pr. board (GET /api/config, board-fw
 * >= 0.34.0) — til PLC-backup og "Genskab board". PSRAM, 1,5 KB pr. board.
 * Opdateres kun fra et board, der ER sat op (har plc_ip), så et nyt, tomt
 * erstatnings-board ikke overskriver den gode kopi. */
#define EXP_SNAP_MAX 1536
static char *g_exp_cfg_snap[EXPANSION_BOARD_MAX];

const char *expansion_board_config_snapshot(uint8_t i) {
  return (i < EXPANSION_BOARD_MAX && g_exp_cfg_snap[i] && g_exp_cfg_snap[i][0]) ? g_exp_cfg_snap[i] : NULL;
}

void expansion_board_config_snapshot_set(uint8_t i, const char *json) {
  if (i >= EXPANSION_BOARD_MAX || !json) return;
  if (!g_exp_cfg_snap[i]) g_exp_cfg_snap[i] = (char *)heap_caps_calloc(1, EXP_SNAP_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!g_exp_cfg_snap[i]) return;
  strncpy(g_exp_cfg_snap[i], json, EXP_SNAP_MAX - 1);
  g_exp_cfg_snap[i][EXP_SNAP_MAX - 1] = 0;
}

static bool exp_snapshot_is_configured(const char *json) {
  const char *p = strstr(json, "\"plc_ip\":\"");
  return p && p[10] != '"';
}

/* FEAT-465: PLC'ens egen IP — Ethernet hvis forbundet, ellers WiFi. */
bool expansion_plc_own_ip(char *out16) {
  uint32_t ip = ethernet_driver_get_local_ip();
  if (!ip) ip = network_manager_get_local_ip();
  if (!ip) { out16[0] = 0; return false; }
  network_config_ip_to_str(ip, out16);
  return true;
}

/* FEAT-462: "Genskab board" — skriv kopien tilbage: kanalopsætning (hver
 * kanal-objekt fra /api/config er allerede i PUT /api/channels/{n}/config-
 * format), PLC-IP (PLC'ens AKTUELLE IP), syslog-modtagere og hostname.
 * Kører i exp_api-tasken som ét samlet kald. */
static void expansion_restore_sequence(uint8_t i, char *summary, size_t cap) {
  const char *snap = expansion_board_config_snapshot(i);
  if (!snap) { snprintf(summary, cap, "{\"ok\":false,\"error\":\"no_snapshot\",\"message\":\"PLC'en har ingen kopi af boardets opsaetning\"}"); return; }
  int ok = 0, fail = 0, code = -1;
  char *resp = NULL;
  char path[40];
  static char *body = NULL;  // PSRAM, kun exp_api-tasken
  if (!body) body = (char *)heap_caps_malloc(640, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!body) { snprintf(summary, cap, "{\"ok\":false,\"error\":\"no_memory\"}"); return; }
  // 1) kanaler
  const char *ch = strstr(snap, "\"channels\":[");
  while (ch && (ch = strstr(ch, "{\"channel\":")) != NULL) {
    const char *end = strchr(ch, '}');
    if (!end) break;
    size_t len = (size_t)(end - ch + 1);
    int n = atoi(ch + 11);
    if (len < 600 && n >= 1 && n <= 8) {
      memcpy(body, ch, len); body[len] = 0;
      snprintf(path, sizeof(path), "/api/channels/%d/config", n);
      if (exp_http(i, "PUT", path, body, &resp, &code)) ok++;
      else if (code != 404) fail++;   // 404 = kanalen findes ikke på dette board (fx C/D)
    }
    ch = end + 1;
  }
  // 2) PLC-IP = PLC'ens aktuelle IP
  char own[16];
  if (expansion_plc_own_ip(own)) {
    snprintf(body, 640, "{\"plc_ip\":\"%s\"}", own);
    if (exp_http(i, "POST", "/api/plc-ip", body, &resp, &code)) ok++; else fail++;
  }
  // 3) syslog
  const char *sl = strstr(snap, "\"syslog\":[");
  const char *se = sl ? strchr(sl, ']') : NULL;
  if (sl && se && (size_t)(se - sl) < 600) {
    snprintf(body, 640, "{\"targets\":%.*s}", (int)(se - (sl + 9) + 1), sl + 9);
    if (exp_http(i, "POST", "/api/syslog", body, &resp, &code)) ok++; else fail++;
  }
  // 4) hostname fra PLC-navnet
  char host[33];
  expansion_hostname_from_name(i, host, sizeof(host));
  snprintf(body, 640, "{\"hostname\":\"%s\"}", host);
  if (exp_http(i, "POST", "/api/hostname", body, &resp, &code)) ok++; else fail++;
  snprintf(summary, cap, "{\"ok\":%s,\"applied\":%d,\"failed\":%d,\"plc_ip\":\"%s\",\"hostname\":\"%s\",\"reboot_required\":true}",
           fail == 0 ? "true" : "false", ok, fail, own, host);
}

// FEAT-466/BUG-477: boardets faktiske kanaltal og hardware-type (fx "4xRS485")
// fra et /api/status-svar
static void expansion_parse_status_hw(uint8_t i, const char *sb) {
  if (!sb) return;
  const char *p = strstr(sb, "\"active_channels\":");
  if (p) g_exp_active_ch[i] = (uint8_t)atoi(p + 18);
  p = strstr(sb, "\"board_type\":\"");
  if (p) {
    p += 14;
    size_t n = 0;
    while (p[n] && p[n] != '"' && n < sizeof(g_exp_hw_type[i]) - 1) { g_exp_hw_type[i][n] = p[n]; n++; }
    g_exp_hw_type[i][n] = 0;
  }
}

static void expansion_health_probe(uint8_t i) {
  char *body = NULL;
  int code = -1;
  expansion_http_get(i, "/api/status", &body, &code);
  g_exp_health_http[i] = (int16_t)code;
  if (code == 200) {
    uint32_t now = millis();
    g_exp_health_ok_ms[i] = now ? now : 1;
    // BUG-477: et board der kom online efter PLC'ens opstart fik foerst sit
    // kanaltal ved naeste 5-min-hentning — saa viste siderne 2 kanaler
    expansion_parse_status_hw(i, body);
  }
}

static void expansion_health_tick(void) {
  // FEAT-458: først — hvis det er tid — boardets kanalliste (timeouts); gælder
  // ALLE boards, også dem med frisk data-trafik.
  uint32_t now = millis();
  for (uint8_t i = 0; i < EXPANSION_BOARD_MAX; i++) {
    const ExpansionBoard *b = &g_persist_config.expansion_boards[i];
    if (!b->configured || b->token[0] == '\0') continue;
    if (g_exp_chanfetch_ms[i] != 0 && (now - g_exp_chanfetch_ms[i]) < EXP_CHANFETCH_PERIOD_MS) continue;
    g_exp_chanfetch_ms[i] = now ? now : 1;
    char *body = NULL;
    int code = -1;
    bool got = expansion_http_get(i, "/api/config", &body, &code);   // FEAT-462 (board-fw >= 0.34.0)
    if (got && exp_snapshot_is_configured(body)) expansion_board_config_snapshot_set(i, body);
    if (!got && code == 404) got = expansion_http_get(i, "/api/channels", &body, &code);  // ældre board-fw
    if (got) {
      expansion_parse_channel_timeouts(i, body);
      // FEAT-466: boardets faktiske kanaltal og hardware-type (fx "4xRS485")
      char *sb = NULL;
      int sc = -1;
      if (expansion_http_get(i, "/api/status", &sb, &sc)) expansion_parse_status_hw(i, sb);
      g_exp_health_http[i] = 200;
      g_exp_health_ok_ms[i] = now ? now : 1;
    } else {
      g_exp_health_http[i] = (int16_t)code;
      g_exp_chanfetch_ms[i] = (now - (EXP_CHANFETCH_PERIOD_MS - 60000UL)) | 1;  // prøv igen om ~1 min
    }
    return;  // højst ét kald pr. tick
  }
  for (uint8_t n = 0; n < EXPANSION_BOARD_MAX; n++) {
    uint8_t i = (uint8_t)((g_exp_health_next + n) % EXPANSION_BOARD_MAX);
    const ExpansionBoard *b = &g_persist_config.expansion_boards[i];
    if (!b->configured || b->token[0] == '\0') continue;
    uint32_t rx = modbus_expansion_board_last_rx_ms(i + 1);
    if (rx && (millis() - rx) < EXP_HEALTH_DATA_FRESH_MS && g_exp_active_ch[i] != 0) continue;  // BUG-477: ukendt kanaltal -> probe alligevel
    g_exp_health_next = (uint8_t)(i + 1);
    expansion_health_probe(i);
    return;
  }
}

/* FEAT-456: samlet online-tilstand pr. board — samme regel som statussiden:
 * frisk Modbus TCP-svar (< 60 s) ELLER frisk sundhedstjek (< 150 s) = online;
 * har vi set boardet/tjekket det, men intet er friskt = offline; ellers ukendt. */
int expansion_board_online_state(uint8_t i) {
  if (i >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[i].configured) return -1;
  uint32_t now = millis();
  uint32_t rx = modbus_expansion_board_last_rx_ms(i + 1);
  uint32_t hok = g_exp_health_ok_ms[i];
  if (rx && (now - rx) < EXP_HEALTH_DATA_FRESH_MS) return 1;
  if (hok && (now - hok) < 150000UL) return 1;
  if (rx || g_exp_health_http[i] != 0) return 0;
  return -1;
}

static void expansion_api_worker(void *pv) {
  (void)pv;
  for (;;) {
    if (xSemaphoreTake(g_expansion_work_sem, pdMS_TO_TICKS(EXP_HEALTH_PERIOD_MS)) != pdTRUE) {
      // FEAT-449: ingen kald i kø — brug pausen til et sundhedstjek
      if (!g_expansion_api_result.in_progress) expansion_health_tick();
      continue;
    }
    expansion_api_do_work();
    g_expansion_api_result.done = true;
    g_expansion_api_result.in_progress = false;
    xSemaphoreGive(g_expansion_api_sem);
  }
}

void expansion_api_client_init(void) {
  if (!g_expansion_api_sem) g_expansion_api_sem = xSemaphoreCreateBinary();
  if (!g_expansion_work_sem) g_expansion_work_sem = xSemaphoreCreateBinary();
  if (!g_expansion_worker && g_expansion_work_sem) {
    xTaskCreatePinnedToCore(expansion_api_worker, "exp_api", EXP_API_WORKER_STACK, NULL, 1,
                            &g_expansion_worker, tskNO_AFFINITY);
  }
}

static bool expansion_api_spawn(void) {
  if (!g_expansion_worker) expansion_api_client_init();  // sidste chance, hvis init ikke er kaldt
  BaseType_t created = g_expansion_worker ? pdPASS : pdFAIL;
  if (created == pdPASS) xSemaphoreGive(g_expansion_work_sem);
  if (created != pdPASS) {
    g_expansion_api_result.in_progress = false;
    g_expansion_api_result.done = true;
    snprintf(g_expansion_api_result.response_json, sizeof(g_expansion_api_result.response_json),
             "{\"ok\":false,\"error\":\"internal_error\",\"message\":\"Kunne ikke starte baggrundstask\"}");
    return false;
  }
  return true;
}

bool expansion_api_start_status(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "GET", "/api/status", NULL, "status")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_channels(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "GET", "/api/channels", NULL, "channels")) return false;
  return expansion_api_spawn();
}

// v7.9.68.3: static, declared function-code support — see
// DESIGN_GUIDE_MODBUS_EXPANSION_FC_CAPABILITIES.md. No bus traffic, no
// side effects, same "simple GET, async, polled" shape as expansion_api_start_status().
bool expansion_api_start_capabilities(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "GET", "/api/capabilities", NULL, "capabilities")) return false;
  return expansion_api_spawn();
}

// FEAT-420: board-firmware-OTA — status/bekraeft/reboot. Alle tre er simple
// kald uden request-body; se expansion_api_client.h for reboot-semantikken
// mens boardet afventer bekraeftelse.
bool expansion_api_start_ota_status(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "GET", "/api/ota/status", NULL, "ota_status")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_ota_confirm(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "POST", "/api/ota/confirm", NULL, "ota_confirm")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_reboot(uint8_t board_index) {
  if (!expansion_api_begin(board_index, "POST", "/api/reboot", NULL, "reboot")) return false;
  return expansion_api_spawn();
}

/* FEAT-455: boardets visningsnavn -> gyldigt DHCP-hostname (RFC 1123-label,
 * samme regler som board-firmwarens `hostname`-kommando): alt andet end
 * bogstaver/tal bliver til '-', flere '-' i træk samles, '-' fjernes forrest
 * og bagerst, højst 32 tegn. Tomt resultat (fx et navn kun af specialtegn)
 * giver "hvplc-exp-<nr>". Fx "Skab 007" -> "Skab-007", "Kælder/øst" -> "K-lder-st". */
void expansion_hostname_from_name(uint8_t board_index, char *out, size_t out_size) {
  if (!out || out_size < 2) return;
  const char *name = (board_index < EXPANSION_BOARD_MAX) ? g_persist_config.expansion_boards[board_index].name : "";
  size_t n = 0;
  const size_t max_len = (out_size - 1 < 32) ? out_size - 1 : 32;
  for (size_t i = 0; name[i] && n < max_len; i++) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (ok) {
      out[n++] = c;
    } else if (n > 0 && out[n - 1] != '-') {
      out[n++] = '-';
    }
  }
  while (n > 0 && out[n - 1] == '-') n--;
  out[n] = 0;
  if (n == 0) snprintf(out, out_size, "hvplc-exp-%u", (unsigned)(board_index + 1));
}

bool expansion_api_start_set_hostname(uint8_t board_index) {
  char host[33];
  expansion_hostname_from_name(board_index, host, sizeof(host));
  char body[64];
  snprintf(body, sizeof(body), "{\"hostname\":\"%s\"}", host);
  if (!expansion_api_begin(board_index, "POST", "/api/hostname", body, "hostname")) return false;
  return expansion_api_spawn();
}

/* FEAT-462/463/465 */
bool expansion_api_start_restore(uint8_t board_index) {
  if (!expansion_board_config_snapshot(board_index)) return false;
  if (!expansion_api_begin(board_index, "POST", "/api/(restore)", NULL, "restore")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_set_plc_ip(uint8_t board_index) {
  char own[16];
  if (!expansion_plc_own_ip(own)) return false;
  char body[48];
  snprintf(body, sizeof(body), "{\"plc_ip\":\"%s\"}", own);
  if (!expansion_api_begin(board_index, "POST", "/api/plc-ip", body, "plc_ip")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_syslog(uint8_t board_index, const char *body) {
  if (!body || strlen(body) >= 500) return false;
  if (!expansion_api_begin(board_index, "POST", "/api/syslog", body, "syslog")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_config_push(uint8_t board_index, uint8_t channel,
                                      bool enabled, const char *mode, uint32_t baudrate,
                                      const char *parity, uint8_t stop_bits,
                                      uint16_t timeout_ms, uint16_t inter_frame_delay_ms) {
  char path[64];
  snprintf(path, sizeof(path), "/api/channels/%u/config", channel);

  JsonDocument doc;
  doc["enabled"] = enabled;
  doc["mode"] = mode;
  doc["baudrate"] = baudrate;
  doc["parity"] = parity;
  doc["stop_bits"] = stop_bits;
  doc["timeout_ms"] = timeout_ms;
  doc["inter_frame_delay_ms"] = inter_frame_delay_ms;
  char body[256];
  serializeJson(doc, body, sizeof(body));

  if (!expansion_api_begin(board_index, "PUT", path, body, "config")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_diag_read(uint8_t board_index, uint8_t channel,
                                    uint8_t function_code, uint8_t slave_id,
                                    uint16_t address, uint16_t quantity) {
  char path[64];
  snprintf(path, sizeof(path), "/api/channels/%u/read", channel);

  JsonDocument doc;
  doc["function_code"] = function_code;
  doc["slave_id"] = slave_id;
  doc["address"] = address;
  doc["quantity"] = quantity;
  char body[128];
  serializeJson(doc, body, sizeof(body));

  if (!expansion_api_begin(board_index, "POST", path, body, "read")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_diag_write_single(uint8_t board_index, uint8_t channel,
                                            uint8_t function_code, uint8_t slave_id,
                                            uint16_t address, uint32_t value) {
  char path[64];
  snprintf(path, sizeof(path), "/api/channels/%u/write", channel);

  JsonDocument doc;
  doc["function_code"] = function_code;
  doc["slave_id"] = slave_id;
  doc["address"] = address;
  if (function_code == 5) {
    doc["value"] = (value != 0);
  } else {
    doc["value"] = value;
  }
  char body[128];
  serializeJson(doc, body, sizeof(body));

  if (!expansion_api_begin(board_index, "POST", path, body, "write")) return false;
  return expansion_api_spawn();
}

bool expansion_api_start_diag_write_multi(uint8_t board_index, uint8_t channel,
                                           uint8_t slave_id, uint16_t address,
                                           const uint16_t *values, uint8_t count) {
  if (count == 0 || count > 32) return false;
  char path[64];
  snprintf(path, sizeof(path), "/api/channels/%u/write", channel);

  JsonDocument doc;
  doc["function_code"] = 16;
  doc["slave_id"] = slave_id;
  doc["address"] = address;
  JsonArray arr = doc["values"].to<JsonArray>();
  for (uint8_t i = 0; i < count; i++) arr.add(values[i]);
  char body[400];
  serializeJson(doc, body, sizeof(body));

  if (!expansion_api_begin(board_index, "POST", path, body, "write")) return false;
  return expansion_api_spawn();
}

// v7.9.68.1: FC15 diagnostic write — mirrors expansion_api_start_diag_write_multi()
// above, "values" array holds bool instead of uint16_t. Board-side contract not
// yet in PLC_INTEGRATION_MANUAL.md (only FC16 is documented there today) —
// symmetric with FC16's own {"function_code","slave_id","address","values"} shape.
bool expansion_api_start_diag_write_multi_coils(uint8_t board_index, uint8_t channel,
                                                 uint8_t slave_id, uint16_t address,
                                                 const bool *values, uint8_t count) {
  if (count == 0 || count > 32) return false;
  char path[64];
  snprintf(path, sizeof(path), "/api/channels/%u/write", channel);

  JsonDocument doc;
  doc["function_code"] = 15;
  doc["slave_id"] = slave_id;
  doc["address"] = address;
  JsonArray arr = doc["values"].to<JsonArray>();
  for (uint8_t i = 0; i < count; i++) arr.add(values[i]);
  char body[400];
  serializeJson(doc, body, sizeof(body));

  if (!expansion_api_begin(board_index, "POST", path, body, "write")) return false;
  return expansion_api_spawn();
}

/* ============================================================================
 * FEAT-420: BOARD-FIRMWARE-OTA — synkront relay browser → PLC → board
 * ============================================================================
 * Boardets kontrakt (POST /api/ota): raa .bin som body, Content-Length
 * paakraevet (boardet understoetter ikke chunked upload), valgfri
 * X-Firmware-MD5. Boardet svarer foerst naar sidste byte er skrevet og
 * verificeret — eller TIDLIGT med en afvisning (401/409/413/...) foer det har
 * laest hele bodyen. Afbrydes forbindelsen midt i uploadet, kasserer boardet
 * det og bliver paa sin nuvaerende firmware.
 */

#define EXP_OTA_BOARD_PORT            8080
#define EXP_OTA_RELAY_CHUNK           2048
#define EXP_OTA_CONNECT_TIMEOUT_MS    3000
#define EXP_OTA_IO_TIMEOUT_S          60   // pr. send/recv, baade mod boardet og browseren
#define EXP_OTA_RESPONSE_TIMEOUT_S    30   // boardets slutsvar (verificerer efter sidste byte)
#define EXP_OTA_BROWSER_MAX_TIMEOUTS  2    // 2 x 60 s uden data fra browseren → afbryd

static void relay_error_json(char *out, size_t out_size, const char *code, const char *msg) {
  snprintf(out, out_size, "{\"ok\":false,\"error\":\"%s\",\"message\":\"%s\"}", code, msg);
}

static int relay_connect(const char *ip) {
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(EXP_OTA_BOARD_PORT);
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) return -1;

  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return -1;

  // Ikke-blokerende connect, saa et slukket board giver fejl efter 3 s i
  // stedet for lwIP's langt laengere standard-connect-timeout.
  int flags = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, flags | O_NONBLOCK);
  int r = connect(s, (struct sockaddr *)&addr, sizeof(addr));
  if (r < 0 && errno != EINPROGRESS) { close(s); return -1; }
  if (r < 0) {
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    struct timeval tv = { EXP_OTA_CONNECT_TIMEOUT_MS / 1000, (EXP_OTA_CONNECT_TIMEOUT_MS % 1000) * 1000 };
    if (select(s + 1, NULL, &wfds, NULL, &tv) <= 0) { close(s); return -1; }
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) { close(s); return -1; }
  }
  fcntl(s, F_SETFL, flags);

  struct timeval io = { EXP_OTA_IO_TIMEOUT_S, 0 };
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
  return s;
}

static bool relay_send_all(int s, const char *buf, size_t len) {
  while (len > 0) {
    int n = send(s, buf, len, 0);
    if (n <= 0) return false;
    buf += n;
    len -= (size_t)n;
  }
  return true;
}

// true hvis boardet allerede har sendt noget (et tidligt afvisningssvar)
// eller lukket forbindelsen — saa stopper vi med at videresende.
static bool relay_board_has_data(int s) {
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(s, &rfds);
  struct timeval tv = { 0, 0 };
  return select(s + 1, &rfds, NULL, NULL, &tv) > 0;
}

// Finder værdien af en header (case-insensitivt) i en NUL-termineret
// header-blok. Returnerer pointer til værdien (efter ':' og mellemrum) eller NULL.
static const char *relay_find_header(const char *hdrs, const char *hdr_end, const char *name) {
  size_t nlen = strlen(name);
  const char *line = strstr(hdrs, "\r\n");
  while (line && line < hdr_end) {
    line += 2;
    if (strncasecmp(line, name, nlen) == 0 && line[nlen] == ':') {
      const char *v = line + nlen + 1;
      while (*v == ' ') v++;
      return v;
    }
    line = strstr(line, "\r\n");
  }
  return NULL;
}

// Afkoder en chunked body in-place. Returnerer den afkodede laengde.
static size_t relay_dechunk(char *body, size_t len) {
  char *src = body, *dst = body, *end = body + len;
  while (src < end) {
    char *line_end = strstr(src, "\r\n");
    if (!line_end || line_end >= end) break;
    unsigned long sz = strtoul(src, NULL, 16);
    src = line_end + 2;
    if (sz == 0) break;
    if (sz > (size_t)(end - src)) sz = (size_t)(end - src);
    memmove(dst, src, sz);
    dst += sz;
    src += sz + 2;  // spring chunk'ens afsluttende CRLF over
  }
  *dst = '\0';
  return (size_t)(dst - body);
}

// Laeser boardets HTTP-svar ind i buf (NUL-termineret). Stopper naar
// Content-Length/chunked-slutningen er naaet, forbindelsen lukkes eller
// timeout. Returnerer HTTP-status og saetter *body, eller -1.
static int relay_read_response(int s, char *buf, size_t buf_size, char **body, int timeout_s) {
  struct timeval tv = { timeout_s, 0 };
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  size_t used = 0;
  char *hdr_end = NULL;
  long content_len = -1;
  bool chunked = false;
  while (used < buf_size - 1) {
    int n = recv(s, buf + used, buf_size - 1 - used, 0);
    if (n <= 0) break;
    used += (size_t)n;
    buf[used] = '\0';
    if (!hdr_end && (hdr_end = strstr(buf, "\r\n\r\n")) != NULL) {
      const char *cl = relay_find_header(buf, hdr_end, "Content-Length");
      if (cl) content_len = strtol(cl, NULL, 10);
      const char *te = relay_find_header(buf, hdr_end, "Transfer-Encoding");
      chunked = te && strncasecmp(te, "chunked", 7) == 0;
    }
    if (hdr_end) {
      const char *b = hdr_end + 4;
      size_t have = used - (size_t)(b - buf);
      if (!chunked && content_len >= 0 && have >= (size_t)content_len) break;
      if (chunked && (strncmp(b, "0\r\n\r\n", 5) == 0 || strstr(b, "\r\n0\r\n\r\n"))) break;
    }
  }
  buf[used] = '\0';
  if (!hdr_end || strncmp(buf, "HTTP/1.", 7) != 0 || used < 12) return -1;
  int status = (int)strtol(buf + 9, NULL, 10);
  if (status < 100 || status > 599) return -1;

  *body = hdr_end + 4;
  size_t body_len = used - (size_t)(*body - buf);
  if (chunked) {
    relay_dechunk(*body, body_len);
  } else if (content_len >= 0 && body_len > (size_t)content_len) {
    (*body)[content_len] = '\0';
  }
  return status;
}

// Selve I/O'en. ip/token er kopier taget ved relay-start (samme race-
// beskyttelse som g_pending). Returnerer HTTP-status til browseren.
static int relay_do(httpd_req_t *req, const char *ip, const char *token, const char *md5_hex,
                    char *resp_json, size_t resp_size, bool *client_gone, bool *transport_ok) {
  const size_t total = req->content_len;

  int s = relay_connect(ip);
  if (s < 0) {
    relay_error_json(resp_json, resp_size, "board_unreachable",
                     "Kunne ikke forbinde til boardet paa port 8080 - tjek IP og at boardet er taendt");
    return 502;
  }

  char *buf = (char *)malloc(EXP_OTA_RELAY_CHUNK + 1);
  if (!buf) {
    close(s);
    relay_error_json(resp_json, resp_size, "internal_error", "PLC'en kunne ikke allokere upload-buffer");
    return 500;
  }

  int hl = snprintf(buf, EXP_OTA_RELAY_CHUNK,
                    "POST /api/ota HTTP/1.1\r\n"
                    "Host: %s:%d\r\n"
                    "Authorization: Bearer %s\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Content-Length: %u\r\n"
                    "%s%s%s"
                    "Connection: close\r\n\r\n",
                    ip, EXP_OTA_BOARD_PORT, token, (unsigned)total,
                    md5_hex ? "X-Firmware-MD5: " : "", md5_hex ? md5_hex : "", md5_hex ? "\r\n" : "");
  bool sent = relay_send_all(s, buf, (size_t)hl);
  memset(buf, 0, (size_t)hl);  // tokenet skal ikke blive liggende i heap'en
  if (!sent) {
    free(buf);
    close(s);
    relay_error_json(resp_json, resp_size, "board_write_failed",
                     "Forbindelsen til boardet blev afbrudt - boardet koerer uaendret videre");
    return 502;
  }
  *transport_ok = true;

  // Browseren kan vaere langsom om at levere bidderne mens boardet skriver
  // flash — samme 60 s som PLC'ens egen OTA-upload (ota_handler.cpp).
  struct timeval rt = { EXP_OTA_IO_TIMEOUT_S, 0 };
  setsockopt(httpd_req_to_sockfd(req), SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));

  size_t forwarded = 0;
  int browser_timeouts = 0;
  while (forwarded < total) {
    size_t want = total - forwarded;
    if (want > EXP_OTA_RELAY_CHUNK) want = EXP_OTA_RELAY_CHUNK;
    int n = httpd_req_recv(req, buf, want);
    if (n == HTTPD_SOCK_ERR_TIMEOUT) {
      if (++browser_timeouts > EXP_OTA_BROWSER_MAX_TIMEOUTS) {
        free(buf);
        close(s);
        relay_error_json(resp_json, resp_size, "upload_timeout",
                         "Ingen data fra browseren i 2 minutter - upload afbrudt, boardet koerer uaendret videre");
        return 408;
      }
      continue;
    }
    if (n <= 0) {
      // Browseren forsvandt. Lukning af forbindelsen faar boardet til at
      // kassere det halve upload og blive paa sin nuvaerende firmware.
      free(buf);
      close(s);
      *client_gone = true;
      relay_error_json(resp_json, resp_size, "upload_aborted",
                       "Upload afbrudt - boardet koerer uaendret videre");
      return 400;
    }
    browser_timeouts = 0;
    if (relay_board_has_data(s)) break;  // tidligt svar fra boardet (afvisning)
    if (!relay_send_all(s, buf, (size_t)n)) {
      if (relay_board_has_data(s)) break;
      free(buf);
      close(s);
      relay_error_json(resp_json, resp_size, "board_write_failed",
                       "Forbindelsen til boardet blev afbrudt under upload - boardet koerer uaendret videre");
      return 502;
    }
    forwarded += (size_t)n;
  }

  char *body = NULL;
  int status = relay_read_response(s, buf, EXP_OTA_RELAY_CHUNK + 1, &body, EXP_OTA_RESPONSE_TIMEOUT_S);
  close(s);
  if (status < 0) {
    free(buf);
    relay_error_json(resp_json, resp_size, "board_no_response",
                     "Boardet svarede ikke efter uploadet - laes boardets OTA-status for at se udfaldet");
    return 504;
  }
  if (body && body[0]) {
    strncpy(resp_json, body, resp_size - 1);
    resp_json[resp_size - 1] = '\0';
  } else {
    snprintf(resp_json, resp_size, "{\"ok\":%s,\"message\":\"Boardet svarede HTTP %d uden indhold\"}",
             status < 300 ? "true" : "false", status);
  }
  free(buf);

  // Et 401/403 fra BOARDET (forkert token) maa ikke naa browseren som 401 —
  // PLC'ens web-UI tolker 401 som "log ind igen" og taeller det som et
  // fejlet PLC-login. Boardets JSON-body (med dets message) sendes uaendret.
  if (status == 401 || status == 403) return 502;
  return status;
}

int expansion_api_ota_relay(httpd_req_t *req, uint8_t board_index, const char *md5_hex,
                            char *resp_json, size_t resp_size, bool *client_gone) {
  *client_gone = false;
  if (board_index >= EXPANSION_BOARD_MAX || !g_persist_config.expansion_boards[board_index].configured) {
    relay_error_json(resp_json, resp_size, "board_not_found", "Board ikke fundet");
    return 404;
  }
  if (g_expansion_api_result.in_progress) {
    relay_error_json(resp_json, resp_size, "busy", "Et andet expansion-board-kald er allerede i gang");
    return 409;
  }

  char ip[16];
  char token[EXPANSION_TOKEN_MAX];
  network_config_ip_to_str(g_persist_config.expansion_boards[board_index].ip, ip);
  strncpy(token, g_persist_config.expansion_boards[board_index].token, sizeof(token) - 1);
  token[sizeof(token) - 1] = '\0';

  expansion_api_claim_slot(board_index, "ota");
  bool transport_ok = false;
  int status = relay_do(req, ip, token, md5_hex, resp_json, resp_size, client_gone, &transport_ok);
  memset(token, 0, sizeof(token));

  g_expansion_api_result.transport_ok = transport_ok;
  g_expansion_api_result.http_status = status;
  strncpy(g_expansion_api_result.response_json, resp_json, sizeof(g_expansion_api_result.response_json) - 1);
  g_expansion_api_result.done = true;
  g_expansion_api_result.in_progress = false;
  xSemaphoreGive(g_expansion_api_sem);

  ESP_LOGI(TAG, "Board %u OTA-relay: %u bytes -> http=%d", board_index, (unsigned)req->content_len, status);
  return status;
}

bool expansion_api_poll(ExpansionApiResult *out) {
  if (!out) return false;
  *out = g_expansion_api_result;
  return g_expansion_api_result.valid;
}

bool expansion_api_is_busy(void) {
  return g_expansion_api_result.in_progress;
}

bool expansion_api_wait_result(uint32_t timeout_ms, ExpansionApiResult *out) {
  if (!g_expansion_api_sem) {
    if (out) *out = g_expansion_api_result;
    return false;
  }
  bool got = xSemaphoreTake(g_expansion_api_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
  if (got) {
    // Giv semaphoren tilbage med det samme — expansion_api_poll()/REST-pollet
    // skal stadig kunne se resultatet bagefter, denne funktion er kun en
    // synkron "vent til færdig"-bekvemmelighed for CLI'en, ikke en exclusive lock.
    xSemaphoreGive(g_expansion_api_sem);
  }
  if (out) *out = g_expansion_api_result;
  return got;
}
