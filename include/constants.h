/**
 * @file constants.h
 * @brief Central location for ALL constants and enums
 *
 * This file should be the ONLY place where constants and enums are defined.
 * Prevents duplicate definitions across multiple files.
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <stdint.h>

/* ============================================================================
 * MODBUS CONFIGURATION
 * ============================================================================ */

#define SLAVE_ID            1           // Default Modbus slave address
#define BAUDRATE            115200      // Default Modbus RTU baudrate
#define MODBUS_FRAME_MAX    256         // Max Modbus frame size
#define MODBUS_TIMEOUT_MS   3500        // Inter-character timeout (ms)

/* Modbus Function Codes */
#define FC_READ_COILS           0x01
#define FC_READ_DISCRETE_INPUTS 0x02
#define FC_READ_HOLDING_REGS    0x03
#define FC_READ_INPUT_REGS      0x04
#define FC_WRITE_SINGLE_COIL    0x05
#define FC_WRITE_SINGLE_REG     0x06
#define FC_WRITE_MULTIPLE_COILS 0x0F
#define FC_WRITE_MULTIPLE_REGS  0x10

/* ============================================================================
 * REGISTER/COIL CONFIGURATION
 * ============================================================================ */

#define HOLDING_REGS_SIZE   256         // Number of holding registers (0-255)
#define INPUT_REGS_SIZE     256         // Number of input registers (0-255)
#define COILS_SIZE          32          // Coil bits (0-255 packed)
#define DISCRETE_INPUTS_SIZE 32         // Discrete input bits (0-255 packed)

/* ============================================================================
 * ST LOGIC REGISTER MAPPING (Input/Holding Registers 200+)
 * ============================================================================ */

// INPUT REGISTERS (Read-only status)
#define ST_LOGIC_STATUS_REG_BASE        200  // Logic1-4 Status (200-203)
#define ST_LOGIC_EXEC_COUNT_REG_BASE    204  // Logic1-4 Execution Count (204-207)
#define ST_LOGIC_ERROR_COUNT_REG_BASE   208  // Logic1-4 Error Count (208-211)
#define ST_LOGIC_ERROR_CODE_REG_BASE    212  // Logic1-4 Last Error Code (212-215)
#define ST_LOGIC_VAR_COUNT_REG_BASE     216  // Logic1-4 Variable Count (216-219)
#define ST_LOGIC_VAR_VALUES_REG_BASE    220  // Logic1-4 Variable Values (220-251)

// PERFORMANCE STATISTICS (v4.1.0) - Input Registers 252-291
#define ST_LOGIC_MIN_EXEC_TIME_REG_BASE 252  // Logic1-4 Min Execution Time µs, 32-bit (252-259, 2 regs each)
#define ST_LOGIC_MAX_EXEC_TIME_REG_BASE 260  // Logic1-4 Max Execution Time µs, 32-bit (260-267, 2 regs each)
#define ST_LOGIC_AVG_EXEC_TIME_REG_BASE 268  // Logic1-4 Avg Execution Time µs, 32-bit (268-275, 2 regs each)
#define ST_LOGIC_OVERRUN_COUNT_REG_BASE 276  // Logic1-4 Overrun Count, 32-bit (276-283, 2 regs each)

// GLOBAL CYCLE STATISTICS (v4.1.0) - Input Registers 284-293
#define ST_LOGIC_CYCLE_MIN_REG          284  // Global cycle min time ms, 32-bit (284-285)
#define ST_LOGIC_CYCLE_MAX_REG          286  // Global cycle max time ms, 32-bit (286-287)
#define ST_LOGIC_CYCLE_OVERRUN_REG      288  // Global cycle overrun count, 32-bit (288-289)
#define ST_LOGIC_TOTAL_CYCLES_REG       290  // Total cycles executed, 32-bit (290-291)
#define ST_LOGIC_EXEC_INTERVAL_RO_REG   292  // Execution interval ms (read-only copy), 32-bit (292-293)

// HOLDING REGISTERS (Read/Write control)
#define ST_LOGIC_CONTROL_REG_BASE       200  // Logic1-4 Control (200-203)
#define ST_LOGIC_VAR_INPUT_REG_BASE     204  // Logic1-4 Variable Input (204-235)
#define ST_LOGIC_EXEC_INTERVAL_RW_REG   236  // Execution interval ms (read-write), 32-bit (236-237)

// Status Register Bit Definitions
#define ST_LOGIC_STATUS_ENABLED         0x0001  // Bit 0: Program enabled
#define ST_LOGIC_STATUS_COMPILED        0x0002  // Bit 1: Program compiled
#define ST_LOGIC_STATUS_RUNNING         0x0004  // Bit 2: Currently executing
#define ST_LOGIC_STATUS_ERROR           0x0008  // Bit 3: Has error

// Control Register Bit Definitions
#define ST_LOGIC_CONTROL_ENABLE         0x0001  // Bit 0: Enable/disable
#define ST_LOGIC_CONTROL_START          0x0002  // Bit 1: Start/stop
#define ST_LOGIC_CONTROL_RESET_ERROR    0x0004  // Bit 2: Reset error flag

/* ============================================================================
 * ST LOGIC PROGRAM LIMITS
 * ============================================================================ */

#define ST_LOGIC_MAX_PROGRAMS  4  // Max ST Logic programs (1-4, affects RAM: ~13KB per program)

/* FEAT-003: User-defined function limits */
#define ST_MAX_USER_FUNCTIONS     16    // Max user-defined functions per program
#define ST_MAX_FUNCTION_PARAMS    8     // Max parameters per function
#define ST_MAX_FUNCTION_LOCALS    16    // Max local variables per function
#define ST_MAX_CALL_DEPTH         8     // Max nested function calls (recursion limit)
#define ST_MAX_TOTAL_FUNCTIONS    32    // User-defined (builtins resolved via func_id, not registry)

/* ============================================================================
 * MODULE ENABLE/DISABLE FLAGS (v6.2.0+)
 * ============================================================================ */

#define MODULE_FLAG_COUNTERS_DISABLED   0x01  // Bit 0: Counters disabled
#define MODULE_FLAG_TIMERS_DISABLED     0x02  // Bit 1: Timers disabled
#define MODULE_FLAG_ST_LOGIC_DISABLED   0x04  // Bit 2: ST Logic disabled

/* ============================================================================
 * COUNTER CONFIGURATION
 * ============================================================================ */

#define COUNTER_COUNT       4           // 4 counters maximum
#define COUNTER_VALUE_MAX   0xFFFFFFFF  // 32-bit max

typedef enum {
  COUNTER_MODE_DISABLED = 0,
  COUNTER_MODE_ENABLED = 1
} CounterModeEnable;

typedef enum {
  COUNTER_EDGE_RISING = 0,
  COUNTER_EDGE_FALLING = 1,
  COUNTER_EDGE_BOTH = 2
} CounterEdgeType;

typedef enum {
  COUNTER_HW_SW = 0,           // Software polling
  COUNTER_HW_SW_ISR = 1,       // Software ISR (GPIO interrupt)
  COUNTER_HW_PCNT = 2          // Hardware PCNT (ESP32 Timer5-equivalent)
} CounterHWMode;

typedef enum {
  COUNTER_DIR_UP = 0,
  COUNTER_DIR_DOWN = 1
} CounterDirection;

#define COUNTER_PRESCALER_VALUES {1, 4, 8, 16, 64, 256, 1024}  // Supported prescalers

/* ============================================================================
 * TIMER CONFIGURATION
 * ============================================================================ */

#define TIMER_COUNT         4           // 4 timers maximum

typedef enum {
  TIMER_MODE_DISABLED = 0,
  TIMER_MODE_1_ONESHOT = 1,
  TIMER_MODE_2_MONOSTABLE = 2,
  TIMER_MODE_3_ASTABLE = 3,
  TIMER_MODE_4_INPUT_TRIGGERED = 4
} TimerMode;

