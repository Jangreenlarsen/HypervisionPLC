// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file modbus_expansion.cpp
 * @brief FEAT-410: Modbus TCP-transport mod expansion-boardenes data-plan.
 *
 * Vedvarende TCP-forbindelse pr. (board, kanal) — genbruges på tværs af
 * transaktioner (matcher PLC_INTEGRATION_MANUAL.md §3.2's anbefaling: "Åbn
 * ÉN vedvarende TCP-forbindelse pr. kanal og genbrug den"). Et fast, lille
 * antal forbindelser (MODBUS_EXPANSION_MAX_CONNECTIONS, se .h) — IKKE 64,
 * se designnoten der for hvorfor.
 *
 * MBAP-header (7 byte): TransactionID(2) ProtocolID(2, altid 0) Length(2)
 * UnitID(1). PDU'en (function code + data) er byte-for-byte identisk med
 * modbus_master.cpp's RTU-PDU'er (se dén fil for den oprindelige,
 * produktionshærdede reference) — kun rammen udenom er anderledes, og CRC
 * er unødvendig (TCP garanterer selv integritet).
 */

#include <Arduino.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "modbus_expansion.h"
#include "modbus_expansion_async.h"
#include "expansion_api_client.h"
#include "config_struct.h"
#include "network_config.h"
#include "debug.h"
#include <esp_heap_caps.h>

static const char *TAG = "MBX_TRANSPORT";

// FEAT-458: transaktions-timeouten følger boardets kanal-timeout (+ margin),
// når den er kendt — se modbus_expansion_effective_timeout_ms().
#define MBX_CONNECT_TIMEOUT_MS      1500

typedef struct {
  bool      in_use;
  uint8_t   board;    // 1-8, 0 = ledigt slot
  uint8_t   channel;  // 1-8
  WiFiClient client;
  uint16_t  next_transaction_id;
  uint32_t  last_activity_ms;
} mbx_connection_t;

static mbx_connection_t g_mbx_conn[MODBUS_EXPANSION_MAX_CONNECTIONS];
static volatile uint32_t g_mbx_board_last_rx_ms[EXPANSION_BOARD_MAX];  // BUG-431

uint32_t modbus_expansion_board_last_rx_ms(uint8_t board) {
  if (board < 1 || board > EXPANSION_BOARD_MAX) return 0;
  return g_mbx_board_last_rx_ms[board - 1];
}

/* FEAT-457: PLC'ens egen statistik pr. (board, kanal) — det ST faktisk
 * oplever (inkl. forbindelsesfejl), ikke boardets RTU-tal. Ligger i PSRAM
 * (allokeres ved første brug) for ikke at koste intern RAM (BUG-458). */
static mbx_chan_stats_t *g_mbx_stats = NULL;   // [EXPANSION_BOARD_MAX * MBX_STAT_CHANNELS]
/* FEAT-458: boardets egen kanal-timeout (ms), hentet af exp_api-workeren fra
 * GET /api/channels. 0 = ukendt -> fast MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS. */
static volatile uint16_t g_mbx_board_timeout_ms[EXPANSION_BOARD_MAX][MBX_STAT_CHANNELS];

static mbx_chan_stats_t *mbx_stats_slot(uint8_t board, uint8_t channel) {
  if (board < 1 || board > EXPANSION_BOARD_MAX || channel < 1 || channel > MBX_STAT_CHANNELS) return NULL;
  if (!g_mbx_stats) {
    g_mbx_stats = (mbx_chan_stats_t *)heap_caps_calloc(EXPANSION_BOARD_MAX * MBX_STAT_CHANNELS, sizeof(mbx_chan_stats_t),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_mbx_stats) return NULL;
  }
  return &g_mbx_stats[(board - 1) * MBX_STAT_CHANNELS + (channel - 1)];
}

static void mbx_stats_record(uint8_t board, uint8_t channel, mb_error_code_t err) {
  mbx_chan_stats_t *s = mbx_stats_slot(board, channel);
  if (!s) return;
  s->requests++;
  if (err == MB_OK) { s->ok++; s->last_ok_ms = millis(); }
  else if (err == MB_TIMEOUT) s->timeouts++;
  else if (err == MB_EXCEPTION) s->exceptions++;
  else s->errors++;
}

bool modbus_expansion_get_chan_stats(uint8_t board, uint8_t channel, mbx_chan_stats_t *out) {
  if (!out || !g_mbx_stats) return false;
  mbx_chan_stats_t *s = mbx_stats_slot(board, channel);
  if (!s) return false;
  *out = *s;
  return s->requests > 0;
}

void modbus_expansion_set_board_timeout(uint8_t board, uint8_t channel, uint16_t timeout_ms) {
  if (board < 1 || board > EXPANSION_BOARD_MAX || channel < 1 || channel > MBX_STAT_CHANNELS) return;
  g_mbx_board_timeout_ms[board - 1][channel - 1] = timeout_ms;
}

uint16_t modbus_expansion_get_board_timeout(uint8_t board, uint8_t channel) {
  if (board < 1 || board > EXPANSION_BOARD_MAX || channel < 1 || channel > MBX_STAT_CHANNELS) return 0;
  return g_mbx_board_timeout_ms[board - 1][channel - 1];
}

/* FEAT-458: PLC'en skal vente længere end boardet selv venter på RTU-slaven
 * (+ TCP/gateway-margin) — ellers giver PLC'en op, mens boardet stadig venter,
 * og svaret ankommer for sent (dét gav BUG-469's sene bytes). */
uint32_t modbus_expansion_effective_timeout_ms(uint8_t board, uint8_t channel) {
  uint32_t t = MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS;
  uint16_t bt = modbus_expansion_get_board_timeout(board, channel);
  if (bt > 0 && (uint32_t)bt + MBX_TIMEOUT_MARGIN_MS > t) t = (uint32_t)bt + MBX_TIMEOUT_MARGIN_MS;
  if (t > MBX_TIMEOUT_MAX_MS) t = MBX_TIMEOUT_MAX_MS;
  return t;
}

// BUG-419: g_mbx_conn[] was written under the original assumption of a
// single calling task (true before BUG-417) — the slot-scan-then-claim
// sequence below is a classic TOCTOU race once genuinely concurrent workers
// (different (board,kanal) pairs, on different cores) can call this at the
// same time: both could see the same slot as free, or pick the same LRU
// eviction victim, and both then write into it — corrupting whichever
// connection loses the race, exactly the kind of state that produced
// BUG-419's permanent hang (a worker left holding a WiFiClient another
// worker had since reassigned/stopped). This mutex makes the ENTIRE
// find-or-claim-or-evict decision atomic. It is a real FreeRTOS mutex, not
// a portMUX spinlock, because eviction needs to call
// modbus_expansion_async_is_channel_inflight() (itself takes a mutex) —
// nesting a blocking-capable call inside a spinlock's interrupts-disabled
// section is not safe on this platform.
static SemaphoreHandle_t g_mbx_conn_mutex = NULL;

void modbus_expansion_init() {
  if (!g_mbx_conn_mutex) {
    g_mbx_conn_mutex = xSemaphoreCreateMutex();
  }
}

static mbx_connection_t *mbx_find_connection(uint8_t board, uint8_t channel) {
  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (g_mbx_conn[i].in_use && g_mbx_conn[i].board == board && g_mbx_conn[i].channel == channel) {
      return &g_mbx_conn[i];
    }
  }
  return NULL;
}

