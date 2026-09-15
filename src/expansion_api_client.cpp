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
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <string.h>
#include "expansion_api_client.h"
#include "config_struct.h"
#include "network_config.h"
#include "debug.h"

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
  { EXPANSION_BOARD_TYPE_MODBUS_2CH, "Modbus Expansion (2x RS485/RS232-kanaler)" },
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

int expansion_board_add(uint8_t number, const char *board_type, const char *name, const char *ip_str, const char *token) {
  if (number < 1 || number > EXPANSION_BOARD_MAX) return -1;
  if (!name || !*name || !ip_str || !*ip_str || !token || !*token) return -1;
  if (strlen(name) >= EXPANSION_BOARD_NAME_MAX) return -1;
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
  if (!name || !*name || strlen(name) >= EXPANSION_BOARD_NAME_MAX) return false;
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

  memset(&g_expansion_api_result, 0, sizeof(g_expansion_api_result));
  g_expansion_api_result.valid = true;
  g_expansion_api_result.in_progress = true;
  g_expansion_api_result.board_index = board_index;
  strncpy(g_expansion_api_result.kind, kind, sizeof(g_expansion_api_result.kind) - 1);

  if (!g_expansion_api_sem) {
    g_expansion_api_sem = xSemaphoreCreateBinary();
  }
  xSemaphoreTake(g_expansion_api_sem, 0);  // dræn evt. gammelt signal fra et timeout'et forsøg

  return true;
}

// Selve netværksarbejdet — returnerer NORMALT (se filens toptekst for hvorfor
// det er en selvstændig funktion og ikke inline i task-entry'en nedenfor).
static void expansion_api_do_work(void) {
  ExpansionApiResult *res = &g_expansion_api_result;

  String url = "http://" + String(g_pending.ip) + ":8080" + String(g_pending.path);

  HTTPClient http;
  http.setConnectTimeout(2000);
  http.setTimeout(3000);

  if (!http.begin(url)) {
    snprintf(res->response_json, sizeof(res->response_json),
             "{\"ok\":false,\"error\":\"transport_error\",\"message\":\"Kunne ikke starte HTTP-forbindelse til boardet\"}");
    res->transport_ok = false;
    res->http_status = -1;
    return;
  }
  http.addHeader("Authorization", "Bearer " + String(g_pending.token));
  if (g_pending.body[0]) {
    http.addHeader("Content-Type", "application/json");
  }

  int code;
  if (strcmp(g_pending.method, "GET") == 0) {
    code = http.GET();
  } else if (strcmp(g_pending.method, "PUT") == 0) {
    code = http.PUT(String(g_pending.body));
  } else {
    code = http.POST(String(g_pending.body));
  }

  res->http_status = code;
  if (code > 0) {
    res->transport_ok = true;
    String body = http.getString();
    strncpy(res->response_json, body.c_str(), sizeof(res->response_json) - 1);
  } else {
    res->transport_ok = false;
    snprintf(res->response_json, sizeof(res->response_json),
             "{\"ok\":false,\"error\":\"transport_error\",\"message\":\"Boardet svarede ikke (netvaerksfejl %d) — tjek IP/token/at boardet er tændt\"}",
             code);
  }
  http.end();
  ESP_LOGI(TAG, "Board %u %s %s -> http=%d", g_pending.board_index, g_pending.method, g_pending.path, code);
}

static void expansion_api_worker(void *pv) {
  (void)pv;
  expansion_api_do_work();  // lokale C++-objekter (HTTPClient m.fl.) destrueres normalt her
  g_expansion_api_result.done = true;
  g_expansion_api_result.in_progress = false;
  xSemaphoreGive(g_expansion_api_sem);
  vTaskDelete(NULL);
}

static bool expansion_api_spawn(void) {
  // Plain HTTP (ingen TLS-haandtryk, modsat github_check_worker's 32768 —
  // se BUG-364/369) — 12288 giver rigelig margen til HTTPClient+String uden
  // TLS-overheaddet der noedvendiggjorde originalens stoerre stak.
  BaseType_t created = xTaskCreatePinnedToCore(expansion_api_worker, "exp_api", 12288, NULL, 1, NULL, tskNO_AFFINITY);
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
