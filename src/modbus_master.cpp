/**
 * @file modbus_master.cpp
 * @brief Modbus Master Implementation (UART1)
 *
 * Implements Modbus RTU Master on UART1 for reading/writing remote slaves.
 */

#include "modbus_master.h"
#include "mb_async.h"
#include "mb_activity_log.h"
#include "uart_driver.h"
#include "config_struct.h"
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>    // vTaskDelay() — BUG-336
#include <freertos/semphr.h>  // SemaphoreHandle_t — BUG-337
#if MODBUS_SINGLE_TRANSCEIVER
#include "gpio_driver.h"
#endif

/* ============================================================================
 * GLOBAL CONFIGURATION
 * ============================================================================ */

/* BUG-334: sat hvis RS485-aktivering blev afbrudt ved boot (ES32D26 deler
 * GPIO1/3 mellem USB-konsol og RS485). Kun i RAM — den gemte config er
 * uaendret, hvilket ellers gjorde tilstanden umulig at forstaa. */
bool g_modbus_master_boot_aborted = false;

/* BUG-338/339: se erklaering i modbus_master.h — bevidst UDENFOR
 * modbus_master_config_t for ikke at aendre PersistConfig's NVS-layout. */
uint32_t g_modbus_bus_busy_errors = 0;

/* BUG-337: mutex om den fysiske UART/RS485-transaktion i
 * modbus_master_send_request(). CLI's `mb read`/`mb write`/`mb scan`
 * kalder denne funktion direkte og synkront fra loopTask (Core 1), mens
 * den asynkrone Modbus Master-task (ST Logic/dashboard-trafik) kalder
 * PRAECIS SAMME funktion fra sin egen task paa Core 0 — helt uden
 * indbyrdes koordinering. Uden denne laas kan to transaktioner sende
 * SAMTIDIGT paa den delte RS485-bus: begge transceivere trykker DE/RE
 * hoejt paa samme tid (elektrisk kollision), og hver task kan laese
 * BYTES DER TILHOERER DEN ANDENS SVAR ind i sin egen response-buffer —
 * observeret som en `mb scan` der "finder" langt flere slaver end der
 * fysisk findes, fordi den opsnappede den anden tasks svar og
 * tilskrev det den forkerte adresse (CRC'en er kun 16-bit, saa et
 * fejlplaceret men i sig selv gyldigt svar bestaar tjekket). */
static SemaphoreHandle_t g_modbus_uart_mutex = NULL;

/* RAII-laas — se MbTempBaud i cli_commands_modbus_master.cpp for samme
 * moenster. Garanterer at mutex'en gives fri paa ALLE returnerings-veje
 * i modbus_master_send_request(), ogsaa selvom funktionen faar flere
 * exit-punkter i fremtiden — en glemt xSemaphoreGive() ville laase HELE
 * Modbus Master-subsystemet permanent. */
class ModbusUartLock {
public:
  ModbusUartLock() : held(false) {
    if (g_modbus_uart_mutex) {
      // Begraenset ventetid (ikke portMAX_DELAY): en fastlaast/mistet
      // laas skal give en fejl til den ventende kalder, ikke haenge
      // for evigt. 2s er rigeligt over selv en langsom, fuld timeout
      // (default 500-1000ms) + inter-frame-margin.
      held = (xSemaphoreTake(g_modbus_uart_mutex, pdMS_TO_TICKS(2000)) == pdTRUE);
    }
  }
  ~ModbusUartLock() {
    if (held) {
      xSemaphoreGive(g_modbus_uart_mutex);
    }
  }
  bool acquired() const { return held; }
private:
  bool held;
};

modbus_master_config_t g_modbus_master_config = {
  .enabled = false,
  .baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE,
  .parity = MODBUS_MASTER_DEFAULT_PARITY,
  .stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS,
  .timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT,
  .inter_frame_delay = MODBUS_MASTER_DEFAULT_INTER_FRAME,
  .max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS,
  .cache_ttl_ms = 0,  // 0 = never expire
  .cache_max_entries = MB_CACHE_MAX_ENTRIES_DEFAULT,
  .queue_max_size = MB_ASYNC_QUEUE_SIZE_DEFAULT,
  .total_requests = 0,
  .successful_requests = 0,
  .timeout_errors = 0,
  .crc_errors = 0,
  .exception_errors = 0
};