// Returns NULL only when every slot is both full AND actively in flight
// right now (caller must treat that as "try again shortly", never as a
// license to touch an unallocated connection).
static mbx_connection_t *mbx_get_or_evict_slot(uint8_t board, uint8_t channel) {
  if (!g_mbx_conn_mutex || xSemaphoreTake(g_mbx_conn_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
    return NULL;
  }

  mbx_connection_t *existing = mbx_find_connection(board, channel);
  if (existing) {
    xSemaphoreGive(g_mbx_conn_mutex);
    return existing;
  }

  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (!g_mbx_conn[i].in_use) {
      g_mbx_conn[i].in_use = true;
      g_mbx_conn[i].board = board;
      g_mbx_conn[i].channel = channel;
      g_mbx_conn[i].next_transaction_id = 1;
      xSemaphoreGive(g_mbx_conn_mutex);
      return &g_mbx_conn[i];
    }
  }

  // Alle slots optaget — evict den mindst for nylig brugte (LRU) BLANDT DEM
  // DER IKKE ER I FLIGHT LIGE NU. Sjældent i praksis med kun ét board/2
  // kanaler i dag, men gør systemet robust hvis flere boards/kanaler tages i
  // brug end MODBUS_EXPANSION_MAX_CONNECTIONS. BUG-419: springer bevidst
  // in-flight forbindelser over — at evict'e én midt i en transaktion ville
  // rive tæppet væk under den worker, der stadig bruger den (samme klasse
  // fejl som selve hovedbuggen).
  uint8_t oldest_idx = 255;
  uint32_t oldest_ms = UINT32_MAX;
  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (modbus_expansion_async_is_channel_inflight(g_mbx_conn[i].board, g_mbx_conn[i].channel)) continue;
    if (g_mbx_conn[i].last_activity_ms < oldest_ms) {
      oldest_ms = g_mbx_conn[i].last_activity_ms;
      oldest_idx = i;
    }
  }

  if (oldest_idx == 255) {
    // Every connection is actively in flight — nothing safe to evict.
    xSemaphoreGive(g_mbx_conn_mutex);
    return NULL;
  }

  g_mbx_conn[oldest_idx].client.stop();
  ESP_LOGI(TAG, "Evicting connection to board=%u ch=%u for board=%u ch=%u (alle %d slots optaget)",
           g_mbx_conn[oldest_idx].board, g_mbx_conn[oldest_idx].channel, board, channel,
           MODBUS_EXPANSION_MAX_CONNECTIONS);
  g_mbx_conn[oldest_idx].in_use = true;
  g_mbx_conn[oldest_idx].board = board;
  g_mbx_conn[oldest_idx].channel = channel;
  g_mbx_conn[oldest_idx].next_transaction_id = 1;
  xSemaphoreGive(g_mbx_conn_mutex);
  return &g_mbx_conn[oldest_idx];
}

