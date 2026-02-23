/**
 * @file types.h
 * @brief Central location for ALL struct definitions
 *
 * This file should be the ONLY place where structs are defined.
 * Prevents duplicate definitions and makes the project structure clear.
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include "constants.h"

/* ============================================================================
 * MODBUS REQUEST STRUCTS
 * ============================================================================ */

/* NOTE: ModbusFrame is defined in modbus_frame.h */

typedef struct {
  uint16_t starting_address;
  uint16_t quantity;
} ModbusReadRequest;

typedef struct {
  uint16_t output_address;
  uint16_t output_value;
} ModbusWriteSingleCoilRequest;

typedef struct {
  uint16_t register_address;
  uint16_t register_value;
} ModbusWriteSingleRegisterRequest;

typedef struct {
  uint16_t starting_address;
  uint16_t quantity_of_outputs;
  uint8_t byte_count;
  uint8_t output_values[MODBUS_FRAME_MAX];
} ModbusWriteMultipleCoilsRequest;

typedef struct {
  uint16_t starting_address;
  uint16_t quantity_of_registers;
  uint8_t byte_count;
  uint16_t register_values[MODBUS_FRAME_MAX / 2];
} ModbusWriteMultipleRegistersRequest;

/* ============================================================================
 * COUNTER CONFIGURATION
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;
  CounterModeEnable mode_enable;
  CounterEdgeType edge_type;
  CounterDirection direction;
  CounterHWMode hw_mode;

  uint16_t prescaler;
  uint8_t bit_width;  // 8, 16, 32, 64
  float scale_factor;

  // Register addresses
  uint16_t value_reg;        // Computed/scaled value register (renamed from index_reg)
  uint16_t raw_reg;          // Prescaled value register
  uint16_t freq_reg;         // Frequency (Hz) register
  uint16_t ctrl_reg;         // Control register (bit layout below)
  uint16_t compare_value_reg; // Compare threshold register (BUG-030, v4.2.4)

  // ctrl_reg bit layout:
  //   Bit 0: Reset command (write 1 to reset counter)
  //   Bit 1: Start/Stop command (write 1 to start, 0 to stop)
  //   Bit 2: Running status (read-only: 1=counting, 0=stopped)
  //   Bit 3: Overflow flag (read-only: 1=overflow occurred)
  //   Bit 4: Compare triggered (read-only: 1=compare threshold reached)
  //   Bit 5-6: Reserved (available for future use)
  //   Bit 7: Direction indicator (read-only: 0=up, 1=down)

  // Mode-specific
  uint64_t start_value;   // BUG-183 FIX: Changed from uint16_t to uint64_t for 32/64-bit counter support
  uint8_t debounce_enabled;
  uint16_t debounce_ms;

  // SW polling mode
  uint8_t input_dis;      // Discrete input index

  // SW-ISR mode
  uint8_t interrupt_pin;  // GPIO pin for interrupt

  // HW (PCNT) mode
  uint8_t hw_gpio;        // GPIO pin for PCNT input (BUG FIX 1.9)

  // COMPARE FEATURE (v2.3+)
  uint8_t compare_enabled;      // Enable compare check
  uint8_t compare_mode;         // 0=≥, 1=>, 2=== (exact match)
  uint64_t compare_value;       // Værdi at sammenligne med
  uint8_t reset_on_read;        // Auto-clear bit 4 ved ctrl-reg read
  uint8_t compare_source;       // BUG-040: 0=raw, 1=prescaled, 2=scaled (default: 1)

  // Note: Compare status stored in ctrl_reg bit 4 (no separate fields needed)

  // Reserved for alignment
  uint8_t reserved[1];
} CounterConfig;

typedef struct {
  uint64_t counter_value;      // Changed from uint32_t to match usage
  uint32_t last_level;
  uint32_t debounce_timer;
  uint8_t is_counting;
  uint8_t overflow_flag;       // BUG FIX 1.1: Track overflow
} CounterSWState;

typedef struct {
  uint64_t pcnt_value;         // Changed from uint32_t for consistency
  uint32_t last_count;         // Stores last PCNT read (int16_t range)
  uint32_t overflow_count;
  uint8_t is_counting;
} CounterHWState;

typedef struct {
  CounterConfig config;
  CounterSWState sw_state;
  CounterHWState hw_state;
  uint32_t measured_frequency;
  uint32_t freq_sample_time;

  // COMPARE FEATURE RUNTIME STATE (v2.3+)
  uint8_t compare_triggered;  // Flag: Compare værdi nået denne iteration
  uint32_t compare_time_ms;   // Timestamp når triggered
  uint64_t last_value;        // Previous counter value (for exact match detection)
} Counter;

/* ============================================================================
 * TIMER CONFIGURATION
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;
  TimerMode mode;

  // Mode 1: One-shot
  uint32_t phase1_duration_ms;
  uint32_t phase2_duration_ms;
  uint32_t phase3_duration_ms;
  uint8_t phase1_output_state;
  uint8_t phase2_output_state;
  uint8_t phase3_output_state;

  // Mode 2: Monostable
  uint32_t pulse_duration_ms;
  uint8_t trigger_level;

  // Mode 3: Astable
  uint32_t on_duration_ms;
  uint32_t off_duration_ms;

  // Mode 4: Input-triggered
  uint8_t input_dis;
  uint32_t delay_ms;
  uint8_t trigger_edge;

  // Output
  uint16_t output_coil;

  // Control register (Modbus holding register for start/stop/reset)
  uint16_t ctrl_reg;

  // Reserved for alignment
  uint8_t reserved[6];
} TimerConfig;

typedef struct {
  TimerConfig config;
  uint32_t start_time;
  uint32_t current_phase;
  uint8_t is_running;
  uint8_t output_state;
} Timer;

/* ============================================================================
 * REGISTER MAPPING (STATIC & DYNAMIC)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint16_t register_address;
  uint8_t value_type;           // ModbusValueType (UINT/INT/DINT/DWORD/REAL)
  uint8_t reserved;             // Alignment (future use)
  union {
    uint16_t value_16;          // For UINT/INT (16-bit)
    uint32_t value_32;          // For DINT/DWORD (32-bit)
    float value_real;           // For REAL (32-bit float)
  };
} StaticRegisterMapping;

typedef struct __attribute__((packed)) {
  uint16_t register_address;
  uint8_t source_type;          // DYNAMIC_SOURCE_COUNTER or DYNAMIC_SOURCE_TIMER
  uint8_t source_id;            // Counter/Timer ID (1-4)
  uint8_t source_function;      // CounterFunction or TimerFunction enum
} DynamicRegisterMapping;

/* ============================================================================
 * COIL MAPPING (STATIC & DYNAMIC)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint16_t coil_address;
  uint8_t static_value;         // STATIC: 0 (OFF) or 1 (ON)
} StaticCoilMapping;

typedef struct __attribute__((packed)) {
  uint16_t coil_address;
  uint8_t source_type;          // DYNAMIC_SOURCE_COUNTER or DYNAMIC_SOURCE_TIMER
  uint8_t source_id;            // Counter/Timer ID (1-4)
  uint8_t source_function;      // CounterFunction or TimerFunction enum
} DynamicCoilMapping;

/* ============================================================================
 * UNIFIED VARIABLE MAPPING (GPIO pins + ST variables ↔ Modbus registers)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  // Source type: what is being mapped
  uint8_t source_type;          // MAPPING_SOURCE_GPIO, MAPPING_SOURCE_ST_VAR

  // GPIO mapping (if source_type == MAPPING_SOURCE_GPIO)
  uint8_t gpio_pin;
  uint8_t associated_counter;   // 0xff if none (set via input-dis=<pin>)
  uint8_t associated_timer;     // 0xff if none

  // ST Variable mapping (if source_type == MAPPING_SOURCE_ST_VAR)
  uint8_t st_program_id;        // Logic program ID (0-3), 0xff if none
  uint8_t st_var_index;         // ST variable index (0-31)

  // I/O Configuration
  uint8_t is_input;             // 1 = INPUT mode (source → register), 0 = OUTPUT mode (register → source)
  uint8_t input_type;           // 0 = Holding Register (HR), 1 = Discrete Input (DI) - only for INPUT mode
  uint8_t output_type;          // 0 = Holding Register (HR), 1 = Coil - only for OUTPUT mode
  uint16_t input_reg;           // Input register index (65535 if none) - for INPUT mode
  uint16_t coil_reg;            // Coil/output register index (65535 if none) - for OUTPUT mode (NOTE: also holds reg address if output_type=0)

  // BUG-105: Multi-register support for DINT/REAL (32-bit types)
  uint8_t word_count;           // Number of consecutive 16-bit registers (1=INT/BOOL, 2=DINT/REAL/DWORD)
} VariableMapping;

/* ============================================================================
 * PERSISTENT REGISTER GROUPS (v4.0+)
 * ============================================================================ */