/* ============================================================================
 * HARDWARE SERIAL
 * ============================================================================ */

#if !MODBUS_SINGLE_TRANSCEIVER
HardwareSerial ModbusSerial(1); // UART1 — dedicated master port (non-ES32D26)
#endif

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

void modbus_master_init() {
  // BUG-337: opret UART-mutex'en foerste (og eneste) gang — modbus_master_init()
  // kan kaldes flere gange over enhedens levetid (reconfigure ved UART-skift
  // m.m.), men mutex'en skal kun oprettes en gang.
  if (!g_modbus_uart_mutex) {
    g_modbus_uart_mutex = xSemaphoreCreateMutex();
  }

  // BUG-239 FIX: Sync runtime config from persistent config at boot
  // g_modbus_master_config is initialized with .enabled=false at compile time,
  // but g_persist_config.modbus_master contains the NVS-loaded values.
  // Without this sync, modbus_master_send_request() always returns MB_NOT_ENABLED.
  g_modbus_master_config.enabled = g_persist_config.modbus_master.enabled;
  g_modbus_master_config.baudrate = g_persist_config.modbus_master.baudrate;
  g_modbus_master_config.parity = g_persist_config.modbus_master.parity;
  g_modbus_master_config.stop_bits = g_persist_config.modbus_master.stop_bits;
  g_modbus_master_config.timeout_ms = g_persist_config.modbus_master.timeout_ms;
  g_modbus_master_config.inter_frame_delay = g_persist_config.modbus_master.inter_frame_delay;
  g_modbus_master_config.max_requests_per_cycle = g_persist_config.modbus_master.max_requests_per_cycle;
  g_modbus_master_config.cache_ttl_ms = g_persist_config.modbus_master.cache_ttl_ms;
  g_modbus_master_config.cache_max_entries = g_persist_config.modbus_master.cache_max_entries;
  g_modbus_master_config.queue_max_size = g_persist_config.modbus_master.queue_max_size;
  g_modbus_master_config.stats_since_ms = millis();

#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: shared transceiver — DIR pin already configured by uart_driver
  // Nothing to do here; uart1_init() handles UART setup
#else
  // Configure DE/RE pin (MAX485 direction control)
  pinMode(uart_get_master_dir_pin(), OUTPUT);
  digitalWrite(uart_get_master_dir_pin(), LOW); // Receive mode
#endif

  // Initialize UART if enabled
#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: DEFER UART reconfigure — GPIO1/3 shares with USB serial.
  // If we reconfigure now, USB console dies BEFORE WiFi/Telnet is ready.
  // main.cpp calls modbus_master_activate_uart() after network services start.
  // Config is synced above, so async task and ST builtins work once UART is live.
#else
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
#endif
}

void modbus_master_activate_uart() {
  // Called from main.cpp AFTER network services are started.
  // Safe to take over GPIO1/3 now — Telnet is available as fallback console.
  if (g_modbus_master_config.enabled) {
    modbus_master_reconfigure();
  }
}

void modbus_master_set_enabled(bool enabled) {
  g_modbus_master_config.enabled = enabled;

  if (enabled) {
    g_modbus_master_boot_aborted = false;  // BUG-334: RS485 aktiveres nu
    modbus_master_reconfigure();

    // BUG-340: denne funktion bruges ogsaa til at (gen)aktivere Master via
    // `set modbus-master enabled on` EFTER et rent CLI-mode-skift (`set
    // modbus mode master`) UDEN reboot, eller efter et boot der endte i
    // SLAVE/OFF-mode (fx fabriksdefault efter BUG-339, eller et afbrudt
    // RS485-boot-vindue, BUG-334). Paa alle de veje har hverken
    // modbus_master_init() (opretter g_modbus_uart_mutex) eller
    // mb_async_init() (starter baggrundstasken) noedvendigvis koert —
    // resultat: mutex'en er NULL saa ModbusUartLock fejler ALTID
    // (MB_BUS_BUSY paa selv en tom bus), og async-tasken staar for evigt
    // "STOPPED" (ST Logic's MB_*-koeer aldrig behandlet). Sikr begge findes
    // her, idempotent — rammer aldrig en allerede koerende opsaetning, saa
    // det er sikkert at kalde uanset hvordan vi naaede hertil.
    if (!g_modbus_uart_mutex) {
      g_modbus_uart_mutex = xSemaphoreCreateMutex();
    }
    if (!mb_async_get_state()->task_handle) {
      mb_async_init();
    }
  } else {
#if MODBUS_SINGLE_TRANSCEIVER
    uart1_stop();
#else
    ModbusSerial.end();
#endif
  }
}