void modbus_expansion_close(uint8_t board, uint8_t channel) {
  mbx_connection_t *c = mbx_find_connection(board, channel);
  if (c) {
    c->client.stop();
    memset(c, 0, sizeof(*c));
  }
}

uint8_t modbus_expansion_get_connections(mbx_connection_info_t *out, uint8_t max_out) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS && n < max_out; i++) {
    if (!g_mbx_conn[i].in_use) continue;
    out[n].board = g_mbx_conn[i].board;
    out[n].channel = g_mbx_conn[i].channel;
    out[n].connected = g_mbx_conn[i].client.connected();
    out[n].last_activity_ms = g_mbx_conn[i].last_activity_ms;
    n++;
  }
  return n;
}

// Selve transaktionen: bygger [MBAP(7)][PDU], sender, modtager svar, pakker
// svar-PDU'en ud. Returnerer MB_OK/MB_TIMEOUT/MB_CRC_ERROR/MB_EXCEPTION/
// MB_INVALID_ADDRESS(boardet ikke fundet)/MB_BUS_BUSY(kunne ikke forbinde).
static mb_error_code_t mbx_transact_inner(uint8_t board, uint8_t channel, uint8_t slave_id,
                                           const uint8_t *pdu, uint8_t pdu_len,
                                           uint8_t *resp_pdu, uint8_t *resp_pdu_len, uint8_t max_resp_pdu_len) {
  *resp_pdu_len = 0;

  if (board < 1 || board > EXPANSION_BOARD_MAX || channel < 1 || channel > 8) {
    return MB_INVALID_ADDRESS;
  }
  uint8_t board_idx = board - 1;
  if (!g_persist_config.expansion_boards[board_idx].configured) {
    return MB_INVALID_ADDRESS;
  }

  char ip_str[16];
  network_config_ip_to_str(g_persist_config.expansion_boards[board_idx].ip, ip_str);
  uint16_t port = 502 + (channel - 1);

  mbx_connection_t *conn = mbx_get_or_evict_slot(board, channel);
  if (!conn) {
    // BUG-419: every connection slot is full AND actively in flight, or the
    // pool mutex itself couldn't be taken in time — never dereference NULL.
    return MB_BUS_BUSY;
  }

  if (!conn->client.connected()) {
    if (!conn->client.connect(ip_str, port, MBX_CONNECT_TIMEOUT_MS)) {
      return MB_TIMEOUT;  // Kunne ikke forbinde — behandles som TIMEOUT (samme som en tavs slave for kalderen)
    }
  }

  // BUG-469: smid evt. sene svar fra en tidligere, timeout'et transaktion
  // vaek, saa de ikke laeses som svar paa DENNE forespoergsel.
  while (conn->client.available()) conn->client.read();

  const uint32_t tmo_ms = modbus_expansion_effective_timeout_ms(board, channel);  // FEAT-458
  uint16_t txn_id = conn->next_transaction_id++;
  uint16_t length = 1 + pdu_len;  // Unit ID + PDU

  // MBAP(7) + max PDU vi selv sender. v7.9.68.0: udvidet fra 8 til 40 byte —
  // FC01-06 enkelt-register krævede kun 8, men FC15/FC16 med op til 16
  // registre/coils kræver op til 1+2+2+1+32 = 38 byte PDU.
  uint8_t packet[7 + 40];
  if (pdu_len > sizeof(packet) - 7) return MB_INVALID_ADDRESS;

  packet[0] = (txn_id >> 8) & 0xFF;
  packet[1] = txn_id & 0xFF;
  packet[2] = 0x00;  // Protocol ID hi
  packet[3] = 0x00;  // Protocol ID lo
  packet[4] = (length >> 8) & 0xFF;
  packet[5] = length & 0xFF;
  packet[6] = slave_id;  // Unit ID = den fysiske RTU-slaves adresse (§4.1-rettelsen, se designdokumentet)
  memcpy(packet + 7, pdu, pdu_len);

  size_t written = conn->client.write(packet, 7 + pdu_len);
  if (written != (size_t)(7 + pdu_len)) {
    conn->client.stop();
    return MB_TIMEOUT;
  }

  // Læs MBAP-svar-header (7 byte) med timeout
  uint32_t start = millis();
  uint8_t hdr[7];
  uint8_t hdr_got = 0;
  while (hdr_got < 7) {
    if (conn->client.available()) {
      int b = conn->client.read();
      if (b < 0) break;
      hdr[hdr_got++] = (uint8_t)b;
    } else if (!conn->client.connected()) {
      conn->client.stop();
      return MB_TIMEOUT;
    } else if (millis() - start > tmo_ms) {
      // BUG-469: luk forbindelsen ved timeout — efter en board-genstart var
      // socketen halvaaben (client.connected() blev ved med at sige ja), og
      // PLC'en sendte i 8+ min ind i en doed forbindelse uden at genforbinde.
      // Naeste kald forbinder paa ny (billigt paa LAN).
      conn->client.stop();
      return MB_TIMEOUT;
    } else {
      delay(1);
    }
  }

  uint16_t resp_length = (hdr[4] << 8) | hdr[5];
  if (resp_length < 1 || resp_length > (max_resp_pdu_len + 1)) {
    conn->client.stop();
    return MB_CRC_ERROR;  // Genbruger MB_CRC_ERROR som generisk "ugyldigt svar"-kode, samme som RTU-siden ved korrupt data
  }
  uint8_t resp_pdu_expected_len = (uint8_t)(resp_length - 1);  // minus Unit ID

  uint8_t got = 0;
  while (got < resp_pdu_expected_len) {
    if (conn->client.available()) {
      int b = conn->client.read();
      if (b < 0) break;
      resp_pdu[got++] = (uint8_t)b;
    } else if (!conn->client.connected()) {
      conn->client.stop();
      return MB_TIMEOUT;
    } else if (millis() - start > tmo_ms) {
      // BUG-469: luk forbindelsen ved timeout — efter en board-genstart var
      // socketen halvaaben (client.connected() blev ved med at sige ja), og
      // PLC'en sendte i 8+ min ind i en doed forbindelse uden at genforbinde.
      // Naeste kald forbinder paa ny (billigt paa LAN).
      conn->client.stop();
      return MB_TIMEOUT;
    } else {
      delay(1);
    }
  }

  conn->last_activity_ms = millis();
  if (got > 0) g_mbx_board_last_rx_ms[board_idx] = conn->last_activity_ms ? conn->last_activity_ms : 1;  // BUG-431
  *resp_pdu_len = got;

  if (got == 0) { conn->client.stop(); return MB_TIMEOUT; }  // BUG-469
  if (resp_pdu[0] & 0x80) return MB_EXCEPTION;  // Modbus-exception (fra slaven ELLER boardets egen gateway, se manualens §3.3/§5)
  return MB_OK;
}