#define PERSIST_GROUP_MAX_REGS  16   // Max registers per group
#define PERSIST_MAX_GROUPS      8    // Max persistence groups

typedef struct __attribute__((packed)) {
  char name[16];                     // Group name (null-terminated)
  uint8_t reg_count;                 // Number of registers (0-16)
  uint16_t reg_addresses[PERSIST_GROUP_MAX_REGS];  // Register addresses
  uint16_t reg_values[PERSIST_GROUP_MAX_REGS];     // Saved values
  uint32_t last_save_ms;             // Timestamp of last save
  uint8_t reserved[3];               // Alignment
} PersistGroup;

typedef struct __attribute__((packed)) {
  uint8_t enabled;                   // Persistence system enabled
  uint8_t group_count;               // Number of active groups (0-8)
  PersistGroup groups[PERSIST_MAX_GROUPS];  // Persistence groups
  uint8_t auto_load_enabled;         // Auto-load on boot enabled (v4.3.0)
  uint8_t auto_load_group_ids[7];    // Group IDs to auto-load (1-8, 0=unused)
} PersistentRegisterData;

/* ============================================================================
 * WATCHDOG MONITOR STATE (v4.0+)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;                   // Watchdog enabled
  uint32_t timeout_ms;               // Timeout (default 30000 = 30s)
  uint32_t reboot_counter;           // Persistent reboot count
  uint32_t last_reset_reason;        // ESP_RST_REASON enum
  char last_error[128];              // Last error message
  uint32_t last_reboot_uptime_ms;    // Uptime before last reboot
  uint8_t reserved[8];
} WatchdogState;

/* ============================================================================
 * HTTP REST API CONFIGURATION (v6.0.0+)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;                              // HTTP server enabled (1) or disabled (0)
  uint16_t port;                                // HTTP port (default 80)
  uint8_t auth_enabled;                         // Basic auth enabled (1) or disabled (0)
  char username[HTTP_AUTH_USERNAME_MAX_LEN];    // Basic auth username
  char password[HTTP_AUTH_PASSWORD_MAX_LEN];    // Basic auth password
  uint8_t api_enabled;                          // API endpoints enabled (1) or disabled (0)
  uint8_t priority;                             // Task priority: 0=LOW, 1=NORMAL, 2=HIGH
  uint8_t tls_enabled;                          // HTTPS/TLS via custom wrapper (FEAT-016)
  uint8_t reserved[3];                          // Alignment/future use
} HttpConfig;

/* ============================================================================
 * ETHERNET CONFIGURATION (v6.1.0+ W5500)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;                              // Ethernet enabled (1) or disabled (0)
  uint8_t dhcp_enabled;                         // 1 = DHCP, 0 = static IP
  uint32_t static_ip;                           // Static IP address (network byte order)
  uint32_t static_gateway;                      // Gateway IP
  uint32_t static_netmask;                      // Netmask
  uint32_t static_dns;                          // Primary DNS
  char hostname[32];                            // Ethernet-specific hostname (empty = system hostname)
  uint8_t reserved[4];                          // Future use
} EthernetConfig;

/* ============================================================================
 * NETWORK CONFIGURATION (v3.0+)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  uint8_t enabled;                              // Wi-Fi enabled (1) or disabled (0)
  uint8_t dhcp_enabled;                         // 1 = DHCP, 0 = static IP
  char ssid[WIFI_SSID_MAX_LEN];                 // Wi-Fi network name
  char password[WIFI_PASSWORD_MAX_LEN];         // Wi-Fi password (WPA2)

  // Static IP configuration (used if dhcp_enabled == 0)
  uint32_t static_ip;                           // Static IP address (network byte order)
  uint32_t static_gateway;                      // Gateway IP
  uint32_t static_netmask;                      // Netmask
  uint32_t static_dns;                          // Primary DNS

  // Telnet configuration
  uint8_t telnet_enabled;                       // 1 = Telnet server enabled
  uint16_t telnet_port;                         // Telnet port (default 23)

  // Telnet authentication (v3.1+)
  char telnet_username[32];                     // Telnet username (max 31 chars + null)
  char telnet_password[64];                     // Telnet password (max 63 chars + null)

  // HTTP REST API configuration (v6.0.0+)
  HttpConfig http;                              // HTTP server configuration

  // Wi-Fi power management (v6.0.4+)
  uint8_t wifi_power_save;                      // 0 = OFF (fast response), 1 = ON (low power)

  // W5500 Ethernet configuration (v6.1.0+)
  EthernetConfig ethernet;                      // Ethernet configuration

  // Reserved for future (SSH, mDNS, etc.)
  uint8_t reserved[3];                          // Future: SSH, certificates, mDNS (reduced from 4)
} NetworkConfig;

/* ============================================================================
 * MODBUS MASTER CONFIGURATION
 * ============================================================================ */