void modbus_master_reconfigure() {
  if (!g_modbus_master_config.enabled) {
    return;
  }

#if MODBUS_SINGLE_TRANSCEIVER
  // ES32D26: reuse shared UART via uart_driver
  // BUG-315 FIX: Build full serial config from master parity/stop bits.
  // Previously uart1_init() hardcoded SERIAL_8N1, silently dropping parity/stop.
  uint32_t uart_config = SERIAL_8N1;
  if (g_modbus_master_config.parity == 1) { // Even
    uart_config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8E2 : SERIAL_8E1;
  } else if (g_modbus_master_config.parity == 2) { // Odd
    uart_config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8O2 : SERIAL_8O1;
  } else { // None
    uart_config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8N2 : SERIAL_8N1;
  }
  uart1_stop();
  uart1_init_ex(g_modbus_master_config.baudrate, uart_config);
  // DIR pin setup
  pinMode(uart_get_master_dir_pin(), OUTPUT);
  digitalWrite(uart_get_master_dir_pin(), LOW); // Receive mode
#else
  // Stop existing UART
  ModbusSerial.end();

  // Determine parity mode
  uint32_t config = SERIAL_8N1; // Default: 8 data bits, no parity, 1 stop bit

  if (g_modbus_master_config.parity == 1) { // Even parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8E2 : SERIAL_8E1;
  } else if (g_modbus_master_config.parity == 2) { // Odd parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8O2 : SERIAL_8O1;
  } else { // No parity
    config = (g_modbus_master_config.stop_bits == 2) ? SERIAL_8N2 : SERIAL_8N1;
  }

  // Start UART — resolve pins from config (0xFF=board default)
  uint8_t mu = g_persist_config.modbus_master_uart;
  uint8_t rx = (mu == 2 && g_persist_config.uart2_rx_pin != 0xFF) ? g_persist_config.uart2_rx_pin :
               (mu == 1 && g_persist_config.uart1_rx_pin != 0xFF) ? g_persist_config.uart1_rx_pin :
               MODBUS_MASTER_RX_PIN;
  uint8_t tx = (mu == 2 && g_persist_config.uart2_tx_pin != 0xFF) ? g_persist_config.uart2_tx_pin :
               (mu == 1 && g_persist_config.uart1_tx_pin != 0xFF) ? g_persist_config.uart1_tx_pin :
               MODBUS_MASTER_TX_PIN;
  ModbusSerial.begin(
    g_modbus_master_config.baudrate,
    config,
    rx,
    tx
  );

  // Flush any pending data
  ModbusSerial.flush();
  while (ModbusSerial.available()) {
    ModbusSerial.read();
  }
#endif
}

void modbus_master_reset_stats() {
  g_modbus_master_config.total_requests = 0;
  g_modbus_master_config.successful_requests = 0;
  g_modbus_master_config.timeout_errors = 0;
  g_modbus_master_config.crc_errors = 0;
  g_modbus_master_config.exception_errors = 0;
  g_modbus_bus_busy_errors = 0;
}

/* ============================================================================
 * CRC16 CALCULATION (Modbus RTU)
 * ============================================================================ */