// FEAT-457: alle transaktioner går hertil, så statistikken pr. (board, kanal)
// tælles ét sted, uanset udfald.
static mb_error_code_t mbx_transact(uint8_t board, uint8_t channel, uint8_t slave_id,
                                     const uint8_t *pdu, uint8_t pdu_len,
                                     uint8_t *resp_pdu, uint8_t *resp_pdu_len, uint8_t max_resp_pdu_len) {
  mb_error_code_t err = mbx_transact_inner(board, channel, slave_id, pdu, pdu_len, resp_pdu, resp_pdu_len, max_resp_pdu_len);
  if (err != MB_INVALID_ADDRESS) mbx_stats_record(board, channel, err);
  return err;
}

mb_error_code_t modbus_expansion_read_coil(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool *result) {
  *result = false;
  uint8_t req[5] = { 0x01, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x01 };
  uint8_t resp[8]; uint8_t resp_len;
  mb_error_code_t err = mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
  if (err != MB_OK) return err;
  if (resp_len >= 3 && resp[0] == 0x01) {
    *result = (resp[2] & 0x01) != 0;
    return MB_OK;
  }
  return MB_CRC_ERROR;
}

mb_error_code_t modbus_expansion_read_input(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool *result) {
  *result = false;
  uint8_t req[5] = { 0x02, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x01 };
  uint8_t resp[8]; uint8_t resp_len;
  mb_error_code_t err = mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
  if (err != MB_OK) return err;
  if (resp_len >= 3 && resp[0] == 0x02) {
    *result = (resp[2] & 0x01) != 0;
    return MB_OK;
  }
  return MB_CRC_ERROR;
}