typedef enum {
  MB_OK = 0,
  MB_TIMEOUT = 1,
  MB_CRC_ERROR = 2,
  MB_EXCEPTION = 3,
  MB_MAX_REQUESTS_EXCEEDED = 4,
  MB_NOT_ENABLED = 5,
  MB_INVALID_SLAVE = 6,       // BUG-084: Invalid slave ID (not 1-247)
  MB_INVALID_ADDRESS = 7      // BUG-085: Invalid address (not 0-65535)
} mb_error_code_t;

typedef struct {
  bool enabled;                  // Master enabled/disabled
  uint32_t baudrate;            // 9600, 19200, 38400, 57600, 115200
  uint8_t parity;               // 0=none, 1=even, 2=odd
  uint8_t stop_bits;            // 1 or 2
  uint16_t timeout_ms;          // Response timeout (default: 500ms)
  uint16_t inter_frame_delay;   // Delay between requests (default: 10ms)
  uint8_t max_requests_per_cycle; // Max requests per ST execution (default: 10)

  // Runtime statistics
  uint32_t total_requests;      // Total requests sent
  uint32_t successful_requests; // Successful responses
  uint32_t timeout_errors;      // Timeout count
  uint32_t crc_errors;          // CRC error count
  uint32_t exception_errors;    // Modbus exception count
} modbus_master_config_t;