/* ============================================================================
 * MODBUS VALUE TYPES (for multi-register support)
 * ============================================================================ */

typedef enum {
  MODBUS_TYPE_UINT = 0,     // 16-bit unsigned (0-65535)
  MODBUS_TYPE_INT = 1,      // 16-bit signed (-32768 to 32767)
  MODBUS_TYPE_DINT = 2,     // 32-bit signed (2 registers)
  MODBUS_TYPE_DWORD = 3,    // 32-bit unsigned (2 registers)
  MODBUS_TYPE_REAL = 4      // 32-bit IEEE-754 float (2 registers)
} ModbusValueType;

/* ============================================================================
 * DYNAMIC REGISTER/COIL CONFIGURATION
 * ============================================================================ */

#define MAX_DYNAMIC_REGS    16          // Max DYNAMIC register mappings
#define MAX_DYNAMIC_COILS   16          // Max DYNAMIC coil mappings

typedef enum {
  DYNAMIC_SOURCE_COUNTER = 0,
  DYNAMIC_SOURCE_TIMER = 1
} DynamicSourceType;

// Counter functions
typedef enum {
  COUNTER_FUNC_INDEX = 0,       // Scaled value (index-reg)
  COUNTER_FUNC_RAW = 1,         // Prescaled value (raw-reg)
  COUNTER_FUNC_FREQ = 2,        // Frequency in Hz (freq-reg)
  COUNTER_FUNC_OVERFLOW = 3,    // Overflow flag (overload-reg)
  COUNTER_FUNC_CTRL = 4         // Control register (ctrl-reg)
} CounterFunction;

// Timer functions
typedef enum {
  TIMER_FUNC_OUTPUT = 0          // Output state (phase output, astable state, etc)
} TimerFunction;

/* ============================================================================
 * VARIABLE MAPPING CONFIGURATION (UNIFIED GPIO + ST VARIABLES)
 * ============================================================================ */

typedef enum {
  MAPPING_SOURCE_GPIO = 0,       // Map GPIO pin
  MAPPING_SOURCE_ST_VAR = 1      // Map ST Logic variable
} VariableMappingSourceType;

/* ============================================================================
 * EEPROM / NVS CONFIGURATION
 * ============================================================================ */

#define CONFIG_SCHEMA_VERSION   19      // Current config schema version (v7.9.7.2: cache/queue size)
// NOTE: v7.9.7.3 ændrer kun platformio.ini (PSRAM enable på ES32D26/WROVER) — ingen schema-ændring.

/* ============================================================================
 * RBAC CONSTANTS (v7.6.2)
 * ============================================================================ */
#define RBAC_MAX_USERS          8       // Max user accounts
#define RBAC_USERNAME_MAX       24      // Max username length (incl. null)
#define RBAC_PASSWORD_MAX       32      // Max password length (incl. null)

// Role bitmask — controls which interfaces a user can access
#define ROLE_API        0x01    // REST API endpoints (/api/*)
#define ROLE_CLI        0x02    // CLI commands (Serial, Telnet, Web CLI)
#define ROLE_EDITOR     0x04    // ST Logic Editor (/editor)
#define ROLE_MONITOR    0x08    // Dashboard/Monitor (/)
#define ROLE_ALL        0x0F    // All roles

// Privilege levels
#define PRIV_READ       0x01    // Read-only (show, GET)
#define PRIV_WRITE      0x02    // Write (set, POST, DELETE)
#define PRIV_RW         0x03    // Read + Write
#define CONFIG_CRC_SEED         0xFFFF  // CRC16 initial value

/* ============================================================================
 * CLI CONFIGURATION
 * ============================================================================ */

#define CLI_BUFFER_SIZE     256         // CLI command buffer
#define CLI_HISTORY_SIZE    10          // Command history buffer size
#define CLI_TOKEN_MAX       20          // Max tokens per command

/* ============================================================================
 * HARDWARE PINS - Board-specifik konfiguration
 * Vælg board-variant via build_flags i platformio.ini
 * ============================================================================ */

// Waveshare S3-ETH har W5500 onboard — auto-enable
#if defined(BOARD_WAVESHARE_S3_ETH) && !defined(ETHERNET_W5500_ENABLED)
  #define ETHERNET_W5500_ENABLED
#endif

#if defined(BOARD_WAVESHARE_S3_ETH)
  // Waveshare ESP32-S3-ETH (W5500 onboard via SPI)
  // Docs: https://www.waveshare.com/wiki/ESP32-S3-ETH
  #define PIN_LED             2
  #define PIN_UART1_RX        4
  #define PIN_UART1_TX        5
  #define PIN_RS485_DIR       15
  #define PIN_INT1            16
  #define PIN_INT2            17
  #define PIN_INT3            18
  #define PIN_INT4            19
  #define PIN_I2C_SDA         21
  #define PIN_I2C_SCL         22
  // W5500 SPI (onboard, fixed pins)
  #define PIN_SPI_MISO        12
  #define PIN_SPI_MOSI        11
  #define PIN_SPI_CLK         13
  #define PIN_SPI_CS          14
  #define PIN_W5500_INT       10
  #define PIN_W5500_RST       9
  #define W5500_SPI_HOST      SPI2_HOST
  #define W5500_SPI_CLOCK_HZ  (20 * 1000 * 1000)  // 20 MHz (onboard, korte spor)

#elif defined(BOARD_ESP32_38PIN)
  // ESP32-WROOM-32 38-pin DevKit
  #define PIN_LED             2
  #define PIN_UART1_RX        4
  #define PIN_UART1_TX        5
  #define PIN_RS485_DIR       15
  #define PIN_INT1            16
  #define PIN_INT2            17
  #define PIN_INT3            18
  #define PIN_INT4            19
  #define PIN_I2C_SDA         21
  #define PIN_I2C_SCL         22
  // W5500 SPI (ekstern modul via HSPI)
  #define PIN_SPI_MISO        12
  #define PIN_SPI_MOSI        13
  #define PIN_SPI_CLK         14
  #define PIN_SPI_CS          23
  #define PIN_W5500_INT       34
  #define PIN_W5500_RST       33
  #define W5500_SPI_HOST      SPI2_HOST
  #define W5500_SPI_CLOCK_HZ  (8 * 1000 * 1000)   // 8 MHz (ekstern, længere ledninger)

