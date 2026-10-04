// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file mb_debug.cpp
 * @brief FEAT-421: live Modbus Master-trafik i Telnet-konsollen
 *
 * Se mb_debug.h for opdelingen capture (Core 0, ingen formatering) /
 * loop (formatering + Telnet-output).
 */

#include "mb_debug.h"
#include "mb_activity_log.h"
#include "console_telnet.h"
#include "telnet_server.h"
#include "constants.h"
#include "debug.h"
#include "cli_shell.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

extern Console *g_serial_console;  // main.cpp

/* ============================================================================
 * STATE
 * ============================================================================ */

#define MB_DBG_RB_SIZE     4096  // ~90 typiske FC03-transaktioner
#define MB_DBG_MAX_BYTES   64    // pr. request/response (FC03/FC16 x16 = 37/41)
#define MB_DBG_PER_LOOP    4     // records pr. loop() — loop'et maa ikke sultes

static volatile uint8_t g_level = 0;
static RingbufHandle_t g_rb = NULL;          // oprettes ved foerste enable, slettes aldrig
static TelnetServer *g_target_telnet = NULL; // persistent (network_manager ejer den)
static bool g_target_serial = false;
static volatile uint32_t g_dropped = 0;
static uint8_t g_rx_wait[256];               // beskyttet af UART-mutex'en

typedef struct {
  uint32_t t_start_ms;
  uint32_t baudrate;
  uint16_t elapsed_ms;
  uint8_t  source;
  uint8_t  result;
  uint8_t  drained;
  uint8_t  dir_pin;
  uint8_t  req_len;     // gemt (afkortet til MB_DBG_MAX_BYTES)
  uint8_t  resp_len;    // gemt (afkortet)
  uint8_t  resp_total;  // faktisk modtaget
  uint8_t  data[];      // req[req_len] + resp[resp_len] + wait[resp_len]
} MbDbgRecord;

/* ============================================================================
 * CAPTURE (kaldes fra modbus_master_send_request, evt. paa Core 0)
 * ============================================================================ */

uint8_t mb_debug_level(void) { return g_level; }

uint8_t *mb_debug_rx_wait_buf(void) { return g_rx_wait; }

void mb_debug_capture(const mb_debug_txn_t *txn,
                      const uint8_t *request, uint8_t request_len,
                      const uint8_t *response, uint8_t response_len,
                      mb_error_code_t result) {
  if (g_level == 0 || g_rb == NULL || txn == NULL) return;

  uint8_t rq = request ? (request_len > MB_DBG_MAX_BYTES ? MB_DBG_MAX_BYTES : request_len) : 0;
  uint8_t rs = response ? (response_len > MB_DBG_MAX_BYTES ? MB_DBG_MAX_BYTES : response_len) : 0;

  // Skriv direkte ind i ringbufferen (ingen mellembuffer paa den lille
  // async-stak). Traadsikkert — MB_NOT_ENABLED-vejen kaldes FOER UART-mutex'en.
  void *slot = NULL;
  if (xRingbufferSendAcquire(g_rb, &slot, sizeof(MbDbgRecord) + rq + 2 * rs, 0) != pdTRUE || !slot) {
    g_dropped++;
    return;
  }
  MbDbgRecord *r = (MbDbgRecord *)slot;
  uint32_t elapsed = millis() - txn->t_start_ms;
  r->t_start_ms = txn->t_start_ms;
  r->baudrate = txn->baudrate;
  r->elapsed_ms = (uint16_t)(elapsed > 0xFFFF ? 0xFFFF : elapsed);
  r->source = txn->source;
  r->result = (uint8_t)result;
  r->drained = txn->drained;
  r->dir_pin = txn->dir_pin;
  r->req_len = rq;
  r->resp_len = rs;
  r->resp_total = response ? response_len : 0;
  if (rq) memcpy(r->data, request, rq);
  if (rs) {
    memcpy(r->data + rq, response, rs);
    memcpy(r->data + rq + rs, g_rx_wait, rs);
  }
  xRingbufferSendComplete(g_rb, slot);
}