mb_error_code_t modbus_expansion_read_holding(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t *result) {
  *result = 0;
  uint8_t req[5] = { 0x03, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x01 };
  uint8_t resp[8]; uint8_t resp_len;
  mb_error_code_t err = mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
  if (err != MB_OK) return err;
  if (resp_len >= 4 && resp[0] == 0x03) {
    *result = (resp[2] << 8) | resp[3];
    return MB_OK;
  }
  return MB_CRC_ERROR;
}

// FEAT-461: FC03 med flere registre (1-16) i én transaktion.
mb_error_code_t modbus_expansion_read_holdings(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, uint16_t *values) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;
  uint8_t req[5] = { 0x03, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, count };
  uint8_t resp[2 + 16 * 2]; uint8_t resp_len;
  mb_error_code_t err = mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
  if (err != MB_OK) return err;
  if (resp_len >= (uint8_t)(2 + count * 2) && resp[0] == 0x03 && resp[1] == count * 2) {
    for (uint8_t i = 0; i < count; i++) values[i] = (uint16_t)((resp[2 + i * 2] << 8) | resp[3 + i * 2]);
    return MB_OK;
  }
  return MB_CRC_ERROR;
}

mb_error_code_t modbus_expansion_read_input_register(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t *result) {
  *result = 0;
  uint8_t req[5] = { 0x04, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), 0x00, 0x01 };
  uint8_t resp[8]; uint8_t resp_len;
  mb_error_code_t err = mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
  if (err != MB_OK) return err;
  if (resp_len >= 4 && resp[0] == 0x04) {
    *result = (resp[2] << 8) | resp[3];
    return MB_OK;
  }
  return MB_CRC_ERROR;
}