#elif defined(BOARD_ES32D26)
  // Eletechsup ES32D26 IO Board (ESP32-D0WD-V3 rev3.1, 38-pin)
  // 8DO (relæer via SN74HC595), 8DI (via SN74HC165), 8AI, 2AO
  // Ingen knapper (modsat ES32A08)
  // Onboard RS485 transceiver: UART0 (GPIO1/3), DIR=GPIO21
  // Shift registers (595): GPIO12(DATA), GPIO22(CLK), GPIO23(LATCH), GPIO13(OE)
  // Shift registers (165): GPIO0(DATA), GPIO2(CLK), GPIO15(LOAD)
  // Analog inputs: GPIO14, 27, 32, 33 (voltage), GPIO34, 35, 36, 39 (current)
  // Analog outputs: GPIO25(DAC1), GPIO26(DAC2)
  // Ledige GPIOs: 4, 5(boot), 16, 17, 18, 19
  // OBS: Ingen ledig LED — GPIO2 brugt til 74HC165 CLK, GPIO15 til 74HC165 LOAD
  #define PIN_LED             -1    // Ingen ledig LED (GPIO2 optaget af 74HC165)
  // Modbus RTU via onboard RS485 transceiver (UART remapped via GPIO matrix)
  // OBS: Delt med USB debug serial — kun én aktiv ad gangen
  #define PIN_UART1_RX        3     // Onboard RS485 RX
  #define PIN_UART1_TX        1     // Onboard RS485 TX
  #define PIN_RS485_DIR       21    // Onboard RS485 DE/RE
  // Counter interrupt pins — IKKE tilgængelige
  // HW-ISR counter mode kan ikke bruges på dette board
  #define PIN_INT1            -1
  #define PIN_INT2            -1
  #define PIN_INT3            -1
  #define PIN_INT4            -1
  // I2C — ikke tilgængelig (ingen ledige I2C-egnede pins)
  #define PIN_I2C_SDA         -1
  #define PIN_I2C_SCL         -1
  // W5500 SPI (ekstern modul, bruger ledige GPIOs: 4, 5, 16, 17, 18, 19)
  #define PIN_SPI_MISO        19    // VSPI MISO (ledig)
  #define PIN_SPI_MOSI        17    // Remapped MOSI (ledig)
  #define PIN_SPI_CLK         18    // VSPI CLK  (ledig)
  #define PIN_SPI_CS          5     // Chip Select (ledig, ⚠️ strapping pin)
  #define PIN_W5500_INT       4     // Interrupt (ledig)
  #define PIN_W5500_RST       16    // Hardware Reset (ledig)
  #define W5500_SPI_HOST      SPI3_HOST   // VSPI
  #define W5500_SPI_CLOCK_HZ  (8 * 1000 * 1000)   // 8 MHz (ekstern modul)
  // Analog Inputs — Spænding (onboard signal conditioning)
  #define PIN_AI_V1           14    // Vi1: 0-10V (ADC2_CH6, ⚠️ ikke med WiFi)
  #define PIN_AI_V2           33    // Vi2: 0-10V (ADC1_CH5, ✅ OK med WiFi)
  #define PIN_AI_V3           27    // Vi3: 0-10V (ADC2_CH7, ⚠️ ikke med WiFi)
  #define PIN_AI_V4           32    // Vi4: 0-10V (ADC1_CH4, ✅ OK med WiFi)
  // Analog Inputs — Strøm (onboard signal conditioning)
  #define PIN_AI_I1           34    // Ii1: 4-20mA (ADC1_CH6, input-only)
  #define PIN_AI_I2           39    // Ii2: 4-20mA (ADC1_CH3, input-only)
  #define PIN_AI_I3           36    // Ii3: 4-20mA (ADC1_CH0, input-only)
  #define PIN_AI_I4           35    // Ii4: 4-20mA (ADC1_CH7, input-only)
  // Analog Outputs (onboard DAC)
  #define PIN_AO1             25    // AO1: Vo1/Io1 (DAC1)
  #define PIN_AO2             26    // AO2: Vo2/Io2 (DAC2)
  // Status LED — ikke tilgængelig (GPIO15 brugt til 74HC165 LOAD)
  #define PIN_STATUS_LED      -1
  // Shift Register hardware pins — SN74HC595 (relæ-udgange) — VERIFIED med multimeter
  #define PIN_SR_OUT_DATA     12    // SN74HC595 pin 14: SER (serial data)
  #define PIN_SR_OUT_CLOCK    22    // SN74HC595 pin 11: SRCLK (shift clock)
  #define PIN_SR_OUT_LATCH    23    // SN74HC595 pin 12: RCLK (storage/latch clock)
  #define PIN_SR_OUT_OE       13    // SN74HC595 pin 13: OE (output enable, active LOW)
  // Shift Register hardware pins — SN74HC165 (digitale inputs)
  #define PIN_SR_IN_LOAD      0     // SN74HC165 pin 1: SH/LD (parallel load, active LOW) ⚠️ strapping pin
  #define PIN_SR_IN_CLOCK     2     // SN74HC165 pin 2: CLK (clock) ⚠️ strapping pin
  #define PIN_SR_IN_DATA      15    // SN74HC165 pin 9: QH (serial data out) ⚠️ strapping pin
  #define SR_OUT_COUNT        1     // ES32D26: 1x 74HC595 (8 relæer, CH1-CH8)
  #define SR_IN_COUNT         1     // ES32D26: 1x 74HC165 (8 opto-inputs, IN1-IN8)
  // Virtual GPIO ranges for shift register I/O (1-indexed)
  // GPIO 101-108: Shift register inputs  (SN74HC165 IN1-IN8)
  // GPIO 201-208: Shift register outputs (SN74HC595 CH1-CH8 relæer)
  #define VGPIO_SR_INPUT_BASE   101   // Virtual GPIO 101 = IN1, 102 = IN2, ...
  #define VGPIO_SR_INPUT_COUNT  (SR_IN_COUNT * 8)
  #define VGPIO_SR_OUTPUT_BASE  201   // Virtual GPIO 201 = CH1, 202 = CH2, ...
  #define VGPIO_SR_OUTPUT_COUNT (SR_OUT_COUNT * 8)
  // Shift register feature flag
  #define SHIFT_REGISTER_ENABLED

#elif defined(BOARD_ESP32_30PIN)
  // ESP32-WROOM-32 30-pin DevKit (DEFAULT)
  #define PIN_LED             2
  #define PIN_UART1_RX        4
  #define PIN_UART1_TX        5
  #define PIN_RS485_DIR       15
  #define PIN_INT1            16
  #define PIN_INT2            17
  #define PIN_INT3            18
  #define PIN_INT4            19
  #define PIN_I2C_SDA         21
  #define PIN_I2C_SCL         22
  // W5500 SPI (ekstern modul via HSPI)
  #define PIN_SPI_MISO        12
  #define PIN_SPI_MOSI        13
  #define PIN_SPI_CLK         14
  #define PIN_SPI_CS          23
  #define PIN_W5500_INT       34
  #define PIN_W5500_RST       33
  #define W5500_SPI_HOST      SPI2_HOST
  #define W5500_SPI_CLOCK_HZ  (8 * 1000 * 1000)   // 8 MHz (ekstern, længere ledninger)

#else
  #error "Ingen BOARD_* variant defineret! Vælg én i platformio.ini build_flags"
#endif

/* ============================================================================
 * SERIAL CONFIGURATION
 * ============================================================================ */

#define SERIAL_BAUD_DEBUG   115200      // USB serial (UART0) baud rate
#define SERIAL_BAUD_MODBUS  115200      // Modbus RTU (UART1) baud rate

/* ============================================================================
 * TIMING CONSTANTS
 * ============================================================================ */

#define HEARTBEAT_INTERVAL_MS   500     // LED blink interval
#define FREQUENCY_MEAS_WINDOW_MS 1000   // Frequency measurement window (1-2 sec)
#define COUNTER_DEBOUNCE_MS     10      // Default debounce filter

/* ============================================================================
 * DEBUG FLAGS (runtime controllable)
 * ============================================================================ */

#define DEBUG_CONFIG_SAVE       1       // Show debug when saving config to NVS
#define DEBUG_CONFIG_LOAD       1       // Show debug when loading config from NVS

/* ============================================================================
 * NETWORK CONFIGURATION (Wi-Fi, Telnet)
 * ============================================================================ */

#define NETWORK_ENABLED                 1           // 0 = disabled, 1 = enabled
#define WIFI_MODE_STATION               1           // Client mode (not AP mode)
#define WIFI_SSID_MAX_LEN               32          // Max SSID length (IEEE 802.11)
#define WIFI_PASSWORD_MAX_LEN           64          // Max password length (WPA2)
#define WIFI_SCAN_TIMEOUT_MS            10000       // Wi-Fi scan timeout
#define WIFI_CONNECT_TIMEOUT_MS         15000       // Connection timeout
#define WIFI_RECONNECT_INTERVAL_MS      5000        // Retry interval on disconnect
#define WIFI_RECONNECT_MAX_RETRIES      10          // Max reconnection attempts