/* ============================================================================
 * FORMATERING (kun fra loop())
 * ============================================================================ */

static const char *kTx = ">TX>";
static const char *kRx = "<RX<";

static void format_uptime(uint32_t ms, char *out, size_t cap) {
  uint32_t s = ms / 1000UL;
  snprintf(out, cap, "%lu:%02u:%02u:%02u.%03u",
           (unsigned long)(s / 86400UL), (unsigned)((s / 3600UL) % 24UL),
           (unsigned)((s / 60UL) % 60UL), (unsigned)(s % 60UL), (unsigned)(ms % 1000UL));
}

static void out_line(const char *line) {
  if (g_target_telnet) {
    telnet_server_writeline(g_target_telnet, line);
  } else if (g_target_serial && g_serial_console && g_serial_console->write_line) {
    g_serial_console->write_line(g_serial_console->context, line);
  }
}

static void emit(uint32_t ts_ms, const char *dir, const char *label, const char *fmt, ...) {
  char ts[24];
  format_uptime(ts_ms, ts, sizeof(ts));
  char content[200];
  va_list args;
  va_start(args, fmt);
  vsnprintf(content, sizeof(content), fmt, args);
  va_end(args);
  char line[256];
  snprintf(line, sizeof(line), "DEBUG [%s] mb_master %s %s: %s", ts, dir, label, content);
  out_line(line);
}

static const char *result_name(uint8_t r) {
  switch (r) {
    case MB_OK:                    return "MB_OK";
    case MB_TIMEOUT:               return "MB_TIMEOUT";
    case MB_CRC_ERROR:             return "MB_CRC_ERROR";
    case MB_EXCEPTION:             return "MB_EXCEPTION";
    case MB_MAX_REQUESTS_EXCEEDED: return "MB_MAX_REQUESTS_EXCEEDED";
    case MB_NOT_ENABLED:           return "MB_NOT_ENABLED";
    case MB_INVALID_SLAVE:         return "MB_INVALID_SLAVE";
    case MB_INVALID_ADDRESS:       return "MB_INVALID_ADDRESS";
    case MB_BUS_BUSY:              return "MB_BUS_BUSY";
    default:                       return "?";
  }
}

static const char *source_name(uint8_t s) {
  switch (s) {
    case MB_SRC_ST_LOGIC:  return "ST";
    case MB_SRC_CLI:       return "CLI";
    case MB_SRC_DASHBOARD: return "Dashboard";
    case MB_SRC_TREND:     return "Trend";  // FEAT-425
    default:               return "?";
  }
}

static const char *fc_name(uint8_t fc) {
  switch (fc) {
    case 0x01: return "Read Coils";
    case 0x02: return "Read Discrete Inputs";
    case 0x03: return "Read Holding Registers";
    case 0x04: return "Read Input Registers";
    case 0x05: return "Write Single Coil";
    case 0x06: return "Write Single Register";
    case 0x0F: return "Write Multiple Coils";
    case 0x10: return "Write Multiple Registers";
    default:   return "Unknown";
  }
}

