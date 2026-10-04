// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file mb_debug.h
 * @brief FEAT-421: live Modbus Master-trafik i Telnet-konsollen
 *
 * Samme funktion og linjeformat som expansion boardets `debug modbus`
 * (board-repoet, src/modbus_channel.cpp):
 *   DEBUG [D:HH:MM:SS.mmm] mb_master >TX>|<RX< <label>: <indhold>
 *
 * Opdeling (vigtigt):
 *  - modbus_master_send_request() (koerer i mb_async-tasken paa Core 0
 *    med kun 4 KB stak, ELLER synkront fra CLI) kalder kun
 *    mb_debug_capture() — en ren memcpy af raa bytes/tider ind i en
 *    FreeRTOS-ringbuffer, uden formatering og uden I/O, saa RTU-timingen
 *    ikke paavirkes (board-repoets lektion: Serial.printf() pr. byte gav
 *    falske MB_TIMEOUT).
 *  - mb_debug_loop() (fra loop(), samme task som Telnet-serveren)
 *    formaterer/decoder og skriver linjerne til den Telnet-session der
 *    slog debug til.
 *
 * Kun Telnet (og seriel-konsollen paa boards med SEPARAT master-UART):
 * paa ES32D26 deler USB-konsollen GPIO1/3 med RS485 — debug-linjer paa
 * Serial ville blive sendt ud paa Modbus-bussen.
 *
 * Level (1-8) er kun i RAM — altid slaaet fra efter reboot.
 */

#ifndef MB_DEBUG_H
#define MB_DEBUG_H

#include <stdint.h>
#include "types.h"
#include "console.h"

#define MB_DEBUG_LEVEL_MAX 8

/* Data som modbus_master_send_request() samler under EN transaktion.
 * Bufferne ligger statisk i mb_debug.cpp (beskyttet af UART-mutex'en),
 * ikke paa den lille async-stak. */
typedef struct {
  uint32_t t_start_ms;      // millis() ved start
  uint8_t  source;          // mb_activity_source_t
  uint8_t  drained;         // stoej-bytes toemt foer TX
  uint8_t  dir_pin;         // DE/RE GPIO
  uint32_t baudrate;
} mb_debug_txn_t;

/** Aktuelt level (0 = fra). Billigt at kalde fra enhver task. */
uint8_t mb_debug_level(void);

/** Buffer til ventetid pr. modtaget byte (ms, satureret 255), sized
 *  til maks. response-laengde. Kun gyldig mens UART-mutex'en holdes. */
uint8_t *mb_debug_rx_wait_buf(void);

/** Kaldes EN gang pr. transaktion, paa alle retur-veje. Ikke-blokerende;
 *  taber (og taeller) transaktionen hvis ringbufferen er fuld. */
void mb_debug_capture(const mb_debug_txn_t *txn,
                      const uint8_t *request, uint8_t request_len,
                      const uint8_t *response, uint8_t response_len,
                      mb_error_code_t result);

/** CLI: `debug modbus [master|all] level <1-8>` / `no debug modbus`. */
void cli_cmd_debug_modbus(uint8_t argc, char *argv[]);
void cli_cmd_no_debug_modbus(void);

/** Linje til `show debug`. */
void mb_debug_print_status(void);

/** Fra loop(): formaterer og sender ventende linjer til Telnet. */
void mb_debug_loop(void);

#endif // MB_DEBUG_H