#define TELNET_PORT                     23          // Telnet standard port
#define TELNET_MAX_CLIENTS              1           // Single client for simplicity
#define TELNET_BUFFER_SIZE              256         // Per-client input buffer
#define TELNET_READ_TIMEOUT_MS          0           // Disabled - no idle timeout (use "exit" to disconnect)
#define TELNET_NEWLINE_CHAR             '\n'        // Telnet uses LF for line ending

/* Telnet IAC (Interpret As Command) protocol bytes */
#define TELNET_IAC                      255         // Interpret As Command
#define TELNET_DONT                     254         // Don't enable option
#define TELNET_DO                       253         // Enable option
#define TELNET_WONT                     252         // Won't enable option
#define TELNET_WILL                     251         // Will enable option
#define TELNET_SB                       250         // Subnegotiation start
#define TELNET_SE                       240         // Subnegotiation end

/* Telnet options */
#define TELNET_OPT_ECHO                 1           // Echo
#define TELNET_OPT_SUPPRESS_GA          3           // Suppress Go Ahead
#define TELNET_OPT_LINEMODE             34          // Line mode

#define DHCP_ENABLED                    1           // Use DHCP (vs static IP)
#define DHCP_HOSTNAME                   "modbus-esp32"  // DHCP hostname

/* NVS namespace for network config */
#define NVS_NAMESPACE_NETWORK           "network"

/* ============================================================================
 * MODBUS MASTER CONFIGURATION (UART1)
 * ============================================================================ */

// Hardware pins — board-dependent
#if defined(BOARD_ES32D26)
  // ES32D26: Single RS485 transceiver shared with slave (GPIO1/3/21)
  // Master reuses uart1_* functions — no separate HardwareSerial
  #define MODBUS_SINGLE_TRANSCEIVER 1
  #define MODBUS_MASTER_TX_PIN      PIN_UART1_TX   // GPIO1 (shared)
  #define MODBUS_MASTER_RX_PIN      PIN_UART1_RX   // GPIO3 (shared)
  #define MODBUS_MASTER_DE_PIN      PIN_RS485_DIR  // GPIO21 (shared)
#else
  // Other boards: dedicated UART1 for master (separate RS485 port)
  #define MODBUS_SINGLE_TRANSCEIVER 0
  #define MODBUS_MASTER_TX_PIN      25    // UART1 TX
  #define MODBUS_MASTER_RX_PIN      26    // UART1 RX
  #define MODBUS_MASTER_DE_PIN      27    // MAX485 DE/RE control
#endif

// Default configuration
#define MODBUS_MASTER_DEFAULT_BAUDRATE     9600
#define MODBUS_MASTER_DEFAULT_PARITY       0     // 0=none, 1=even, 2=odd
#define MODBUS_MASTER_DEFAULT_STOP_BITS    1
#define MODBUS_MASTER_DEFAULT_TIMEOUT      500   // ms
#define MODBUS_MASTER_DEFAULT_INTER_FRAME  10    // ms
#define MODBUS_MASTER_DEFAULT_MAX_REQUESTS 10    // per cycle

// Protocol constants
#define MODBUS_MASTER_MIN_RESPONSE_TIME    3     // ms (minimum inter-frame delay)
#define MODBUS_MASTER_MAX_RETRIES          0     // No retries (ST Logic handles it)

/* ============================================================================
 * HTTP REST API CONFIGURATION (v6.0.0+)
 * ============================================================================ */

#define HTTP_SERVER_PORT                80          // Default HTTP port
#define HTTP_SERVER_MAX_URI_LEN         128         // Max URI length for API endpoints
#define HTTP_SERVER_MAX_RESP_SIZE       2048        // Max JSON response size
#define HTTP_JSON_DOC_SIZE              1024        // ArduinoJson document size
#define HTTP_AUTH_USERNAME_MAX_LEN      32          // Max username length
#define HTTP_AUTH_PASSWORD_MAX_LEN      64          // Max password length

/* NVS namespace for HTTP config */
#define NVS_NAMESPACE_HTTP              "http"

/* ============================================================================
 * OTA FIRMWARE UPDATE (FEAT-031)
 * ============================================================================ */

#define OTA_CHUNK_SIZE          4096        // Flash write chunk size (one sector)
#define OTA_MAX_FIRMWARE_SIZE   0x1A0000    // Must match OTA partition size (1.625MB)
#define OTA_REBOOT_DELAY_MS     2000        // Delay before reboot after OTA

/* ============================================================================
 * VERSION & BUILD
 * ============================================================================ */

#define PROJECT_NAME        "Modbus RTU Server (ESP32)"
#define PROJECT_VERSION     "7.9.7.6"
// BUILD_DATE and BUILD_NUMBER now in build_version.h (auto-generated)