uint16_t modbus_master_calc_crc(const uint8_t *buffer, uint8_t len) {
  uint16_t crc = 0xFFFF;

  for (uint8_t i = 0; i < len; i++) {
    crc ^= buffer[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

/* ============================================================================
 * REQUEST/RESPONSE HANDLING
 * ============================================================================ */

/**
 * @brief FEAT-149: log this Master transaction to the wire-level activity
 * log — the single chokepoint every Master read/write funnels through
 * (async queue AND direct CLI calls), so it's the truest possible "sniff
 * point" short of tapping the UART pins themselves.
 *
 * Value/count are read from the REQUEST (always available) plus the
 * RESPONSE when the transaction succeeded (the only case the response
 * bytes are known-complete and trustworthy).
 */
static void mb_log_master_activity(const uint8_t *request, uint8_t request_len,
                                    const uint8_t *response, uint8_t response_len,
                                    mb_error_code_t err) {
  if (request_len < 4) return;  // malformed request — nothing sane to log

  uint8_t slave_id = request[0];
  uint8_t fc = request[1];
  uint16_t address = ((uint16_t)request[2] << 8) | request[3];
  uint8_t count = 1;
  int32_t value = 0;

  switch (fc) {
    case 0x01: case 0x02: case 0x03: case 0x04:  // Reads
      if (request_len >= 6) count = (uint8_t)(((uint16_t)request[4] << 8) | request[5]);
      if (err == MB_OK && response_len >= 4) {
        if (fc == 0x01 || fc == 0x02) {
          value = response[3] & 0x01;  // First coil/discrete bit
        } else if (response_len >= 5) {
          value = ((int32_t)response[3] << 8) | response[4];  // First register
        }
      }
      break;
    case 0x05:  // Write Single Coil
      if (request_len >= 6) value = (((uint16_t)request[4] << 8) | request[5]) ? 1 : 0;
      break;
    case 0x06:  // Write Single Holding Register
      if (request_len >= 6) value = ((uint16_t)request[4] << 8) | request[5];
      break;
    case 0x10:  // Write Multiple Registers (FC16)
      if (request_len >= 6) count = (uint8_t)(((uint16_t)request[4] << 8) | request[5]);
      if (request_len >= 9) value = ((int32_t)request[7] << 8) | request[8];  // First register written
      break;
    default:
      break;
  }

  mb_activity_log_add(MB_ACTIVITY_ROLE_MASTER, g_mb_activity_current_source,
                       slave_id, fc, address, count, value, (int16_t)err);
}

mb_error_code_t modbus_master_send_request(
  const uint8_t *request,
  uint8_t request_len,
  uint8_t *response,
  uint8_t *response_len,
  uint8_t max_response_len
) {
  if (!g_modbus_master_config.enabled) {
    mb_log_master_activity(request, request_len, NULL, 0, MB_NOT_ENABLED);
    return MB_NOT_ENABLED;
  }

  // BUG-337: seriali sér adgang til den fysiske UART/RS485-bus. Holdes for
  // hele funktionens levetid (RAII — frigives automatisk paa ethvert
  // return-punkt nedenfor). Se deklarationen af ModbusUartLock foroven for
  // hvorfor dette er noedvendigt: CLI's mb read/write/scan og den
  // asynkrone Modbus Master-task kalder begge denne funktion, fra hver
  // sin FreeRTOS-task, helt uden koordinering udenom denne laas.
  ModbusUartLock uart_lock;
  if (!uart_lock.acquired()) {
    // BUG-338: egen fejlkode (ikke MB_TIMEOUT) — dette betyder vi ALDRIG
    // fik sendt noget paa bussen (bussen var optaget), til forskel fra et
    // reelt MB_TIMEOUT hvor forespoergslen blev sendt, men ingen svarede.
    // Uden denne skelnen saa en `mb scan` der konsekvent tabte laase-koeb
    // mod den asynkrone task identisk ud som en scanning af en tom bus —
    // observeret som falsk "0 slaver fundet" paa en bus med 2 kendte enheder.
    g_modbus_bus_busy_errors++;
    mb_log_master_activity(request, request_len, NULL, 0, MB_BUS_BUSY);
    return MB_BUS_BUSY;  // Bussen var optaget af en anden transaktion i >2s
  }

  // Flush RX buffer
#if MODBUS_SINGLE_TRANSCEIVER
  uart1_flush_rx();
#else
  while (ModbusSerial.available()) {
    ModbusSerial.read();
  }
#endif

  // Set DE/RE to transmit mode
  digitalWrite(uart_get_master_dir_pin(), HIGH);
  delayMicroseconds(50); // Small delay for transceiver switching

  // Send request
#if MODBUS_SINGLE_TRANSCEIVER
  uart1_write_buffer(request, request_len);
  uart1_flush_tx();
#else
  ModbusSerial.write(request, request_len);
  ModbusSerial.flush(); // Wait for TX complete
#endif

  // BUG-316 FIX: Wait long enough for last byte to fully exit the TX shift
  // register BEFORE releasing DE. HardwareSerial::flush() semantics vary
  // across Arduino ESP32 core versions — older versions only wait for FIFO
  // empty, not shift register complete. A fixed 50µs was far too short at
  // 9600 baud (1 byte = ~1040µs). Calculate one full char-time (11 bits
  // worst-case with parity/2-stop-bits) plus 100µs margin.
  uint32_t byte_us = (11UL * 1000000UL) / g_modbus_master_config.baudrate;
  delayMicroseconds(byte_us + 100);
  digitalWrite(uart_get_master_dir_pin(), LOW);

  // Wait for response with timeout
  // Two-phase timeout: full timeout_ms for first byte, then shorter inter-char timeout
  uint32_t start_time = millis();
  uint8_t bytes_received = 0;
  bool timeout = false;
  // Inter-character timeout: T3.5 at baudrate (min 2ms, max 20ms)
  uint32_t interchar_ms = (uint32_t)(38500UL / g_modbus_master_config.baudrate);
  if (interchar_ms < 2) interchar_ms = 2;
  if (interchar_ms > 20) interchar_ms = 20;

  while (bytes_received < max_response_len) {
    // Check timeout: use full timeout for first byte, inter-char after that
    uint32_t active_timeout = (bytes_received == 0) ? g_modbus_master_config.timeout_ms : interchar_ms;
    if (millis() - start_time > active_timeout) {
      timeout = true;
      break;
    }

    // Check for available data
#if MODBUS_SINGLE_TRANSCEIVER
    if (uart1_available()) {
      int b = uart1_read();
      if (b >= 0) {
        response[bytes_received++] = (uint8_t)b;
        start_time = millis();
      }
#else
    if (ModbusSerial.available()) {
      response[bytes_received++] = ModbusSerial.read();
      start_time = millis(); // Reset for inter-character timeout
#endif

      // Check if we have minimum response (slave_id + function + data + CRC)
      if (bytes_received >= 5) {
        // For exceptions: slave_id + (function | 0x80) + exception_code + CRC (5 bytes)
        // For normal: depends on function
        uint8_t function_code = response[1];

        if (function_code & 0x80) {
          // BUG-149 FIX: Exception response is always exactly 5 bytes
          // (slave_id + function + exception_code + CRC)
          // We already checked bytes_received >= 5 in outer condition, so break immediately
          break; // Complete exception response
        } else {
          // Normal response - check expected length
          if (function_code == 0x01 || function_code == 0x02) {
            // Read Coils/Inputs: slave_id + fc + byte_count + data + CRC
            if (bytes_received >= 3) {
              uint8_t byte_count = response[2];
              if (bytes_received >= (uint8_t)(3 + byte_count + 2)) {
                break; // Complete response
              }
            }
          } else if (function_code == 0x03 || function_code == 0x04) {
            // Read Registers: slave_id + fc + byte_count + data + CRC
            if (bytes_received >= 3) {
              uint8_t byte_count = response[2];
              if (bytes_received >= (uint8_t)(3 + byte_count + 2)) {
                break; // Complete response
              }
            }
          } else if (function_code == 0x05 || function_code == 0x06) {
            // Write Single: slave_id + fc + address(2) + value(2) + CRC(2) = 8 bytes
            if (bytes_received >= 8) {
              break; // Complete response
            }
          } else if (function_code == 0x10) {
            // FC16 Write Multiple: slave_id + fc + address(2) + count(2) + CRC(2) = 8 bytes
            if (bytes_received >= 8) {
              break; // Complete response
            }
          }
        }
      }
    } else if (bytes_received == 0) {
      // BUG-341: vTaskDelay(1) (BUG-336b) and Core-0-pinning httpd/SSE
      // (BUG-336c) were BOTH empirically confirmed insufficient — dashboard
      // stayed unreachable for the whole `mb scan`, even AFTER BUG-338/340
      // removed all UART-mutex contention (async task fully paused during
      // the scan, so it is provably not lock contention). That isolates the
      // cause to raw CPU-time monopolization: a non-responding slave sits in
      // THIS loop for the full timeout_ms (500-1000ms default), waking up
      // every single tick (~1ms) to check uart1_available() and immediately
      // going back to sleep. Each wake costs a real context switch, and the
      // resulting 1ms windows are too short and too frequent for httpd/WiFi/
      // lwIP to make USABLE progress on a request (TCP handshake, header
      // parse, response send) even when they are technically "free" to run
      // between them — a scheduling-thrash pattern, not starvation. Fix:
      // poll far less often instead of yielding more cleverly. Modbus RTU
      // has no sub-millisecond requirement for noticing the FIRST byte of a
      // response — real slaves' turnaround time is commonly single-digit to
      // low-tens of ms, and the UART hardware FIFO holds any byte that
      // arrives regardless of how promptly software checks for it, so nothing
      // is lost by checking less often. 10ms cuts the number of wake-ups
      // (and thus context switches) by ~10x versus 1ms, giving httpd/lwIP
      // windows an order of magnitude longer between this task's brief
      // interruptions. Still confined to "no byte received yet" — inter-
      // character timing (interchar_ms, the bytes_received>0 path) is
      // completely untouched, so mid-frame RTU timing is unaffected.
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  *response_len = bytes_received;

  // Extract slave_id and address from request for error tracking
  uint8_t req_slave_id = request[0];
  uint16_t req_address = (request_len >= 4) ? ((uint16_t)request[2] << 8 | request[3]) : 0;

  // Check timeout
  if (timeout || bytes_received == 0) {
    g_modbus_master_config.timeout_errors++;
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_TIMEOUT;
    mb_log_master_activity(request, request_len, response, bytes_received, MB_TIMEOUT);
    return MB_TIMEOUT;
  }

  // Verify CRC
  if (bytes_received >= 3) {
    uint16_t received_crc = (response[bytes_received - 1] << 8) | response[bytes_received - 2];
    uint16_t calculated_crc = modbus_master_calc_crc(response, bytes_received - 2);

    if (received_crc != calculated_crc) {
      g_modbus_master_config.crc_errors++;
      g_modbus_master_config.last_error_slave_id = req_slave_id;
      g_modbus_master_config.last_error_address = req_address;
      g_modbus_master_config.last_error_type = MB_CRC_ERROR;
      mb_log_master_activity(request, request_len, response, bytes_received, MB_CRC_ERROR);
      return MB_CRC_ERROR;
    }
  } else {
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_CRC_ERROR;
    mb_log_master_activity(request, request_len, response, bytes_received, MB_CRC_ERROR);
    return MB_CRC_ERROR;
  }

  // Check for Modbus exception
  if (response[1] & 0x80) {
    g_modbus_master_config.exception_errors++;
    g_modbus_master_config.last_error_slave_id = req_slave_id;
    g_modbus_master_config.last_error_address = req_address;
    g_modbus_master_config.last_error_type = MB_EXCEPTION;
    mb_log_master_activity(request, request_len, response, bytes_received, MB_EXCEPTION);
    return MB_EXCEPTION;
  }

  g_modbus_master_config.successful_requests++;
  mb_log_master_activity(request, request_len, response, bytes_received, MB_OK);
  return MB_OK;
}

/* ============================================================================
 * MODBUS FUNCTIONS
 * ============================================================================ */

mb_error_code_t modbus_master_read_coil(uint8_t slave_id, uint16_t address, bool *result) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: slave_id + FC01 + address(2) + quantity(2) + CRC(2)
  request[0] = slave_id;
  request[1] = 0x01; // FC01: Read Coils
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00; // Quantity high byte (1 coil)
  request[5] = 0x01; // Quantity low byte
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = false;
    return err;
  }

  // Parse response: slave_id + FC01 + byte_count + data + CRC
  if (response_len >= 5 && response[1] == 0x01) {
    *result = (response[3] & 0x01) != 0;
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_input(uint8_t slave_id, uint16_t address, bool *result) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC02 (Read Discrete Inputs)
  request[0] = slave_id;
  request[1] = 0x02;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = false;
    return err;
  }

  if (response_len >= 5 && response[1] == 0x02) {
    *result = (response[3] & 0x01) != 0;
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_holding(uint8_t slave_id, uint16_t address, uint16_t *result) {
  uint8_t request[8];
  uint8_t response[9];
  uint8_t response_len;

  // Build request: FC03 (Read Holding Registers)
  request[0] = slave_id;
  request[1] = 0x03;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01; // Read 1 register
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = 0;
    return err;
  }

  // Parse response: slave_id + FC03 + byte_count(1) + data(2) + CRC(2)
  if (response_len >= 7 && response[1] == 0x03) {
    *result = (response[3] << 8) | response[4];
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_read_input_register(uint8_t slave_id, uint16_t address, uint16_t *result) {
  uint8_t request[8];
  uint8_t response[9];
  uint8_t response_len;

  // Build request: FC04 (Read Input Registers)
  request[0] = slave_id;
  request[1] = 0x04;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = 0x01;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    *result = 0;
    return err;
  }

  if (response_len >= 7 && response[1] == 0x04) {
    *result = (response[3] << 8) | response[4];
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_write_coil(uint8_t slave_id, uint16_t address, bool value) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC05 (Write Single Coil)
  request[0] = slave_id;
  request[1] = 0x05;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = value ? 0xFF : 0x00; // 0xFF00 = ON, 0x0000 = OFF
  request[5] = 0x00;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  return err;
}

mb_error_code_t modbus_master_write_holding(uint8_t slave_id, uint16_t address, uint16_t value) {
  uint8_t request[8];
  uint8_t response[8];
  uint8_t response_len;

  // Build request: FC06 (Write Single Register)
  request[0] = slave_id;
  request[1] = 0x06;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = (value >> 8) & 0xFF;
  request[5] = value & 0xFF;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  return err;
}

mb_error_code_t modbus_master_read_holdings(uint8_t slave_id, uint16_t address, uint8_t count, uint16_t *results) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  uint8_t request[8];
  // Response: slave(1) + FC(1) + byte_count(1) + data(count*2) + CRC(2)
  uint8_t response[5 + 16 * 2];  // Max 16 registers
  uint8_t response_len;

  // Build request: FC03 (Read Holding Registers) with count
  request[0] = slave_id;
  request[1] = 0x03;
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = count;
  uint16_t crc = modbus_master_calc_crc(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, 8, response, &response_len, sizeof(response));
  if (err != MB_OK) {
    memset(results, 0, count * sizeof(uint16_t));
    return err;
  }

  // Parse response: slave_id + FC03 + byte_count + data[count*2] + CRC
  uint8_t expected_bytes = count * 2;
  if (response_len >= (uint8_t)(5 + expected_bytes) && response[1] == 0x03 && response[2] == expected_bytes) {
    for (uint8_t i = 0; i < count; i++) {
      results[i] = (response[3 + i * 2] << 8) | response[4 + i * 2];
    }
    return MB_OK;
  }

  return MB_CRC_ERROR;
}

mb_error_code_t modbus_master_write_holdings(uint8_t slave_id, uint16_t address, uint8_t count, const uint16_t *values) {
  if (count == 0 || count > 16) return MB_INVALID_ADDRESS;

  // Request: slave(1) + FC16(1) + addr(2) + count(2) + byte_count(1) + data(count*2) + CRC(2)
  uint8_t request[9 + 16 * 2];  // Max 16 registers
  uint8_t response[8];
  uint8_t response_len;

  uint8_t byte_count = count * 2;

  // Build request: FC16 (Write Multiple Registers)
  request[0] = slave_id;
  request[1] = 0x10;  // FC16
  request[2] = (address >> 8) & 0xFF;
  request[3] = address & 0xFF;
  request[4] = 0x00;
  request[5] = count;
  request[6] = byte_count;
  for (uint8_t i = 0; i < count; i++) {
    request[7 + i * 2] = (values[i] >> 8) & 0xFF;
    request[8 + i * 2] = values[i] & 0xFF;
  }
  uint8_t req_len = 7 + byte_count;
  uint16_t crc = modbus_master_calc_crc(request, req_len);
  request[req_len] = crc & 0xFF;
  request[req_len + 1] = (crc >> 8) & 0xFF;

  g_modbus_master_config.total_requests++;

  mb_error_code_t err = modbus_master_send_request(request, req_len + 2, response, &response_len, sizeof(response));
  return err;
}