mb_error_code_t modbus_expansion_write_coil(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool value) {
  uint8_t req[5] = { 0x05, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), (uint8_t)(value ? 0xFF : 0x00), 0x00 };
  uint8_t resp[8]; uint8_t resp_len;
  return mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
}

mb_error_code_t modbus_expansion_write_holding(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t value) {
  uint8_t req[5] = { 0x06, (uint8_t)(address >> 8), (uint8_t)(address & 0xFF), (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  uint8_t resp[8]; uint8_t resp_len;
  return mbx_transact(board, channel, slave_id, req, sizeof(req), resp, &resp_len, sizeof(resp));
}

// v7.9.68.0: FC16 multi-register write — PDU shape mirrors modbus_master.cpp's
// modbus_master_write_holdings() byte-for-byte (see file header design note),
// minus the slave-id prefix and CRC suffix (MBAP carries the Unit ID instead).
mb_error_code_t modbus_expansion_write_holdings(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  uint8_t byte_count = count * 2;
  uint8_t req[6 + 16 * 2];  // fc(1)+addr(2)+count(2)+byte_count(1)+data(count*2)
  req[0] = 0x10;
  req[1] = (address >> 8) & 0xFF;
  req[2] = address & 0xFF;
  req[3] = 0x00;
  req[4] = count;
  req[5] = byte_count;
  for (uint8_t i = 0; i < count; i++) {
    req[6 + i * 2] = (values[i] >> 8) & 0xFF;
    req[7 + i * 2] = values[i] & 0xFF;
  }

  uint8_t resp[8]; uint8_t resp_len;
  return mbx_transact(board, channel, slave_id, req, (uint8_t)(6 + byte_count), resp, &resp_len, sizeof(resp));
}

// v7.9.68.0: FC15 multi-coil write — bit-packs 8 coils/byte (LSB-first), same
// chip=i/8,bit=i%8 idiom as gpio_driver.cpp's shift-register cache, mirrors
// modbus_master.cpp's modbus_master_write_coils().
mb_error_code_t modbus_expansion_write_coils(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const bool *values) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  uint8_t byte_count = (count + 7) / 8;
  uint8_t req[6 + 2];  // fc(1)+addr(2)+count(2)+byte_count(1)+data(<=2)
  req[0] = 0x0F;
  req[1] = (address >> 8) & 0xFF;
  req[2] = address & 0xFF;
  req[3] = 0x00;
  req[4] = count;
  req[5] = byte_count;
  req[6] = 0x00;
  req[7] = 0x00;
  for (uint8_t i = 0; i < count; i++) {
    if (values[i]) {
      req[6 + i / 8] |= (uint8_t)(1 << (i % 8));
    }
  }

  uint8_t resp[8]; uint8_t resp_len;
  return mbx_transact(board, channel, slave_id, req, (uint8_t)(6 + byte_count), resp, &resp_len, sizeof(resp));
}
