/**
 * @file modbus_expansion.h
 * @brief FEAT-410: Modbus TCP-transport mod expansion-boardenes data-plan
 * (port 502+kanal-1, se PLC_INTEGRATION_MANUAL.md §3). Mirrors
 * modbus_master.h's offentlige API 1:1 (samme FC01-06-funktioner, samme
 * mb_error_code_t), blot med (board, kanal) som to ekstra, indledende
 * parametre — PDU-opbygningen er BYTE-FOR-BYTE identisk med RTU's (se
 * modbus_master.cpp), kun rammen udenom (MBAP i stedet for adresse-byte+CRC)
 * er forskellig.
 *
 * `board` er 1-8 (samme "nr" som expansion_api_client.h's board-CRUD bruger
 * — internt array-index = board-1). `channel` er 1-8 (A=1, B=2, ...).
 *
 * Denne fil er BEVIDST kun transport (ét kald ind, ét svar ud, synkron) —
 * kø/cache/backoff-laget ligger i modbus_expansion_async.h/.cpp (mirrors
 * mb_async.h/.cpp), som er det ST Logic-builtins (MBX_*) reelt taler med.
 */

#ifndef MODBUS_EXPANSION_H
#define MODBUS_EXPANSION_H

#include <stdint.h>
#include <stdbool.h>
#include "types.h"  // mb_error_code_t

mb_error_code_t modbus_expansion_read_coil(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool *result);
mb_error_code_t modbus_expansion_read_input(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool *result);
mb_error_code_t modbus_expansion_read_holding(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t *result);
mb_error_code_t modbus_expansion_read_input_register(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t *result);
mb_error_code_t modbus_expansion_write_coil(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, bool value);
mb_error_code_t modbus_expansion_write_holding(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint16_t value);

// v7.9.68.0: FC16/FC15 multi-value writes — mirrors modbus_master.cpp's
// modbus_master_write_holdings()/write_coils() PDU shape, count: 1-16.
mb_error_code_t modbus_expansion_write_holdings(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values);
mb_error_code_t modbus_expansion_write_coils(uint8_t board, uint8_t channel, uint8_t slave_id, uint16_t address, uint8_t count, const bool *values);

// Luk (og glem) en evt. åben forbindelse til (board, channel) — bruges når et
// board fjernes/redigeres (IP-skift), så en gammel socket ikke forbliver åben
// mod en forkert/afmeldt destination.
void modbus_expansion_close(uint8_t board, uint8_t channel);

/**
 * Realistisk forbindelses-loft (IKKE et arkitektonisk loft på 64 kanaler —
 * blot hvor mange samtidige TCP-sockets DENNE firmware/ESP-IDF-build reelt
 * har raad til, se .cpp-filens designnote). At skalere op til det fulde
 * 8-boards-x-8-kanaler-loft kraever enten at haeve dette (og verificere
 * CONFIG_LWIP_MAX_SOCKETS er sat tilsvarende hoejt) eller at skifte til en
 * "aabn-send-luk-per-transaktion"-model i stedet for vedvarende forbindelser.
 */
#define MODBUS_EXPANSION_MAX_CONNECTIONS 8

// Ingen per-kanal timeout-config cachet PLC-side (se modbus_expansion.cpp's
// designnote) — én fast, generøs transaktions-timeout for hele data-planet.
#define MODBUS_EXPANSION_TRANSACTION_TIMEOUT_MS 800

#endif // MODBUS_EXPANSION_H