/* ============================================================================
 * MODBUS SLAVE CONFIGURATION
 * ============================================================================ */

typedef struct {
  bool enabled;                  // Slave enabled/disabled
  uint8_t slave_id;              // Modbus slave ID (1-247)
  uint32_t baudrate;            // 9600, 19200, 38400, 57600, 115200
  uint8_t parity;               // 0=none, 1=even, 2=odd
  uint8_t stop_bits;            // 1 or 2
  uint16_t inter_frame_delay;   // Inter-frame delay (default: 10ms)

  // Runtime statistics
  uint32_t total_requests;      // Total requests received
  uint32_t successful_requests; // Successful responses
  uint32_t crc_errors;          // CRC error count
  uint32_t exception_errors;    // Modbus exception count
} modbus_slave_config_t;

/* ============================================================================
 * PERSISTENT CONFIGURATION (EEPROM/NVS)
 * ============================================================================ */

typedef struct __attribute__((packed)) {
  // Schema versioning
  uint8_t schema_version;

  // Modbus Slave configuration (v4.4.1+)
  modbus_slave_config_t modbus_slave;

  char hostname[32];                // System hostname (max 31 chars + null)

  // CLI configuration (v3.2+)
  uint8_t remote_echo;              // Enable/disable remote echo (for serial terminals)

  // Network configuration (v3.0+)
  NetworkConfig network;

  // Counters (4 maximum)
  CounterConfig counters[COUNTER_COUNT];

  // Timers (4 maximum)
  TimerConfig timers[TIMER_COUNT];

  // STATIC Register mappings
  uint8_t static_reg_count;
  StaticRegisterMapping static_regs[MAX_DYNAMIC_REGS];

  // DYNAMIC Register mappings
  uint8_t dynamic_reg_count;
  DynamicRegisterMapping dynamic_regs[MAX_DYNAMIC_REGS];

  // STATIC Coil mappings
  uint8_t static_coil_count;
  StaticCoilMapping static_coils[MAX_DYNAMIC_COILS];

  // DYNAMIC Coil mappings
  uint8_t dynamic_coil_count;
  DynamicCoilMapping dynamic_coils[MAX_DYNAMIC_COILS];

  // Variable mappings (GPIO pins + ST variables)
  uint8_t var_map_count;
  VariableMapping var_maps[32];  // Reduced from 64 to fit in NVS (saves ~400 bytes)

  // GPIO2 configuration (heartbeat control)
  uint8_t gpio2_user_mode;  // 0 = heartbeat mode (default), 1 = user mode (GPIO2 available)

  // Persistent register groups (v4.0+)
  PersistentRegisterData persist_regs;

  // ST Logic configuration (v4.1+)
  uint32_t st_logic_interval_ms;  // Execution interval for all ST Logic programs (default: 10ms)

  // Modbus Master configuration (v4.4+)
  modbus_master_config_t modbus_master;

  // Module enable/disable flags (v6.2.0+)
  uint8_t module_flags;  // Bitmask: MODULE_FLAG_* constants

  // Reserved for future features
  uint8_t reserved[7];  // Reserved space for future use

  // CRC checksum (last)
  uint16_t crc16;
} PersistConfig;