/* Version history:
 * v7.9.7.6 (2026-04-20): FEAT-148: ST program pool 8× forstørret via PSRAM
 *                    - ST_LOGIC_POOL_SIZE: 8 KB → 64 KB (#ifdef BOARD_HAS_PSRAM)
 *                    - source_pool: static DRAM array → dynamisk heap_caps_malloc(SPIRAM)
 *                      med fallback til regular heap hvis PSRAM fejler
 *                    - DRAM sparet: 8 KB (pool flyttet helt ud af BSS)
 *                    - Et enkelt ST program kan nu være op til 64 KB source code
 *                    - Eller 4 programmer á 16 KB — 8× tidligere kapacitet
 * v7.9.7.5 (2026-04-20): FEAT-147: Flash chip info synlig i CLI + metrics
 *                    - `show version`/`show status` viser nu Flash MB @ MHz + mode (QIO/DIO)
 *                    - `Target:` linje afspejler nu WROVER når BOARD_HAS_PSRAM er defineret
 *                    - /api/metrics eksponerer esp32_flash_total_bytes + esp32_flash_speed_hz
 * v7.9.7.4 (2026-04-20): FEAT-146: Flash-optimering — fra 101.3% til 89.5%
 *                    - LTO aktiveret: -flto + -ffunction-sections + -fdata-sections +
 *                      -Wl,--gc-sections (sparede 25 KB via cross-TU inlining + dead-code)
 *                    - CORE_DEBUG_LEVEL: INFO (3) → WARNING (2), LOG_LOCAL_LEVEL: ESP_LOG_WARN
 *                    - Partition repartition: OTA 1.625 → 1.812 MB, SPIFFS 640 → 256 KB
 *                    - NB: Første flash kræver `pio run -t erase && pio run -t upload`
 * v7.9.7.3 (2026-04-20): FEAT-145: ES32D26 → ESP32-WROVER (4 MB PSRAM)
 *                    - platformio.ini env:es32d26: board = esp-wrover-kit
 *                    - build_flags: -DBOARD_HAS_PSRAM -mfix-esp32-psram-cache-issue
 *                    - board_build.arduino.memory_type = qio_qspi
 *                    - Eksisterende ESP.getPsramSize() detection (BUG-250) viser nu ~4 MB
 * v7.9.7.2 (2026-04-14): FEAT: Konfigurerbar cache/kø-størrelse via CLI + NVS
 *                    - set modbus-master cache-size <1-32> og queue-size <4-32>
 *                    - Runtime limits med compile-time max som øvre grænse
 *                    - Schema 18→19 migration med defaults (cache:32, queue:16)
 *                    - show config/running-config viser cache-ttl, cache-size, queue-size
 * v7.9.7.1 (2026-04-14): FEAT: Modbus Master priority queue + forbedret cache statistik
 *                    - Priority køsystem: Write > Read-fresh > Read-refresh
 *                    - Eviction: nyeste lavprioritets-requests droppes først ved fuld kø
 *                    - FreeRTOS FIFO queue erstattet med custom priority array + mutex/semaphore
 *                    - Nye Prometheus metrics: queue_depth, queue_hwm, priority_drops, hit_rate, utilization
 *                    - Dashboard Modbus Master kort: queue depth, HWM, priority drops, cache utilization
 * v7.9.7.0 (2026-04-14): FEAT: Dashboard sub-tabs + indstillinger
 *                    - Metrics-side opdelt i faner: Alle, Overblik, Modbus, Forbindelser, Applikation
 *                    - Ny "Indstillinger" side: vis/skjul kort, flyt kort mellem faner
 *                    - Backend: card_tabs + card_hidden i PersistConfig (schema 17→18)
 *                    - Tab-valg gemmes i localStorage, kort-config gemmes på ESP32 via API
 * v7.9.6.7 (2026-04-14): FEAT-075: TCP forbindelsesmonitor dashboard kort
 *                    - Nyt "TCP Forbindelser" kort på dashboard med live SSE/Telnet/HTTP status
 *                    - Telnet klient IP, bruger og forbindelsestid i Prometheus metrics
 *                    - SSE klient-detaljetabel (IP, bruger, topics, uptime) via /api/events/clients
 *                    - TcpClient struct udvidet med connected_ms for præcis forbindelsestid
 * v7.9.6.6 (2026-04-13): FEAT: Masonry dashboard layout — dynamisk kort-stacking
 *                    - Kort pakkes tæt (små kort stakkes ved siden af store)
 *                    - Bred/normal toggle per kort (⇔ knap) med localStorage persistence
 *                    - Mobile fallback til enkelt-kolonne layout
 * v7.9.6.5 (2026-04-13): FIX: Login modal flash ved sideskift i web GUI
 *                    - BUG-323: Editor/CLI/System sider startede med login modal synlig
 *                    - FEAT-140: Modbus Master mini-form labels (FC Type, Slave ID, Adresse, Antal/Værdi)
 *                               (class="show"), så brugeren blev promptet for login ved
 *                               hvert sideskift selvom sessionStorage havde gyldigt auth.
 *                               Nu starter modal skjult og vises kun hvis auth mangler/fejler.
 * v7.9.5.1 (2026-04-10): FIX: Modbus Master wire-level kommunikation
 *                    - BUG-315: uart1_init() hardcoded SERIAL_8N1 → master parity/stop-bits
 *                               ignoreret på ES32D26 (shared transceiver). Ny uart1_init_ex().
 *                    - BUG-316: 50µs post-TX delay for lidt ved lav baudrate — sidste byte
 *                               (CRC hi) kunne afskæres når DE faldt LOW. Nu beregnet fra baudrate.
 * v7.9.5.0 (2026-04-09): FEAT: mb CLI — remote Modbus Master read/write/scan/reset backoff
 * v7.9.4.1 (2026-04-09): FIX: 4 bugs fundet under FEAT-121-126 integration test (23/23 PASS)
 *                    - BUG-311: ST_AST_FUNCTION_CALL ikke håndteret i statement compiler (kun expr)
 *                    - BUG-312: TIME variabel vist som int16 i REST API (manglende ST_TYPE_TIME case)
 *                    - BUG-313: DINT literal >32767 crashede ESP32 (parser overflow + PUSH_INT 16-bit)
 *                    - BUG-314: Integer auto-promote til DINT ved overflow (>INT16_MAX / <INT16_MIN)
 * v7.9.4.0 (2026-04-08): FEAT-121-126: IEC 61131-3 TIME, TON, TOF, CTU, CTD, CTUD
 *                    - FEAT-121: ST_TYPE_TIME native datatype, TIME keyword, T# literals som TIME
 *                    - FEAT-122-123: TON/TOF med IEC 61131-3 named-parameter syntax (IN:=, PT:=, Q=>, ET=>)
 *                    - FEAT-124-126: CTU/CTD/CTUD med named-parameter syntax (CU:=, PV:=, Q=>, CV=>)
 *                    - Ny opcode ST_OP_LOAD_FB_FIELD: læser timer/counter instance-felter (ET, CV, QD)
 *                    - Ny token ST_TOK_OUTPUT_ARROW (=>) for output parameter binding
 *                    - Fuld backward-kompatibilitet med eksisterende positionel syntax
 *                    - Timer PT udvidet til 32-bit (op til ~24.8 dage, var 32 sekunder)
 * v7.9.3.2 (2026-04-08): OPT: Modbus Master cache + timeout optimering
 *                    - Request struct: 41->10 bytes (multi_regs pool), sparer ~496B i queue
 *                    - Cache LRU eviction: ældste entry erstattes ved fuld cache
 *                    - Write deduplication: skip identiske writes allerede i cache
 *                    - FC16 response parsing: manglede 0x10 case (ventede til timeout)
 *                    - Dual-phase timeout: fuld timeout for første byte, T3.5 interchar derefter
 *                    - Per-slave adaptive backoff: 50ms->2s ved consecutive timeouts, decay ved success
 * v7.9.3.1 (2026-04-08): UI: Realtime compiler ressource-info i ST editor
 *                    - /api/logic returnerer resources{} med heap, pool, AST node stats
 *                    - Editor Heap-bar: storste blok + max AST nodes (auto-poll 5s)
 * v7.9.3 (2026-04-08): OPT: ST Compiler heap optimization
 *                    - AST node: 156 -> 84 bytes (46% reduktion, ~9 KB/128 nodes)
 *                    - var_names: [32][32] -> [32][16] (512 bytes/program sparet)
 *                    - Bytecode buffer: dynamisk 256->2048 (var 8 KB fast, nu 2 KB start)
 *                    - Function registry: functions[64] -> [32] (~1.7 KB/program)
 *                    - CASE branches: inline[16] -> heap-allokeret pointer
 *                    - Bytecode persist format v3
 * v7.9.2 (2026-04-06): FEAT: Multi-register Modbus Master fra ST Logic
 *                    - MB_READ_HOLDINGS(slave, addr, count): FC03 multi-register read
 *                    - MB_WRITE_HOLDINGS(slave, addr, count): FC16 multi-register write
 *                    - Protocol: modbus_master_read_holdings() + write_holdings()
 *                    - Async: MB_REQ_READ_HOLDINGS/WRITE_HOLDINGS i mb_async task
 * v7.7.2 (2026-03-31): FEAT: HW Counter Access fra ST Logic
 *                    - 9 nye ST builtins: CNT_SETUP, CNT_SETUP_ADV, CNT_SETUP_CMP,
 *                      CNT_ENABLE, CNT_CTRL, CNT_VALUE, CNT_RAW, CNT_FREQ, CNT_STATUS
 *                    - Konfigurer, styr og aflæs ESP32 HW counters direkte fra ST-programmer
 *                    - Config fra ST afspejles i CLI (show counter) og NVS persistens
 *                    - VM udvidet til max 6 argumenter (CNT_SETUP)
 *                    - Web editor: autocomplete + hjælpepanel for CNT funktioner
 * v7.7.1.1 (2026-03-31): VAR initializer fix + Telnet RBAC + version i web UI
 *                    - FIX: VAR declarations med initial values (fx INT := 120) virker nu
 *                    - FIX: Telnet bruger RBAC når enabled, ellers lokal telnet user/pass
 *                    - FIX: Web System/Administration viser version+build konsistent
 * v7.7.0 (2026-03-31): Async Modbus Master — nul blokering, nul overruns
 *                    - FEAT-070: Dedikeret FreeRTOS baggrundstask (Core 0) til Modbus UART I/O
 *                    - FEAT-070: MB_Read/Write builtins er nu non-blocking (cached reads, queued writes)
 *                    - FEAT-070: Nye ST builtins: MB_SUCCESS(), MB_BUSY(), MB_ERROR()
 *                    - FEAT-070: Request deduplication — identical reads samles automatisk
 *                    - FEAT-070: Async cache med 32 entries, 16-deep request queue
 *                    - FEAT-070: show modbus-master viser nu async cache statistik + entries
 *                    - Backward-kompatibel: eksisterende ST-programmer virker uændret
 * v7.6.2.7 (2026-03-31): SSE konfiguration i backup/restore
 *                      - FEAT-069: Backup eksporterer nu SSE indstillinger (enabled, port, max_clients,
 *                        check_interval_ms, heartbeat_ms) som selvstændig "sse" JSON sektion
 *                      - FEAT-069: Restore gendanner SSE indstillinger fra backup fil
 * v7.6.2.6 (2026-03-31): Fix confirm dialog — callback blev aldrig kaldt
 *                      - BUG-268: confirmAction() kaldte closeConfirm() som satte pendingConfirmFn=null
 *                        FØR callback blev kaldt. Fabriksindstillinger, Genstart, SSE disconnect-all
 *                        viste bekræftelsesdialog men gjorde ingenting ved "Bekræft"
 * v7.6.2.5 (2026-03-31): Web system fejlhåndtering for write-operationer
 *                      - BUG-267: doReboot() catch slugte fejl silently — brugeren så "Genstarter..."
 *                        men intet skete. Alle write-knapper viser nu 403-fejl ved manglende privilege
 *                      - Forbedret fejlvisning for reboot, save, load, defaults, SSE disconnect
 * v7.6.2.4 (2026-03-31): RBAC privilege enforcement + SSE rolle-fix
 *                      - BUG-265: 42 write-endpoints ændret fra CHECK_AUTH til CHECK_AUTH_WRITE
 *                        Read-only brugere fik 200 OK på POST/DELETE — nu 403 "Write privilege required"
 *                      - BUG-266: rbac_parse_privilege("write") returnerede PRIV_RW i stedet for PRIV_WRITE
 *                      - BUG-264: SSE afviste brugere med api rolle — udvidet til MONITOR|API
 *                      - show sse viser nu RBAC Authentication sektion
 *                      - show users roles viser SSE krav (api ELLER monitor)
 * v7.6.2.3 (2026-03-30): Fix /api/user/me route + SSE klient management i web
 *                      - BUG-263: /api/user/me manglede httpd URI handler registrering
 *                        (kun i v1_routes, ikke direkte — fetch('/api/user/me') gav 404)
 *                      - FIX: Dashboard login modal + Log ind knap i user menu
 *                      - FIX: updateUserBadge() kaldes nu efter login i alle 4 web sider
 *                      - FEAT-066: SSE klient management sektion i web System side
 *                        GET /api/events/clients + POST /api/events/disconnect
 *                        Vis slot, IP, bruger, topics, uptime + afbryd knapper
 * v7.6.2.2 (2026-03-30): Web user badge + show user CLI + /api/user/me
 *                      - FEAT-063: User badge i topnav med login/logout og session info
 *                      - FEAT: GET /api/user/me returnerer autentificeret bruger info
 *                      - FEAT: show user CLI kommando viser aktuel session
 *                      - FIX: show config http viser RBAC status i stedet for legacy auth
 * v7.6.2.1 (2026-03-30): SSE monitor udvidelse + show config RBAC fix
 *                      - FEAT-062: show sse viser username, IP, topics, uptime per klient
 *                      - FIX: BUG-261 show config rbac viste tom sektion (selvstændig filter)
 *                      - FIX: BUG-262 set rbac alias → USER i normalize_alias()
 *                      - FEAT: show config set-commands inkluderer RBAC user-kommandoer
 * v7.6.2 (2026-03-30): RBAC multi-user system (op til 8 brugere)
 *                      - FEAT-059: Role-Based Access Control med roller (api, cli, editor, monitor)
 *                      - FEAT: Privilege-levels (read, write, read/write)
 *                      - FEAT: RBAC auth for HTTP API, SSE, CLI og Web UI
 *                      - FEAT: CLI kommandoer: set user, show users, delete user
 *                      - FEAT: Schema migration v14→v15 (legacy single-user → RBAC)
 *                      - FEAT: SSE kræver MONITOR rolle
 *                      - Nye filer: rbac.h, rbac.cpp
 * v7.6.1.1 (2026-03-30): Web CLI udskilt som selvstændig side + konsistent navigation
 *                      - FEAT: Ny /cli side med standalone Web CLI console
 *                      - FEAT: Konsistent topnav på alle sider: Monitor | ST Editor | CLI | System | OTA Update
 *                      - REFACTOR: CLI fjernet fra ST Editor (var view-panel, nu egen side)
 *                      - Nye filer: web_cli.h, web_cli.cpp
 * v7.6.1 (2026-03-30): FIX: SSE subscribe=all overvåger nu ALLE registre (watch_all mode)
 *                      - FIX: BUG-250 subscribe=all defaultede til HR 0-15 — coils/IR/DI ignoreret
 *                      - FEAT: watch_all mode scanner 256 HR + 256 IR + 256 coils + 256 DI
 *                      - FEAT: Heap-allokeret SseWatchAllState (~1.5 KB pr. klient)
 *                      - FEAT: connected event rapporterer "mode":"all" i watching-felt
 *                      - DOC: SSE_USER_GUIDE.md brugervejledning med curl/JS/Node-RED/Python eksempler
 * v7.6.0 (2026-03-29): Bytecode persistens i SPIFFS + fasebaseret ST compiler
 *                      - FEAT: Bytecode cache i /logic_N.bc (CRC32 validering)
 *                      - FEAT: Boot indlæser cached bytecode (~2 KB peak vs 36-94 KB recompile)
 *                      - FEAT: AST pool reserve 6→24 KB (function registry allokering virker nu)
 *                      - FEAT: Segment compiler + source scanner for chunked compilation
 *                      - FEAT: st_logic_compile_chunked() multi-pass pipeline
 *                      - FIX: FUNCTION/FUNCTION_BLOCK kompilering fejlede pga. heap pressure
 * v7.3.0 (2026-03-25): FEAT-038 Web-based ST Logic Editor
 *                      - FEAT: Embedded web editor served at /editor (HTML/CSS/JS in PROGMEM)
 *                      - FEAT: Program slot selector (1-4) with compile status indicators
 *                      - FEAT: Code editor with line numbers, Tab indent, Ctrl+S to compile
 *                      - FEAT: Live pool usage meter (8KB shared source pool)
 *                      - FEAT: ST language keyword reference sidebar
 *                      - FEAT: Enable/disable toggle, delete, save config from browser
 *                      - FEAT: Login modal with Basic Auth (uses existing API auth)
 *                      - FEAT: Compile error feedback with timestamped output log
 *                      - FIX: GPIO metrics #ifdef guard for SHIFT_REGISTER_ENABLED
 *                      - Zero runtime RAM impact (all HTML in flash/PROGMEM)
 *                      - Uses existing /api/logic/* endpoints — no new API needed
 * v7.2.2 (2026-03-25): FEAT-032 Extended Prometheus metrics
 *                      - FEAT: ST Logic metrics (execution_count, error_count, exec_time_us, min/max/overruns per slot)
 *                      - FEAT: Counter frequency (Hz) per counter
 *                      - FEAT: Timer runtime state (is_running, current_phase per timer)
 *                      - FEAT: GPIO digital input/output state (74HC165 IN, 74HC595 DO)
 *                      - FEAT: Modbus holding/input register values (non-zero)
 *                      - FEAT: Persistence group metrics (reg_count, last_save_ms)
 *                      - FEAT: Network metrics (telnet_connected, wifi_reconnect_retries)
 *                      - FEAT: ST Logic global stats (total_cycles, cycle_overruns)
 *                      - Buffer 4KB → 8KB for expanded metrics
 * v7.2.1 (2026-03-25): BUG-239 Modbus Master config sync + BUG-240 ST compiler memory optimering
 *                      - FIX: BUG-239 Modbus Master runtime config synced fra NVS ved boot
 *                      - FIX: BUG-240 ST compiler 5x memory optimering:
 *                        AST node 1920→140 bytes, pool allocator, direct bytecode output,
 *                        compiler bytecode pointer, parser early-free
 *                        Max ST program: 21→34 IF-blokke (+62%), min heap 47→80 KB (+70%)
 *                      - DOC: DM56A04 display tutorial + ST size test
 * v7.2.0 (2026-03-23): Modbus UART refaktor (ES32D26 single-transceiver) + AO mode config
 *                      - FEAT: Modbus mode CLI: set modbus mode slave|master|off
 *                      - FEAT: ES32D26 shared RS485 transceiver — slave ELLER master (MODBUS_SINGLE_TRANSCEIVER)
 *                      - FEAT: AO output mode CLI: set ao1|ao2 mode voltage|current (DIP switch SW1 config)
 *                      - FEAT: modbus_mode, ao1_mode, ao2_mode i PersistConfig (schema 12→13)
 *                      - FEAT: Backup/restore + REST API support for nye felter
 *                      - FIX: Modbus Master bruger ikke laengere GPIO25/26 paa ES32D26 (DAC outputs)
 * v7.1.1 (2026-03-23): BUG-237 WiFi static IP fix + BUG-238 74HC165 pin-mapping fix (ES32D26)
 *                      - FIX: wifi_driver_apply_static_ip() kaldes nu i WIFI_EVENT_STA_CONNECTED
 *                      - FIX: 74HC165 DATA/LOAD pin-mapping byttet (GPIO0=LOAD, GPIO15=DATA)
 *                      - FEAT: 'test sr input' CLI kommando til 74HC165 diagnostik
 * v7.1.0 (2026-03-18): FEAT-022 Persist Group API + FEAT-028 Rate Limiting + FEAT-032 Prometheus Metrics
 * v7.0.3 (2026-03-18): SSE CLI management + konfigurerbare SSE parametre
 *                      - FEAT: CLI `set sse` / `show sse` sektioner (enable/disable/port/max-clients/interval/heartbeat)
 *                      - FEAT: SSE klient-registry med IP-tracking per slot
 *                      - FEAT: `show sse` viser tilsluttede klienter med IP, fd, uptime
 *                      - FEAT: `set sse disconnect all|<slot>` — kick klienter fra CLI
 *                      - FEAT: `show config` inkluderer [API SSE] status + # API SSE config sektioner
 *                      - FEAT: SSE parametre konfigurerbare og persisteret i NVS (schema 12)
 *                      - FIX: `set sse` alias registreret i normalize_alias()
 * v7.0.2 (2026-03-18): SSE multi-klient stabilitet + reconnection-beskyttelse
 *                      - FIX: Heap-check før client task spawn (afvis ved <10KB fri)
 *                      - FIX: Cooldown (500ms) efter task spawn mod rapid reconnect storms
 *                      - FIX: Defensiv sse_active_clients decrement (undgår underflow)
 *                      - FIX: 1s delay ved task creation failure
 *                      - Testet: 2 samtidige SSE klienter bekræftet stabil
 * v7.0.1 (2026-03-17): SSE register watch udvidet til alle 4 registertyper
 *                      - Query params: ?hr=0,5,10-15&coils=0-7&ir=0-3&di=0-3
 *                      - Understøtter: Holding Registers, Input Registers, Coils, Discrete Inputs
 *                      - Max 32 adresser per type, ranges og enkelt-adresser
 *                      - Backward-kompatibelt: default = HR 0-15
 * v7.0.0 (2026-03-17): API v7.0.0 — SSE Real-Time Events + API Versioning
 *                      - FEAT-023: GET /api/events — Server-Sent Events for live push updates
 *                        - Subscribe per topic: ?subscribe=counters,timers,registers,system
 *                        - Max 3 samtidige SSE connections, 10 Hz change detection
 *                        - Automatic keepalive heartbeat every 15s
 *                      - FEAT-030: API Versioning — /api/v1/* prefix support
 *                        - GET /api/version — API version info
 *                        - /api/v1/* dispatcher med URI rewriting til eksisterende handlers
 *                        - Backward-kompatibelt: /api/* virker stadig uændret
 *                      - GET /api/events/status — SSE subsystem info
 * v6.3.0 (2026-03-16): API v6.3.0 — 8 nye features (FEAT-019 to FEAT-027)
 *                      - FEAT-019: GET/POST /api/telnet — Telnet konfiguration via API
 *                      - FEAT-020: ST Logic Debug API (pause/step/continue/breakpoint/state)
 *                      - FEAT-021: Bulk register operations (HR/IR/coils/DI read/write ranges)
 *                      - FEAT-024: GET/POST /api/hostname — Hostname ændring via API
 *                      - FEAT-025: GET /api/system/watchdog — Watchdog status
 *                      - FEAT-026: GET/POST /api/gpio/2/heartbeat — GPIO2 heartbeat kontrol
 *                      - FEAT-027: CORS headers (Access-Control-Allow-Origin) + OPTIONS preflight
 *                      - API discovery opdateret med 20 nye endpoints (50 → 70 total)
 * v6.2.0 (2026-03-15): FEAT-018 CLI ping + BUG-235 Ethernet static IP reconnect
 * v6.1.0 (2026-02-25): W5500 Ethernet + Telnet standalone
 *                      - FEAT: W5500 SPI Ethernet driver (GPIO12-15,33,34)
 *                      - FEAT: set ethernet enable/disable/dhcp/ip/gateway/netmask/dns
 *                      - FEAT: set telnet — standalone CLI sektion (uafhængig af WiFi)
 *                      - FIX: BUG-223 W5500 ping ~1000ms → 2-5ms (xTaskNotifyGive polling)
 *                      - FIX: BUG-224 Telnet echo langsom (TCP_NODELAY)
 *                      - FIX: BUG-225 sh config | s telnet viste ingen SET commands
 *                      - FIX: BUG-221+222 CLI parser set wifi disable / set logic interval
 * v6.0.7 (2026-02-14): FEAT-017 Config Backup/Restore via HTTP API + CLI
 *                      - FEAT: GET /api/system/backup - Download fuld config som JSON inkl. passwords + ST Logic source
 *                      - FEAT: POST /api/system/restore - Upload JSON for fuld restore, auto-save + apply
 *                      - FEAT: CLI "show backup" kommando med download URL
 *                      - Build #1227
 * v6.0.3 (2026-01-25): Test plan restructuring
 *                      - DOC: Split ST_COMPLETE_TEST_PLAN.md (46k tokens) into 9 modular files
 *                      - DOC: New tests/ folder with TEST_INDEX.md as navigation hub
 *                      - DOC: Added API_TEST_PLAN.md with 41 HTTP REST API test cases
 *                      - DOC: Updated CLAUDE_INDEX.md, README.md, CHANGELOG.md
 *                      - Build #1135
 * v6.0.2 (2026-01-23): NVS fix + API discovery
 *                      - FEAT: GET /api/ - Discovery endpoint listing all API endpoints
 *                      - FEAT: reset nvs - New CLI command to erase NVS partition
 *                      - FIX: NVS save error 4357 (ESP_ERR_NVS_NOT_ENOUGH_SPACE) with helpful hint
 *                      - Build #1119
 * v6.0.1 (2026-01-23): HTTP API enhancements and bugfixes
 *                      - FEAT: set http api enable/disable - Separate control for REST API endpoints
 *                      - FEAT: set debug http-server/http-api on/off - Debug flags for HTTP
 *                      - FEAT: reset http stats - Reset HTTP API statistics from CLI
 *                      - FEAT: show debug now includes http_server and http_api flags
 *                      - FEAT: config output now shows [API HTTP] section with server/api/auth status
 *                      - BUG-192: Fixed HTTP Basic Auth (missing null-terminator after base64 encode)
 *                      - FIX: defaults command now uses centralized config_struct_create_default()
 *                      - FIX: config_struct_create_default() now calls network_config_init_defaults()
 *                      - Build #1113
 * v6.0.0 (2026-01-21): FEAT-011 HTTP REST API for Node-RED integration
 *                      - NEW: http_server.h/cpp - ESP-IDF HTTP server wrapper
 *                      - NEW: api_handlers.h/cpp - JSON REST endpoint handlers
 *                      - FEAT: GET /api/status - System info (version, uptime, heap, wifi)
 *                      - FEAT: GET /api/counters, /api/counters/{1-4} - Counter data
 *                      - FEAT: GET /api/timers, /api/timers/{1-4} - Timer data
 *                      - FEAT: GET/POST /api/registers/hr/{addr} - Holding registers
 *                      - FEAT: GET/POST /api/registers/coils/{addr} - Coils
 *                      - FEAT: GET /api/registers/ir/{addr}, /di/{addr} - Input registers
 *                      - FEAT: GET /api/logic, /api/logic/{1-4} - ST Logic programs
 *                      - CLI: set http enabled/port/auth/username/password
 *                      - CLI: show http - HTTP server status and statistics
 *                      - DEP: ArduinoJson@^7.0.0 for JSON serialization
 *                      - SCHEMA: v9→v10 migration for HttpConfig
 * v5.3.0 (2026-01-19): FEAT-008 ST Logic Debugger - Single-step, Breakpoints, Variable Inspection
 *                      - FEAT: Pause/continue/step/stop program execution
 *                      - FEAT: Breakpoints at PC addresses (max 8 per program)
 *                      - FEAT: Variable inspection when paused
 *                      - FEAT: Instruction disassembly at current PC
 *                      - NEW: st_debug.h/cpp - Debug state management and display
 *                      - CLI: set logic <id> debug pause/continue/step/stop
 *                      - CLI: set logic <id> debug break <pc> / clear [<pc>]
 *                      - CLI: show logic <id> debug [vars|stack]
 *                      - BUG-190: Fixed step counter (only in debug mode)
 *                      - BUG-191: Fixed halt/error snapshot saving
 *                      - Build #1082-1083
 * v5.2.0 (2026-01-18): FEAT-006 TIME Literal Support (T#5s, T#100ms, T#1h30m)
 *                      - FEAT: Native IEC 61131-3 TIME literals in ST programs
 *                      - Lexer: T#<value><unit> parsing (ms, s, m, h, d)
 *                      - Compound TIME: T#1h30m5s100ms supported
 *                      - Stored as DINT milliseconds internally
 *                      - Build #1075-1081
 * v5.1.0 (2026-01-11): EXPORT Keyword & Dynamic IR Pool Allocation (BUG-143 RESOLVED)
 *                      - FEAT: EXPORT keyword for ST variable visibility control (IEC 61131-3 compliant)
 *                      - FEAT: Dynamic IR 220-251 pool allocation (32 regs shared flexibly across 4 programs)
 *                      - NEW: ir_pool_manager.h/cpp - Dynamic pool allocation with type-aware sizing
 *                      - Type-aware allocation: INT/BOOL=1 reg, REAL/DINT/DWORD=2 regs
 *                      - Pool persistence: ir_pool_reallocate_all() restores allocations after NVS load
 *                      - BUG-143 RESOLVED: Removed fixed 8 vars/program limitation
 *                      - Compiler: Added var_export_flags[32] to bytecode, ST_TOK_EXPORT token
 *                      - registers.cpp: Replaced fixed offset with dynamic pool lookup
 *                      - Documentation: README.md, MODBUS_REGISTER_MAP.md, ST_COMPLETE_TEST_PLAN.md
 *                      - Build #1032-1035
 * v4.8.0 (2026-01-07): Signal Processing Function Blocks + SR/RS Latches
 *                      - FEAT: SCALE(IN, IN_MIN, IN_MAX, OUT_MIN, OUT_MAX) - Linear scaling/mapping (5-arg stateless)
 *                      - FEAT: HYSTERESIS(IN, HIGH, LOW) - Schmitt trigger with dead zone (stateful, 1 byte)
 *                      - FEAT: BLINK(ENABLE, ON_TIME, OFF_TIME) - Pulse generator (stateful, 6 bytes)
 *                      - FEAT: FILTER(IN, TIME_CONSTANT) - Exponential moving average low-pass (stateful, 4 bytes)
 *                      - FEAT: SR/RS bistable latches with priority handling (v4.7.3, Build #999)
 *                      - Extended VM to support 5-argument functions (previously max 3)
 *                      - Memory: +88 bytes per program (452→540 bytes)
 *                      - Documentation: ST_SIGNAL_PROCESSING_DESIGN.md, ST_LATCH_TEST_CASES.md
 *                      - Build #999-1006
 * v4.7.2 (2026-01-04): CLI Consistency & Documentation - Holding-Reg vs Input-Reg Naming
 *                      - BUG-144: CLI naming standardiseret (holding-reg vs input-reg)
 *                      - BUG-145: Fixed missing "read input-reg" i help message
 *                      - BUG-143: Dokumenteret IR 220-251 begrænsning (8 vars/program)
 *                      - MODBUS_REGISTER_MAP.md opdateret med CLI kommando guide
 *                      - Alle help messages konsekvent: "holding-reg" vs "input-reg"
 *                      - Build #973-974
 * v4.7.1 (2026-01-04): Persistent Register Type Support & Documentation Updates
 *                      - FEAT-001: set reg STATIC multi-type support (UINT/INT/DINT/DWORD/REAL)
 *                      - Schema migration v8 → v9 (StaticRegisterMapping struct expansion)
 *                      - Register range validation (ST Logic HR 200-237 protection)
 *                      - Documentation corrections (MODBUS_REGISTER_MAP.md HR/IR ranges)
 *                      - BUG-142: Identified range validation too broad (pending fix)
 *                      - Build #966-972
 * v4.7.0 (2026-01-01): Advanced ST Functions & Memory Optimization
 *                      - 13 new ST functions: EXP/LN/LOG/POW, R_TRIG/F_TRIG, TON/TOF/TP, CTU/CTD/CTUD
 *                      - Stateful function blocks with automatic instance allocation
 *                      - Memory optimization: +17.8KB overflow → +32 bytes (99.8% reduction)
 *                      - Input validation: NaN/INF protection, overflow/underflow guards
 *                      - Documentation: ST_FUNCTIONS_V47.md, RELEASE_NOTES_V47.md
 * v4.4.0 (2025-12-30): CLI Documentation & ST Logic Debugging Enhancements
 *                      - Comprehensive CLI Commands Reference (650+ lines)
 *                      - show logic <id> bytecode command
 *                      - show config reorganization (8 sections with ST variable annotations)
 *                      - README Table of Contents refactor (all 16 documents)
 *                      Fixed: BUG-126, BUG-127, BUG-128 (normalize_alias BYTECODE/TIMING)
 * v4.3.0 (2025-12-20): Auto-Load Persistent Register Groups on boot + CLI parser fixes
 * v4.2.9 (2025-12-18): Counter control parameter restructuring
 * v3.1.1 (2025-12-08): Telnet insert mode & ST upload copy/paste
 * v3.1.0 (2025-12-05): WiFi display & validation improvements
 * v3.0.0 (2025-12-05): WiFi/Network subsystem + Phase 1 security fixes
 * v2.2.0 (2025-12-04): ST Logic Modbus integration & extended registers
 */

#endif // CONSTANTS_H
