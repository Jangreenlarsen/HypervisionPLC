/**
 * @file rs485_stats.h
 * @brief FEAT-450: målt RS-485/UART-trafik (bytes og frames pr. retning)
 *
 * Tælles i protokol-laget (modbus_rx/modbus_tx for slaven, modbus_master for
 * masteren), så trafikken kan fordeles på rolle, også når begge deler én
 * transceiver (ES32D26). Dashboardet beregner udnyttelsen ud fra deltaer:
 * bytes × bit pr. tegn (start + 8 data + paritet + stop) ÷ baudrate.
 *
 * uint32-tællere: én skriver pr. rolle (slave-RX-loop / master under
 * bus-mutex'en), og læsere bruger kun deltaer — et wrap efter 4 GB håndteres
 * af klienten.
 */
#ifndef RS485_STATS_H
#define RS485_STATS_H

#include <stdint.h>

#define RS485_ROLE_SLAVE  0
#define RS485_ROLE_MASTER 1

typedef struct {
  volatile uint32_t tx_bytes;
  volatile uint32_t rx_bytes;
  volatile uint32_t tx_frames;
  volatile uint32_t rx_frames;
} Rs485RoleStats;

extern Rs485RoleStats g_rs485_stats[2];

static inline void rs485_count_tx_frame(uint8_t role, uint32_t bytes) {
  g_rs485_stats[role].tx_bytes += bytes;
  g_rs485_stats[role].tx_frames++;
}
static inline void rs485_count_rx_bytes(uint8_t role, uint32_t bytes) {
  g_rs485_stats[role].rx_bytes += bytes;
}
static inline void rs485_count_rx_frame(uint8_t role) {
  g_rs485_stats[role].rx_frames++;
}

#endif // RS485_STATS_H
