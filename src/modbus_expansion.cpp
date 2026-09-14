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
#include "modbus_expansion.h"
#include "expansion_api_client.h"
#include "config_struct.h"
#include "network_config.h"
#include "debug.h"

static const char *TAG = "MBX_TRANSPORT";

// Ingen per-kanal timeout-config cachet PLC-side (kanal-config er bevidst
// IKKE duplikeret her, se expansion_api_client.h's designnote — boardet er
// selv autoritativt). Datatransaktioner bruger derfor en generøs, fast
// timeout (MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS, .h) fremfor et ekstra
// REST-opslag pr. transaktion (upraktisk ved høj-frekvent polling).
// Fremtidig forbedring: cache boardets konfigurerede timeout_ms lokalt og
// genbruge den her.
#define MBX_TRANSACTION_TIMEOUT_MS  MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS
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

static mbx_connection_t *mbx_find_connection(uint8_t board, uint8_t channel) {
  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (g_mbx_conn[i].in_use && g_mbx_conn[i].board == board && g_mbx_conn[i].channel == channel) {
      return &g_mbx_conn[i];
    }
  }
  return NULL;
}

static mbx_connection_t *mbx_get_or_evict_slot(uint8_t board, uint8_t channel) {
  mbx_connection_t *existing = mbx_find_connection(board, channel);
  if (existing) return existing;

  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (!g_mbx_conn[i].in_use) {
      g_mbx_conn[i].in_use = true;
      g_mbx_conn[i].board = board;
      g_mbx_conn[i].channel = channel;
      g_mbx_conn[i].next_transaction_id = 1;
      return &g_mbx_conn[i];
    }
  }

  // Alle slots optaget — evict den mindst for nylig brugte (LRU). Sjældent i
  // praksis med kun ét board/2 kanaler i dag, men gør systemet robust hvis
  // flere boards/kanaler tages i brug end MODBUS_EXPANSION_MAX_CONNECTIONS.
  uint8_t oldest_idx = 0;
  uint32_t oldest_ms = UINT32_MAX;
  for (uint8_t i = 0; i < MODBUS_EXPANSION_MAX_CONNECTIONS; i++) {
    if (g_mbx_conn[i].last_activity_ms < oldest_ms) {
      oldest_ms = g_mbx_conn[i].last_activity_ms;
      oldest_idx = i;
    }
  }
  g_mbx_conn[oldest_idx].client.stop();
  ESP_LOGI(TAG, "Evicting connection to board=%u ch=%u for board=%u ch=%u (alle %d slots optaget)",
           g_mbx_conn[oldest_idx].board, g_mbx_conn[oldest_idx].channel, board, channel,
           MODBUS_EXPANSION_MAX_CONNECTIONS);
  g_mbx_conn[oldest_idx].in_use = true;
  g_mbx_conn[oldest_idx].board = board;
  g_mbx_conn[oldest_idx].channel = channel;
  g_mbx_conn[oldest_idx].next_transaction_id = 1;
  return &g_mbx_conn[oldest_idx];
}

void modbus_expansion_close(uint8_t board, uint8_t channel) {
  mbx_connection_t *c = mbx_find_connection(board, channel);
  if (c) {
    c->client.stop();
    memset(c, 0, sizeof(*c));
  }
}

// Selve transaktionen: bygger [MBAP(7)][PDU], sender, modtager svar, pakker
// svar-PDU'en ud. Returnerer MB_OK/MB_TIMEOUT/MB_CRC_ERROR/MB_EXCEPTION/
// MB_INVALID_ADDRESS(boardet ikke fundet)/MB_BUS_BUSY(kunne ikke forbinde).
static mb_error_code_t mbx_transact(uint8_t board, uint8_t channel, uint8_t slave_id,
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

  if (!conn->client.connected()) {
    if (!conn->client.connect(ip_str, port, MBX_CONNECT_TIMEOUT_MS)) {
      return MB_TIMEOUT;  // Kunne ikke forbinde — behandles som TIMEOUT (samme som en tavs slave for kalderen)
    }
  }

  uint16_t txn_id = conn->next_transaction_id++;
  uint16_t length = 1 + pdu_len;  // Unit ID + PDU

  uint8_t packet[7 + 8];  // MBAP(7) + max PDU vi selv sender (8 er rigeligt for FC01-06 enkelt-register)
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
    } else if (millis() - start > MBX_TRANSACTION_TIMEOUT_MS) {
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
    } else if (millis() - start > MBX_TRANSACTION_TIMEOUT_MS) {
      return MB_TIMEOUT;
    } else {
      delay(1);
    }
  }

  conn->last_activity_ms = millis();
  *resp_pdu_len = got;

  if (got == 0) return MB_TIMEOUT;
  if (resp_pdu[0] & 0x80) return MB_EXCEPTION;  // Modbus-exception (fra slaven ELLER boardets egen gateway, se manualens §3.3/§5)
  return MB_OK;
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
