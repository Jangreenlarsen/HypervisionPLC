/**
 * @file modbus_server.cpp
 * @brief Modbus server main state machine implementation (LAYER 3)
 *
 * Main orchestration: Idle → RX → Process → TX → Idle
 */

#include "modbus_server.h"
#include "modbus_rx.h"
#include "modbus_tx.h"
#include "modbus_fc_dispatch.h"
#include "modbus_frame.h"
#include "constants.h"
#include "debug.h"
#include "mb_activity_log.h"
#include "system_log.h"  // FEAT-089
#include "registers.h"   // FEAT-089: old-vaerdi-opslag foer dispatch
#include <Arduino.h>

/* ============================================================================
 * STATIC STATE
 * ============================================================================ */

static modbus_server_state_t server_state = MODBUS_STATE_IDLE;
static uint8_t slave_id = SLAVE_ID;
static ModbusFrame request_frame;
static ModbusFrame response_frame;

/* ============================================================================
 * MODBUS SERVER FUNCTIONS
 * ============================================================================ */

/**
 * @brief FEAT-149: log an incoming Slave-role transaction to the wire-level
 * activity log, right where the request has just been fully processed —
 * the Slave-side equivalent of mb_log_master_activity() in modbus_master.cpp.
 * Always MB_SRC_EXTERNAL: by definition a Slave-role request always
 * originates from an external master on the bus.
 */
static void mb_log_slave_activity(const ModbusFrame *req, const ModbusFrame *resp, bool success) {
  uint8_t fc = req->function_code;
  uint16_t address = (req->length >= 4) ? (((uint16_t)req->data[0] << 8) | req->data[1]) : 0;
  uint8_t count = 1;
  int32_t value = 0;
  int16_t error = 0;

  if (!success) {
    // resp was built by modbus_serialize_error_response(): data[0] = exception code
    error = (resp->length >= 3) ? (int16_t)resp->data[0] : -1;
  }

  switch (fc) {
    case FC_READ_COILS: case FC_READ_DISCRETE_INPUTS:
    case FC_READ_HOLDING_REGS: case FC_READ_INPUT_REGS:
      if (req->length >= 6) count = (uint8_t)(((uint16_t)req->data[2] << 8) | req->data[3]);
      if (success && resp->length >= 4) {
        if (fc == FC_READ_COILS || fc == FC_READ_DISCRETE_INPUTS) {
          value = resp->data[1] & 0x01;  // First coil/discrete bit
        } else if (resp->length >= 5) {
          value = ((int32_t)resp->data[1] << 8) | resp->data[2];  // First register
        }
      }
      break;
    case FC_WRITE_SINGLE_COIL:
      if (req->length >= 6) value = (((uint16_t)req->data[2] << 8) | req->data[3]) ? 1 : 0;
      break;
    case FC_WRITE_SINGLE_REG:
      if (req->length >= 6) value = ((uint16_t)req->data[2] << 8) | req->data[3];
      break;
    case FC_WRITE_MULTIPLE_COILS: case FC_WRITE_MULTIPLE_REGS:
      if (req->length >= 6) count = (uint8_t)(((uint16_t)req->data[2] << 8) | req->data[3]);
      if (fc == FC_WRITE_MULTIPLE_REGS && req->length >= 9) {
        value = ((int32_t)req->data[5] << 8) | req->data[6];  // First register written
      }
      break;
    default:
      break;
  }

  mb_activity_log_add(MB_ACTIVITY_ROLE_SLAVE, MB_SRC_EXTERNAL, req->slave_id, fc, address, count, value, error);
}

/**
 * @brief FEAT-089: log en registerandring udloest af en ekstern Modbus
 * master (FC05/06/0F/10). "Hvem" er her blot "ekstern master" — RTU-bussen
 * har ingen finere identitet (ingen klient-IP/brugernavn som ved REST).
 *
 * Kaldes fra MODBUS_STATE_PROCESS lige EFTER dispatch — men gammel vaerdi
 * skal laeses FOER dispatch (ellers er registret allerede overskrevet), saa
 * kaldestedet laeser den separat og sender den ind som parameter.
 */
static void system_log_modbus_slave_write(const ModbusFrame *req, bool success, uint16_t old_val) {
  if (!success) return;  // Kun log reelt gennemfoerte skrivninger

  uint8_t fc = req->function_code;
  if (fc != FC_WRITE_SINGLE_COIL && fc != FC_WRITE_SINGLE_REG &&
      fc != FC_WRITE_MULTIPLE_COILS && fc != FC_WRITE_MULTIPLE_REGS) {
    return;  // Kun skrive-FC'er
  }
  if (req->length < 4) return;

  uint16_t address = ((uint16_t)req->data[0] << 8) | req->data[1];
  bool is_coil = (fc == FC_WRITE_SINGLE_COIL || fc == FC_WRITE_MULTIPLE_COILS);

  // Ny vaerdi ved FOERSTE register/coil (samme "kun foerste" pragmatiske
  // forenkling som REST bulk-write-hooket — se der for begrundelse)
  int32_t new_val;
  if (fc == FC_WRITE_SINGLE_COIL) {
    new_val = (req->length >= 4 && (((uint16_t)req->data[2] << 8) | req->data[3])) ? 1 : 0;
  } else if (fc == FC_WRITE_SINGLE_REG) {
    new_val = (req->length >= 4) ? (((uint16_t)req->data[2] << 8) | req->data[3]) : 0;
  } else {
    new_val = is_coil ? registers_get_coil(address) : registers_get_holding_register(address);
  }

  if (new_val == (int32_t)old_val) return;  // Ingen reel aendring

  system_log_add_reg_change((uint8_t)SYSLOG_SRC_MODBUS_SLAVE, NULL, NULL,
                             address, is_coil, (int32_t)old_val, new_val);
}