static const char *exception_name(uint8_t code) {
  switch (code) {
    case 1: return "Illegal Function";
    case 2: return "Illegal Data Address";
    case 3: return "Illegal Data Value";
    case 4: return "Slave Device Failure";
    case 5: return "Acknowledge";
    case 6: return "Slave Device Busy";
    default: return "?";
  }
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* Kompakt decode af en hel RTU-frame (adresse+PDU+CRC). Skriver "" hvis
 * framen er for kort/ukendt — samme stille fallback som boardets. */
static void decode_frame(const uint8_t *f, uint8_t len, bool is_response, char *out, size_t cap) {
  out[0] = '\0';
  if (len < 4) return;
  uint8_t fc = f[1];
  int n = snprintf(out, cap, "ID: %02X (%u), ", f[0], f[0]);
  if (n < 0 || (size_t)n >= cap) return;
  size_t pos = (size_t)n;
  const uint8_t *crc = f + len - 2;

  if (is_response && (fc & 0x80)) {
    snprintf(out + pos, cap - pos, "FC%02X Exception %u (%s), CRC: %02X %02X",
             fc & 0x7F, f[2], exception_name(f[2]), crc[0], crc[1]);
    return;
  }

  n = snprintf(out + pos, cap - pos, "FC%02X %s", fc, fc_name(fc));
  if (n < 0) return;
  pos += (size_t)n;

  if (!is_response) {
    if (len >= 8 && fc >= 0x01 && fc <= 0x04) {
      n = snprintf(out + pos, cap - pos, ", Addr: %u, Qty: %u", be16(f + 2), be16(f + 4));
    } else if (len >= 8 && fc == 0x05) {
      n = snprintf(out + pos, cap - pos, ", Addr: %u, Value: %s", be16(f + 2), be16(f + 4) ? "ON" : "OFF");
    } else if (len >= 8 && fc == 0x06) {
      int16_t v = (int16_t)be16(f + 4);
      n = snprintf(out + pos, cap - pos, ", Addr: %u, Value: %u (signed: %d)", be16(f + 2), be16(f + 4), v);
    } else if (len >= 9 && (fc == 0x0F || fc == 0x10)) {
      n = snprintf(out + pos, cap - pos, ", Addr: %u, Qty: %u, Bytes: %u", be16(f + 2), be16(f + 4), f[6]);
      if (n > 0 && fc == 0x10) {
        pos += (size_t)n;
        n = snprintf(out + pos, cap - pos, ", Values:");
        uint8_t regs = f[6] / 2;
        for (uint8_t i = 0; i < regs && i < 8 && n > 0 && 7 + 2 * i + 1 < len - 2; i++) {
          pos += (size_t)n;
          n = snprintf(out + pos, cap - pos, " %u", be16(f + 7 + 2 * i));
        }
        if (n > 0 && regs > 8) { pos += (size_t)n; n = snprintf(out + pos, cap - pos, " ..."); }
      }
    } else {
      n = 0;
    }
  } else {
    if ((fc == 0x03 || fc == 0x04) && len >= 5) {
      uint8_t bytes = f[2];
      n = snprintf(out + pos, cap - pos, ", Bytes: %u, Regs:", bytes);
      for (uint8_t i = 0; i < bytes / 2 && i < 8 && n > 0 && 3 + 2 * i + 1 < len - 2; i++) {
        pos += (size_t)n;
        int16_t v = (int16_t)be16(f + 3 + 2 * i);
        if (v < 0) n = snprintf(out + pos, cap - pos, " %u(%d)", (uint16_t)v, v);
        else       n = snprintf(out + pos, cap - pos, " %u", (uint16_t)v);
      }
      if (n > 0 && bytes / 2 > 8) { pos += (size_t)n; n = snprintf(out + pos, cap - pos, " ..."); }
    } else if ((fc == 0x01 || fc == 0x02) && len >= 5) {
      n = snprintf(out + pos, cap - pos, ", Bytes: %u, Bits: %02X", f[2], f[3]);
    } else if ((fc == 0x05 || fc == 0x06 || fc == 0x0F || fc == 0x10) && len >= 8) {
      n = snprintf(out + pos, cap - pos, ", Addr: %u, %s: %u", be16(f + 2),
                   (fc == 0x05 || fc == 0x06) ? "Value" : "Qty", be16(f + 4));
    } else {
      n = 0;
    }
  }
  if (n < 0) return;
  pos += (size_t)n;
  if (pos < cap) snprintf(out + pos, cap - pos, ", CRC: %02X %02X", crc[0], crc[1]);
}

static void hex_dump(const uint8_t *d, uint8_t len, bool truncated, char *out, size_t cap) {
  size_t pos = 0;
  out[0] = '\0';
  for (uint8_t i = 0; i < len && pos + 4 < cap; i++) {
    pos += (size_t)snprintf(out + pos, cap - pos, "%02X ", d[i]);
  }
  if (truncated && pos + 4 < cap) snprintf(out + pos, cap - pos, "...");
}

static uint16_t crc16(const uint8_t *b, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= b[i];
    for (uint8_t j = 0; j < 8; j++) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

static void print_record(const MbDbgRecord *r, uint8_t lvl) {
  const uint8_t *req = r->data;
  const uint8_t *resp = r->data + r->req_len;
  const uint8_t *wait = r->data + r->req_len + r->resp_len;
  const uint32_t t0 = r->t_start_ms;
  char buf[200];

  if (r->req_len < 2) return;

  emit(t0, kTx, "start", "FC: %02X, Slave: %u, Len: %u, Kilde: %s",
       req[1], req[0], r->req_len, source_name(r->source));

  // Afvist foer bussen blev roert
  if (r->result == MB_NOT_ENABLED || r->result == MB_BUS_BUSY) {
    emit(t0, kRx, "result", "%s (%s)", result_name(r->result),
         r->result == MB_NOT_ENABLED ? "Modbus Master er deaktiveret" : "UART optaget af anden transaktion i >2s");
    return;
  }

  decode_frame(req, r->req_len, false, buf, sizeof(buf));
  if (buf[0]) emit(t0, kTx, "decode", "%s", buf);

  if (lvl >= 2 && r->drained) emit(t0, kRx, "noise", "draining %u byte(s) from bus", r->drained);
  if (lvl >= 3) emit(t0, kTx, "dir", "DE/RE -> TX (GPIO%u HIGH)", r->dir_pin);
  if (lvl >= 7) {
    hex_dump(req, r->req_len, false, buf, sizeof(buf));
    emit(t0, kTx, "packet", "%s", buf);
  }
  if (lvl >= 3) emit(t0, kRx, "dir", "DE/RE -> RX (GPIO%u LOW)", r->dir_pin);

  if (lvl >= 4) {
    uint32_t ts = t0;
    for (uint8_t i = 0; i < r->resp_len; i++) {
      ts += wait[i];
      char label[16];
      snprintf(label, sizeof(label), "byte[%u]", i);
      emit(ts, kRx, label, "0x%02X (ventede %ums)", resp[i], wait[i]);
    }
  }

  const uint32_t t_end = t0 + r->elapsed_ms;
  if (r->resp_total == 0 || r->result == MB_TIMEOUT) {
    emit(t_end, kRx, "result", "%s (modtog %u byte(s), %ums, %lu baud)", result_name(r->result),
         r->resp_total, r->elapsed_ms, (unsigned long)r->baudrate);
    return;
  }

  if (lvl >= 8) {
    hex_dump(resp, r->resp_len, r->resp_total > r->resp_len, buf, sizeof(buf));
    emit(t_end, kRx, "packet", "%s", buf);
  }

  // Decode kun naar CRC'en har valideret svaret (samme regel som boardet)
  if (r->result == MB_OK || r->result == MB_EXCEPTION) {
    decode_frame(resp, r->resp_len, true, buf, sizeof(buf));
    if (buf[0]) emit(t_end, kRx, "decode", "%s, Status: %s", buf, result_name(r->result));
  }

  if (lvl >= 6 && r->resp_len >= 3 && r->resp_len == r->resp_total) {
    uint16_t rx_crc = (uint16_t)((resp[r->resp_len - 1] << 8) | resp[r->resp_len - 2]);
    uint16_t calc = crc16(resp, r->resp_len - 2);
    emit(t_end, kRx, "parse", "CRC modtaget=%04X beregnet=%04X -> %s", rx_crc, calc, result_name(r->result));
  }

  emit(t_end, kRx, "result", "%s (modtog %u byte(s), %ums)", result_name(r->result),
       r->resp_total, r->elapsed_ms);
}

/* ============================================================================
 * LOOP
 * ============================================================================ */

static void stop_debug(void) {
  g_level = 0;
  g_target_telnet = NULL;
  g_target_serial = false;
}

void mb_debug_loop(void) {
  if (g_rb == NULL) return;

  // Telnet-sessionen er lukket -> slaa debug fra (ellers faar en senere,
  // anden bruger paa samme server trafikken uden at have bedt om den).
  if (g_level > 0 && g_target_telnet && !telnet_server_client_connected(g_target_telnet)) {
    stop_debug();
  }

  for (int i = 0; i < MB_DBG_PER_LOOP; i++) {
    size_t size = 0;
    MbDbgRecord *r = (MbDbgRecord *)xRingbufferReceive(g_rb, &size, 0);
    if (!r) break;
    uint8_t lvl = g_level;
    if (lvl > 0 && size >= sizeof(MbDbgRecord)) {
      print_record(r, lvl);
    }
    vRingbufferReturnItem(g_rb, r);
  }

  if (g_dropped && g_level > 0) {
    uint32_t d = g_dropped;
    g_dropped = 0;
    emit(millis(), kRx, "dropped", "%lu transaktion(er) ikke vist (debug-buffer fuld - brug lavere level)",
         (unsigned long)d);
  }
}

/* ============================================================================
 * CLI
 * ============================================================================ */

static void print_usage(void) {
  debug_println("Brug: debug modbus [master|all] level <1-8>");
  debug_println("      no debug modbus");
  debug_println("  Viser Modbus Master-trafikken (RS485) live i DENNE Telnet-session.");
  debug_println("  1 = start/decode/resultat, 2 = + stoej, 3 = + DE/RE,");
  debug_println("  4 = + timing pr. modtaget byte, 6 = + CRC-tjek,");
  debug_println("  7 = + raa TX-hex, 8 = + raa RX-hex");
  debug_println("  Gemmes IKKE - altid fra efter reboot, og naar Telnet-sessionen lukkes.");
}

void cli_cmd_debug_modbus(uint8_t argc, char *argv[]) {
  // argv[0] = "modbus"
  uint8_t i = 1;
  if (i < argc && (!strcasecmp(argv[i], "master") || !strcasecmp(argv[i], "all") ||
                   !strcasecmp(argv[i], "a"))) {
    i++;
  }
  if (i + 1 >= argc || strcasecmp(argv[i], "level") != 0) {
    print_usage();
    return;
  }
  char *end = NULL;
  long level = strtol(argv[i + 1], &end, 10);
  if (!end || *end != '\0' || level < 1 || level > MB_DEBUG_LEVEL_MAX) {
    debug_printf("FEJL: level skal vaere 1-%d\n", MB_DEBUG_LEVEL_MAX);
    return;
  }

  Console *console = cli_shell_get_debug_console();
  TelnetServer *telnet = console_telnet_get_server(console);
  bool serial = (console != NULL && console == g_serial_console);

  if (!telnet) {
#if MODBUS_SINGLE_TRANSCEIVER
    if (serial) {
      debug_println("FEJL: debug modbus virker kun via Telnet paa dette board -");
      debug_println("      seriel-konsollen deler forbindelse (GPIO1/3) med RS485.");
      return;
    }
#endif
    if (!serial) {
      debug_println("FEJL: debug modbus kraever en Telnet-session (web-CLI kan ikke vise live output)");
      return;
    }
  }

  if (g_rb == NULL) {
    g_rb = xRingbufferCreate(MB_DBG_RB_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (g_rb == NULL) {
      debug_println("FEJL: kunne ikke allokere debug-buffer (lav heap)");
      return;
    }
  }

  g_target_telnet = telnet;
  g_target_serial = (telnet == NULL) && serial;
  g_dropped = 0;
  g_level = (uint8_t)level;
  debug_printf("ok - modbus-debug level %ld for Modbus Master (%s)\n", level,
               telnet ? "Telnet" : "seriel");
  debug_println("     Slaa fra med 'no debug modbus'");
}

void cli_cmd_no_debug_modbus(void) {
  stop_debug();
  debug_println("ok - modbus-debug slaaet fra");
}

void mb_debug_print_status(void) {
  if (g_level == 0) {
    debug_println("  modbus (master):   DISABLED  ('debug modbus level <1-8>')");
  } else {
    debug_printf("  modbus (master):   LEVEL %u -> %s\n", g_level,
                 g_target_telnet ? "Telnet" : "seriel");
  }
}