typedef struct {
  // Runtime state (not persisted)
  uint8_t wifi_connected;                       // Current Wi-Fi connection status
  uint8_t telnet_client_connected;              // Telnet client connected
  uint32_t wifi_connect_time_ms;                // When last connected
  uint32_t wifi_reconnect_retries;              // Current reconnect attempt count

  // Wi-Fi IP information (DHCP or static)
  uint32_t local_ip;                            // Current local IP (Wi-Fi)
  uint32_t gateway;                             // Current gateway (Wi-Fi)
  uint32_t netmask;                             // Current netmask (Wi-Fi)
  uint32_t dns;                                 // Current DNS (Wi-Fi)

  // Ethernet state (v6.1.0+)
  uint8_t eth_connected;                        // Current Ethernet connection status
  uint32_t eth_local_ip;                        // Current local IP (Ethernet)
  uint32_t eth_gateway;                         // Current gateway (Ethernet)
  uint32_t eth_netmask;                         // Current netmask (Ethernet)
  uint32_t eth_dns;                             // Current DNS (Ethernet)

  // Client socket
  int telnet_socket;                            // Socket descriptor (-1 if none)
} NetworkState;

/* ============================================================================
 * DEBUG FLAGS (RUNTIME, NOT PERSISTED)
 * ============================================================================ */

typedef struct {
  uint8_t all;                // Enable all debug output (master flag)
  uint8_t config_save;        // Show debug when saving config to NVS
  uint8_t config_load;        // Show debug when loading config from NVS
  uint8_t wifi_connect;       // Show debug when connecting WiFi (network_manager, wifi_driver)
  uint8_t network_validate;   // Show debug for network config validation
  uint8_t http_server;        // Show debug for HTTP server operations
  uint8_t http_api;           // Show debug for HTTP API request/response
} DebugFlags;

/* ============================================================================
 * RUNTIME STATE (NOT PERSISTED)
 * ============================================================================ */

typedef struct {
  Counter counters[COUNTER_COUNT];
  Timer timers[TIMER_COUNT];

  // Modbus state
  uint8_t modbus_tx_in_progress;
  uint32_t modbus_last_rx_time;

  // CLI state
  uint8_t cli_active;
  uint32_t cli_last_input_time;
} RuntimeState;

#endif // TYPES_H