void modbus_server_init(uint8_t sid) {
  slave_id = sid;
  server_state = MODBUS_STATE_IDLE;

  // Initialize subsystems
  modbus_rx_init();
  modbus_tx_init();

  debug_print("Modbus server initialized (Slave ID: ");
  debug_print_uint(slave_id);
  debug_println(")");
}

void modbus_server_loop(void) {
  switch (server_state) {
    case MODBUS_STATE_IDLE:
      // Reset RX state and wait for request
      modbus_rx_reset();
      server_state = MODBUS_STATE_RX;
      break;

    case MODBUS_STATE_RX:
      // Process RX
      {
        modbus_rx_state_t rx_state = modbus_rx_process(&request_frame);

        if (rx_state == MODBUS_RX_COMPLETE) {
          // Frame received successfully
          // Check if frame is for this slave (or broadcast 0)
          if (request_frame.slave_id == slave_id || request_frame.slave_id == 0) {
            debug_print("Modbus request received: FC=0x");
            debug_print_uint(request_frame.function_code);
            debug_newline();
            server_state = MODBUS_STATE_PROCESS;
          } else {
            // Not for this slave - ignore and return to idle
            debug_print("Modbus request for different slave (ID: ");
            debug_print_uint(request_frame.slave_id);
            debug_println("), ignoring");
            server_state = MODBUS_STATE_IDLE;
          }
        } else if (rx_state == MODBUS_RX_ERROR) {
          // RX error - return to idle
          debug_println("Modbus RX error, returning to idle");
          server_state = MODBUS_STATE_IDLE;
        }
        // Otherwise stay in RX state
      }
      break;

    case MODBUS_STATE_PROCESS:
      // Process request and generate response
      {
        // FEAT-089: gammel vaerdi skal laeses FOER dispatch — bagefter er
        // registret allerede overskrevet. Kun relevant for skrive-FC'er.
        uint16_t syslog_old_val = 0;
        {
          uint8_t fc0 = request_frame.function_code;
          if ((fc0 == FC_WRITE_SINGLE_COIL || fc0 == FC_WRITE_SINGLE_REG ||
               fc0 == FC_WRITE_MULTIPLE_COILS || fc0 == FC_WRITE_MULTIPLE_REGS) &&
              request_frame.length >= 4) {
            uint16_t addr0 = ((uint16_t)request_frame.data[0] << 8) | request_frame.data[1];
            bool is_coil0 = (fc0 == FC_WRITE_SINGLE_COIL || fc0 == FC_WRITE_MULTIPLE_COILS);
            syslog_old_val = is_coil0 ? registers_get_coil(addr0) : registers_get_holding_register(addr0);
          }
        }

        bool success = modbus_dispatch_function_code(&request_frame, &response_frame);
        mb_log_slave_activity(&request_frame, &response_frame, success);
        system_log_modbus_slave_write(&request_frame, success, syslog_old_val);

        if (success) {
          // Broadcast requests (slave_id == 0) should NOT generate responses
          if (request_frame.slave_id == 0) {
            debug_println("Broadcast request - no response sent");
            server_state = MODBUS_STATE_IDLE;
          } else {
            debug_println("Processing complete, sending response");
            server_state = MODBUS_STATE_TX;
          }
        } else {
          // Error occurred - response_frame contains error response
          // Broadcast requests should NOT send error responses
          if (request_frame.slave_id == 0) {
            debug_println("Broadcast request error - no response sent");
            server_state = MODBUS_STATE_IDLE;
          } else {
            debug_println("Processing error, sending error response");
            server_state = MODBUS_STATE_TX;
          }
        }
      }
      break;

    case MODBUS_STATE_TX:
      // Transmit response
      {
        bool success = modbus_tx_send_frame(&response_frame);

        if (success) {
          debug_println("Response transmitted");
        } else {
          debug_println("TX error");
        }

        server_state = MODBUS_STATE_IDLE;
      }
      break;

    case MODBUS_STATE_ERROR:
      // Error state - reset to idle
      debug_println("Modbus server error, resetting to idle");
      server_state = MODBUS_STATE_IDLE;
      break;
  }
}

modbus_server_state_t modbus_server_get_state(void) {
  return server_state;
}

void modbus_server_set_slave_id(uint8_t sid) {
  if (sid >= 1 && sid <= 247) {
    slave_id = sid;
    debug_print("Modbus slave ID changed to: ");
    debug_print_uint(slave_id);
    debug_newline();
  } else {
    debug_println("ERROR: Invalid slave ID (must be 1-247)");
  }
}

uint8_t modbus_server_get_slave_id(void) {
  return slave_id;
}

