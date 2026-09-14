/**
 * @file config_load.cpp
 * @brief Configuration load from NVS with CRC validation
 *
 * LAYER 6: Persistence
 * Responsibility: Load configuration from NVS and validate integrity
 */

#include "config_load.h"
#include "config_save.h"
#include "constants.h"
#include "mb_async.h"
#include "rbac.h"
#include "analog_driver.h"  // FEAT-034/035/036: analog_io_set_defaults()
#include "debug.h"
#include "debug_flags.h"
#include "network_config.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstddef>
#include <cstring>

// NVS key for storing config
#define NVS_CONFIG_KEY "modbus_cfg"
#define NVS_NAMESPACE  "modbus"

// FEAT-397i (schema 24→25 migration only): byte-for-byte kopi af
// PersistConfig's layout SOM DET VAR foer var_maps blev udvidet fra 32 til
// 64 (dvs. praecis schema ≤24's on-disk-format). Bruges KUN til at genlaese
// den raa NVS-blob under migrationen, saa de gamle bytes tolkes med de
// RIGTIGE (gamle) feltoffsets i stedet for at blive fejlfortolket under det
// nye (nu 416 bytes stoerre midt i structen) layout. Skal IKKE bruges andre
// steder — hold denne synkroniseret med PersistConfig i types.h (minus
// var_maps-stoerrelsen) hvis felter nogensinde tilfoejes FOeR var_maps.
typedef struct __attribute__((packed)) {
  uint8_t schema_version;
  modbus_slave_config_t modbus_slave;
  char hostname[32];
  uint8_t remote_echo;
  NetworkConfig network;
  CounterConfig counters[COUNTER_COUNT];
  TimerConfig timers[TIMER_COUNT];
  uint8_t static_reg_count;
  StaticRegisterMapping static_regs[MAX_DYNAMIC_REGS];
  uint8_t dynamic_reg_count;
  DynamicRegisterMapping dynamic_regs[MAX_DYNAMIC_REGS];
  uint8_t static_coil_count;
  StaticCoilMapping static_coils[MAX_DYNAMIC_COILS];
  uint8_t dynamic_coil_count;
  DynamicCoilMapping dynamic_coils[MAX_DYNAMIC_COILS];
  uint8_t var_map_count;
  VariableMapping var_maps[32];  // Schema ≤24: altid 32, IKKE MAX_VAR_MAPPINGS
  uint8_t gpio2_user_mode;
  PersistentRegisterData persist_regs;
  uint32_t st_logic_interval_ms;
  modbus_master_config_t modbus_master;
  uint8_t module_flags;
  uint8_t modbus_mode;
  uint8_t ao1_mode;
  uint8_t ao2_mode;
  uint8_t modbus_slave_uart;
  uint8_t modbus_master_uart;
  uint8_t uart1_tx_pin;
  uint8_t uart1_rx_pin;
  uint8_t uart1_dir_pin;
  uint8_t uart2_tx_pin;
  uint8_t uart2_rx_pin;
  uint8_t uart2_dir_pin;
  RbacConfig rbac;
  NtpConfig ntp;
  char dashboard_card_order[160];
  char dashboard_card_tabs[256];
  char dashboard_card_hidden[80];
  AnalogInputConfig  analog_ai_v[4];
  AnalogInputConfig  analog_ai_i[4];
  AnalogOutputConfig analog_ao[2];
  uint16_t https_port;
  uint8_t rbac_salt[RBAC_MAX_USERS][16];
  uint8_t http_legacy_salt[16];
  char dashboard_card_custom[80];
  uint8_t http_auth_mode;  // Schema 24+ (FEAT-397h) — findes ikke i schema <24, men det er
                           // harmloest: nvs_get_blob() fylder blot ikke dette sidste byte
                           // for saa gamle blobs, og den almindelige schema-kaede saetter
                           // http_auth_mode eksplicit ved 23→24 alligevel.
  uint16_t crc16;
} PersistConfig_v24_shadow_t;

/**
 * @brief Initialize configuration with factory defaults
 */
static void config_init_defaults(PersistConfig* cfg) {
  memset(cfg, 0, sizeof(PersistConfig));
  cfg->schema_version = CONFIG_SCHEMA_VERSION;  // Current schema version

  // Modbus Slave defaults (v4.4.1+)
  cfg->modbus_slave.enabled = true;
  cfg->modbus_slave.slave_id = 1;
  cfg->modbus_slave.baudrate = 9600;
  cfg->modbus_slave.parity = 0;  // None
  cfg->modbus_slave.stop_bits = 1;
  cfg->modbus_slave.inter_frame_delay = 0;  // 0=auto (t3.5 calculated from baudrate)

  strncpy(cfg->hostname, "modbus-esp32", 31);  // Default hostname (v3.2+)
  cfg->hostname[31] = '\0';
  cfg->remote_echo = 1;  // Default: echo ON (v3.2+)

  // Initialize persistent register system (v4.0+)
  memset(&cfg->persist_regs, 0, sizeof(PersistentRegisterData));
  cfg->persist_regs.enabled = 0;  // Disabled by default
  cfg->persist_regs.group_count = 0;

  // ST Logic configuration (v4.1+)
  cfg->st_logic_interval_ms = 10;  // Default: 10ms execution interval

  // Modbus Master configuration (v4.4+)
  cfg->modbus_master.enabled = false;  // Disabled by default
  cfg->modbus_master.baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE;  // 9600
  cfg->modbus_master.parity = MODBUS_MASTER_DEFAULT_PARITY;  // 0 (none)
  cfg->modbus_master.stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS;  // 1
  cfg->modbus_master.timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT;  // 500ms
  cfg->modbus_master.inter_frame_delay = 0;  // 0=auto (t3.5 calculated from baudrate)
  cfg->modbus_master.max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS;  // 10
  cfg->modbus_master.cache_ttl_ms = 0;  // 0 = never expire (default)
  cfg->modbus_master.cache_max_entries = MB_CACHE_MAX_ENTRIES_DEFAULT;  // 32
  cfg->modbus_master.queue_max_size = MB_ASYNC_QUEUE_SIZE_DEFAULT;     // 16
  cfg->modbus_master.total_requests = 0;
  cfg->modbus_master.successful_requests = 0;
  cfg->modbus_master.timeout_errors = 0;
  cfg->modbus_master.crc_errors = 0;
  cfg->modbus_master.exception_errors = 0;

  // modbus_master2/uart2_role (schema 29): RESERVERET, IKKE LAENGERE AKTIVT
  // BRUGT — FEAT-408 (Modbus Master #2) blev rullet tilbage efter et
  // hardware-blocker-fund, se BUGS_INDEX.md. Initialiseres stadig til
  // fornuftige (ikke-nul) vaerdier for konsistens, selvom intet laeser dem.
  cfg->modbus_master2.enabled = false;
  cfg->modbus_master2.baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE;
  cfg->modbus_master2.parity = MODBUS_MASTER_DEFAULT_PARITY;
  cfg->modbus_master2.stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS;
  cfg->modbus_master2.timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT;
  cfg->modbus_master2.inter_frame_delay = 0;
  cfg->modbus_master2.max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS;
  cfg->modbus_master2.cache_ttl_ms = 0;
  cfg->modbus_master2.cache_max_entries = MB_CACHE_MAX_ENTRIES_DEFAULT;
  cfg->modbus_master2.queue_max_size = MB_ASYNC_QUEUE_SIZE_DEFAULT;
  cfg->modbus_master2.total_requests = 0;
  cfg->modbus_master2.successful_requests = 0;
  cfg->modbus_master2.timeout_errors = 0;
  cfg->modbus_master2.crc_errors = 0;
  cfg->modbus_master2.exception_errors = 0;
  cfg->uart2_role = 0;  // Fra

  // FEAT-409 (schema 30): Modbus Expansion Board-registrering — tom liste
  // som fabriksdefault, samme "zero-init er korrekt default" moenster som
  // acl_rules/rbac.users ovenfor.
  cfg->expansion_board_count = 0;
  memset(cfg->expansion_boards, 0, sizeof(cfg->expansion_boards));

  // Modbus mode (v7.2.0+ single-transceiver support)
  cfg->modbus_mode = MODBUS_MODE_SLAVE;   // Default: slave mode

  // Analog output mode (v7.2.0+ ES32D26 AO1/AO2)
  cfg->ao1_mode = AO_MODE_VOLTAGE;        // Default: 0-10V
  cfg->ao2_mode = AO_MODE_VOLTAGE;        // Default: 0-10V

  // Analog I/O defaults (FEAT-034/035/036, schema 20+)
  // Register-adresser: Vi1-4 = HR 0-7, Ii1-4 = HR 8-15, AO1-2 = HR 16-17
  // (fri i dag — counters bruger 100-179, timere 180-199, ST Logic 200-235/IR200-251)
  analog_io_set_defaults(cfg);

  // Dedikeret HTTPS-port (BUG-350, schema 21+) — se PersistConfig i types.h
  cfg->https_port = HTTPS_SERVER_PORT;

  // HTTP auth-metode (FEAT-397h, schema 24+) — brugerens eksplicitte valg:
  // Bearer med det samme, ogsaa for et frisk fabriksinstall (ikke kun
  // migrerede enheder), se schema 23→24-migrationen laengere nede.
  cfg->http_auth_mode = HTTP_AUTH_MODE_BEARER;

  // UART selection defaults (board-dependent)
#if defined(BOARD_ES32D26)
  cfg->modbus_slave_uart = 2;             // ES32D26: UART2 (Serial2) on GPIO1/3
  cfg->modbus_master_uart = 2;            // ES32D26: UART2 (shared transceiver)
#else
  cfg->modbus_slave_uart = 1;             // Other boards: UART1 (Serial1) on GPIO4/5
  cfg->modbus_master_uart = 1;            // Other boards: UART1 (Serial1) on GPIO25/26
#endif

  // UART pin config: 0xFF = use board defaults from constants.h
  cfg->uart1_tx_pin = 0xFF;
  cfg->uart1_rx_pin = 0xFF;
  cfg->uart1_dir_pin = 0xFF;
  cfg->uart2_tx_pin = 0xFF;
  cfg->uart2_rx_pin = 0xFF;
  cfg->uart2_dir_pin = 0xFF;

  // RBAC defaults: disabled (legacy single-user mode)
  memset(&cfg->rbac, 0, sizeof(RbacConfig));

  // NTP defaults (v7.8.1)
  cfg->ntp.enabled = 0;
  strncpy(cfg->ntp.server, "pool.ntp.org", sizeof(cfg->ntp.server) - 1);
  strncpy(cfg->ntp.timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(cfg->ntp.timezone) - 1);
  cfg->ntp.sync_interval_min = 60;

  // Dashboard card order defaults (v7.8.4.2) - empty = default order
  memset(cfg->dashboard_card_order, 0, sizeof(cfg->dashboard_card_order));

  // Dashboard tab assignments + hidden cards (v7.9.6.8) - empty = defaults
  memset(cfg->dashboard_card_tabs, 0, sizeof(cfg->dashboard_card_tabs));
  memset(cfg->dashboard_card_hidden, 0, sizeof(cfg->dashboard_card_hidden));

  // Initialize network config with defaults (v3.0+)
  network_config_init_defaults(&cfg->network);

  // BUG-352: network_config_init_defaults() satte network.http.password til
  // klartekst "modbus123" (den kender kun NetworkConfig, ikke det omkringliggende
  // PersistConfig hvor saltet bor) — hash den straks her, saa et fabriksnyt
  // install ALDRIG har et klartekst-password liggende, heller ikke midlertidigt.
  rbac_hash_and_store_legacy_password(cfg, "modbus123");

  // Initialize all GPIO/ST-binding mappings as unused (FEAT-397i: 64 slots)
  for (uint8_t i = 0; i < MAX_VAR_MAPPINGS; i++) {
    cfg->var_maps[i].input_reg = 65535;
    cfg->var_maps[i].output_reg = 65535;
    cfg->var_maps[i].associated_counter = 0xff;
    cfg->var_maps[i].associated_timer = 0xff;
    cfg->var_maps[i].source_type = 0xff;  // Mark as unused
    cfg->var_maps[i].input_type = 0;      // Default: Holding Register
    cfg->var_maps[i].output_type = 0;     // Default: Holding Register
  }
  cfg->var_map_count = 0;  // No mappings in default config
}

bool config_load_from_nvs(PersistConfig* out) {
  if (out == NULL) {
    debug_println("ERROR: config_load_from_nvs - NULL output");
    return false;
  }

  DebugFlags* dbg = debug_flags_get();
  if (dbg->config_load) {
    debug_println("[LOAD_START] Loading config from NVS...");
  }

  // Open NVS
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    debug_println("CONFIG LOAD: NVS namespace not found, using defaults");
    config_init_defaults(out);
    return true;
  } else if (err != ESP_OK) {
    debug_print("ERROR: NVS open failed: ");
    debug_print_uint(err);
    debug_println(", using defaults");
    config_init_defaults(out);
    return true;
  }

  // Read config blob from NVS
  size_t required_size = sizeof(PersistConfig);
  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] Reading blob, size=");
    debug_print_uint(required_size);
    debug_println("");
  }
  err = nvs_get_blob(handle, NVS_CONFIG_KEY, out, &required_size);
  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] nvs_get_blob returned err=");
    debug_print_uint(err);
    debug_print(" required_size=");
    debug_print_uint(required_size);
    debug_println("");
  }
  nvs_close(handle);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    debug_println("CONFIG LOAD: Config key not found, using defaults");
    config_init_defaults(out);
    return true;
  } else if (err != ESP_OK) {
    debug_print("ERROR: NVS get_blob failed: ");
    debug_print_uint(err);
    debug_println(", using defaults");
    config_init_defaults(out);
    return true;
  }

  if (dbg->config_load) {
    debug_print("[LOAD_DEBUG] After nvs_get_blob: var_map_count=");
    debug_print_uint(out->var_map_count);
    debug_print(" schema_version=");
    debug_print_uint(out->schema_version);
    debug_print(" crc16=");
    debug_print_uint(out->crc16);
    debug_println("");

    // DEBUG: Dump first few bytes of loaded config
    debug_println("[LOAD_DEBUG] First 20 bytes of loaded data:");
    uint8_t* data = (uint8_t*)out;
    for (int i = 0; i < 20; i++) {
      debug_print("  [");
      debug_print_uint(i);
      debug_print("]=0x");
      if (data[i] < 16) debug_print("0");
      debug_print_uint(data[i]);
      debug_print(" ");
    }
    debug_println("");
  }

  // BUG-351: satt naar en migration reelt koerer nedenfor. En migration
  // AENDRER data (nye standardvaerdier, evt. forskudte bytes for felter
  // tilfoejet i halen af PersistConfig) — den gamle `stored_crc` (laest
  // direkte fra NVS-blob'en, beregnet foer migrationen) matcher derfor
  // ALDRIG en frisk genberegning af CRC paa den migrerede struct. Kommentaren
  // "CRC will be invalid, but we'll recalculate on next save" har staaet her
  // siden schema 7→8 uden at det rent faktisk blev implementeret som en
  // undtagelse i CRC-tjekket nedenfor — konsekvensen var at ENHVER
  // struct-aendrende migration ramte "CRC mismatch, CORRUPTED, REJECTING" og
  // nulstillede HELE configen (WiFi, RBAC, alt) til fabriksdefault paa
  // foerste boot efter en schema-bump. Fundet ved gennemgang af BUG-350
  // (dedikeret HTTPS-port, schema 20→21) FOER det ramte produktion.
  bool migrated = false;

  // Validate schema version (MUST be checked before CRC to prevent struct misalignment)
  if (out->schema_version != CONFIG_SCHEMA_VERSION) {
    migrated = true;

    // VAR_MAPS RE-POSITIONERING (FEAT-397i, schema <25 → 25): var_maps[] blev
    // udvidet fra 32 til 64 entries, og sidder MIDT i PersistConfig — den
    // eneste gang det er sket i denne struct (alle tidligere schema-udvidelser
    // har ALTID kun tilfoejet felter HELT TIL SIDST, lige foer crc16, se
    // https_port/rbac_salt/dashboard_card_custom-kommentarerne i types.h).
    // nvs_get_blob() ovenfor (linje ~174) laeste den lagrede (gamle, kortere)
    // blob raat, byte-for-byte, ind i DENNE struct's NYE (stoerre) layout —
    // det betyder at alt fra og med gpio2_user_mode og frem allerede sidder
    // 32*13=416 bytes for TIDLIGT i `out` lige nu (midt inde i det som i det
    // nye layout er var_maps[32..63]), FOER en eneste linje af den almindelige
    // schema-kaede nedenfor har rørt noget. Denne fejl skal derfor rettes HER,
    // foer schema_version-kaeden starter, ikke som et normalt kaede-trin —
    // ellers ville 7→8...23→24-migrationerne nedenfor arbejde videre paa
    // allerede-forskudt/forkert data (fx https_port, rbac, ntp, dashboard-felter).
    // Findes ved at genlaese den samme raa blob ind i en gammel-formet
    // skyggestruct (var_maps stadig 32, praecis schema ≤24's layout), og
    // derefter kopiere hvert felt til dets korrekte NYE position i `out`.
    if (out->schema_version < 25) {
      nvs_handle_t migrate_handle;
      if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &migrate_handle) == ESP_OK) {
        PersistConfig_v24_shadow_t old;
        memset(&old, 0, sizeof(old));  // Ældre (kortere) lagrede schemas efterlader "halen" nul, ikke stak-skrammel
        size_t old_size = sizeof(PersistConfig_v24_shadow_t);
        esp_err_t migrate_err = nvs_get_blob(migrate_handle, NVS_CONFIG_KEY, &old, &old_size);
        nvs_close(migrate_handle);

        if (migrate_err == ESP_OK) {
          // var_maps[0..31] uaendret position/indhold i begge layouts — kopier direkte.
          memcpy(out->var_maps, old.var_maps, sizeof(old.var_maps));
          out->var_map_count = old.var_map_count;

          // Nye slots [32..63]: eksplicit "unused" (source_type=0xff), IKKE
          // blot nul — 0 er MAPPING_SOURCE_GPIO, saa nul-initialiserede slots
          // ville fejlagtigt fremstaa som aktive GPIO-mappinger paa pin 0.
          for (uint8_t i = old.var_map_count; i < MAX_VAR_MAPPINGS; i++) {
            memset(&out->var_maps[i], 0, sizeof(VariableMapping));
            out->var_maps[i].input_reg = 65535;
            out->var_maps[i].output_reg = 65535;
            out->var_maps[i].associated_counter = 0xff;
            out->var_maps[i].associated_timer = 0xff;
            out->var_maps[i].source_type = 0xff;
            out->var_maps[i].input_type = 0;
            out->var_maps[i].output_type = 0;
          }

          // "Halen" (gpio2_user_mode → http_auth_mode, foer crc16): identisk
          // indbyrdes layout i begge structs, kun den ABSOLUTTE offset differerer
          // (forskudt af de 32 nye var_maps-entries) — een sammenhaengende
          // memcpy fra skyggestructens hale til den nye structs hale er derfor
          // korrekt og fuldstaendig. Felter der ikke fandtes i det oprindelige
          // (endnu aeldre) lagrede schema staar nu som nul her, og bliver
          // eksplicit sat til deres rigtige default af den almindelige
          // schema-kaede nedenfor (fx https_port ved 20→21, akkurat som foer
          // denne aendring).
          size_t tail_size = sizeof(PersistConfig_v24_shadow_t) - offsetof(PersistConfig_v24_shadow_t, gpio2_user_mode);
          memcpy(&out->gpio2_user_mode, &old.gpio2_user_mode, tail_size);

          debug_println("CONFIG LOAD: var_maps + hale gen-positioneret korrekt (FEAT-397i)");
        } else {
          debug_print("ERROR: Kunne ikke genlaese raa NVS-blob til var_maps-migration, err=");
          debug_print_uint(migrate_err);
          debug_println(" - falder tilbage til fabriksdefault for hele configen");
          config_init_defaults(out);
          return true;
        }
      } else {
        debug_println("ERROR: Kunne ikke aabne NVS til var_maps-migrationens gen-laesning - falder tilbage til fabriksdefault");
        config_init_defaults(out);
        return true;
      }
    }

    // Schema migration support (v7 → v8 → v9)
    if (out->schema_version == 7) {
      debug_println("CONFIG LOAD: Migrating schema 7 → 8 (adding persist_regs)");

      // Initialize new persist_regs field with defaults
      memset(&out->persist_regs, 0, sizeof(PersistentRegisterData));
      out->persist_regs.enabled = 0;
      out->persist_regs.group_count = 0;

      // Update schema version
      out->schema_version = 8;

      debug_println("CONFIG LOAD: Migration 7→8 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save

      // Fall through to v8→v9 migration
    }

    if (out->schema_version == 8) {
      debug_println("CONFIG LOAD: Migrating schema 8 → 9 (STATIC register multi-type support)");

      // Migrate STATIC register mappings (old format → new format)
      // OLD v8: struct { uint16_t register_address; uint16_t static_value; }
      // NEW v9: struct { uint16_t register_address; uint8_t value_type; uint8_t reserved; union { uint16_t value_16; uint32_t value_32; float value_real; }; }

      // The old struct was 4 bytes, new struct is 8 bytes (with union)
      // We need to convert in-place from the END of the array backwards to avoid overwriting

      typedef struct __attribute__((packed)) {
        uint16_t register_address;
        uint16_t static_value;
      } OldStaticRegisterMapping;

      OldStaticRegisterMapping* old_regs = (OldStaticRegisterMapping*)out->static_regs;
      uint8_t count = out->static_reg_count;

      if (count > MAX_DYNAMIC_REGS) {
        count = MAX_DYNAMIC_REGS;  // Safety clamp
      }

      // Convert from END to START (to avoid overwriting data)
      for (int i = count - 1; i >= 0; i--) {
        uint16_t addr = old_regs[i].register_address;
        uint16_t value = old_regs[i].static_value;

        // Write new format
        out->static_regs[i].register_address = addr;
        out->static_regs[i].value_type = MODBUS_TYPE_UINT;  // Default to UINT
        out->static_regs[i].reserved = 0;
        out->static_regs[i].value_16 = value;
      }

      // Update schema version
      out->schema_version = 9;

      debug_println("CONFIG LOAD: Migration 8→9 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
      // Fall through to v9→v10 migration
    }

    if (out->schema_version == 9) {
      debug_println("CONFIG LOAD: Migrating schema 9 → 10 (HTTP REST API support)");

      // Initialize HTTP config with defaults (new field in NetworkConfig)
      out->network.http.enabled = 1;                  // HTTP enabled by default
      out->network.http.port = HTTP_SERVER_PORT;      // Port 80
      out->network.http.auth_enabled = 0;             // No auth by default
      strncpy(out->network.http.username, "admin", sizeof(out->network.http.username) - 1);
      out->network.http.username[sizeof(out->network.http.username) - 1] = '\0';
      strncpy(out->network.http.password, "modbus123", sizeof(out->network.http.password) - 1);
      out->network.http.password[sizeof(out->network.http.password) - 1] = '\0';
      out->network.http.tls_enabled = 0;
      out->network.http.api_enabled = 1;              // API enabled by default
      out->network.http.priority = 1;                 // NORMAL priority
      out->network.http.sse_port = 0;                 // 0 = auto (main port + 1)
      out->network.http.sse_enabled = 1;               // SSE enabled by default
      out->network.http.sse_max_clients = 3;            // Default 3 clients
      out->network.http.sse_check_interval_ms = 100;    // 10 Hz change detection
      out->network.http.sse_heartbeat_ms = 15000;       // 15s heartbeat

      // Update schema version
      out->schema_version = 10;

      debug_println("CONFIG LOAD: Migration 9→10 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
      // Fall through to v10→v11 migration
    }

    if (out->schema_version == 10) {
      debug_println("CONFIG LOAD: Migrating schema 10 → 11 (W5500 Ethernet support)");

      // Initialize Ethernet config with defaults (new field in NetworkConfig)
      out->network.ethernet.enabled = 0;              // Disabled by default
      out->network.ethernet.dhcp_enabled = 1;          // DHCP by default
      out->network.ethernet.static_ip = 0;
      out->network.ethernet.static_gateway = 0;
      out->network.ethernet.static_netmask = 0;
      out->network.ethernet.static_dns = 0;
      memset(out->network.ethernet.hostname, 0, sizeof(out->network.ethernet.hostname));
      memset(out->network.ethernet.reserved, 0, sizeof(out->network.ethernet.reserved));

      // Update schema version
      out->schema_version = 11;

      debug_println("CONFIG LOAD: Migration 10→11 complete");
      // Note: CRC will be invalid, but we'll recalculate on next save
    }

    if (out->schema_version == 11) {
      debug_println("CONFIG LOAD: Migrating schema 11 → 12 (SSE config fields)");

      out->network.http.sse_enabled = 1;               // SSE enabled by default
      out->network.http.sse_max_clients = 3;            // Default 3 clients
      out->network.http.sse_check_interval_ms = 100;    // 10 Hz change detection
      out->network.http.sse_heartbeat_ms = 15000;       // 15s heartbeat

      out->schema_version = 12;

      debug_println("CONFIG LOAD: Migration 11→12 complete");
    }

    if (out->schema_version == 12) {
      debug_println("CONFIG LOAD: Migrating schema 12 → 13 (modbus_mode + ao_mode)");

      out->modbus_mode = MODBUS_MODE_SLAVE;   // Default: slave (backward compatible)
      out->ao1_mode = AO_MODE_VOLTAGE;        // Default: 0-10V
      out->ao2_mode = AO_MODE_VOLTAGE;        // Default: 0-10V
      // UART selection defaults
#if defined(BOARD_ES32D26)
      out->modbus_slave_uart = 2;             // ES32D26: UART2
      out->modbus_master_uart = 2;            // ES32D26: UART2 (shared)
#else
      out->modbus_slave_uart = 1;             // Other: UART1
      out->modbus_master_uart = 1;            // Other: UART1
#endif

      out->schema_version = 13;

      debug_println("CONFIG LOAD: Migration 12→13 complete");
    }

    if (out->schema_version == 13) {
      debug_println("CONFIG LOAD: Migrating schema 13 → 14 (UART pin config)");

      // 0xFF = use board default pins from constants.h
      out->uart1_tx_pin = 0xFF;
      out->uart1_rx_pin = 0xFF;
      out->uart1_dir_pin = 0xFF;
      out->uart2_tx_pin = 0xFF;
      out->uart2_rx_pin = 0xFF;
      out->uart2_dir_pin = 0xFF;

      out->schema_version = 14;

      debug_println("CONFIG LOAD: Migration 13→14 complete");
    }

    if (out->schema_version == 14) {
      debug_println("CONFIG LOAD: Migrating schema 14 → 15 (RBAC multi-user)");

      // Migrate legacy single-user to RBAC
      rbac_migrate_legacy(out);

      out->schema_version = 15;

      debug_println("CONFIG LOAD: Migration 14→15 complete");
    }

    if (out->schema_version == 15) {
      debug_println("CONFIG LOAD: Migrating schema 15 → 16 (NTP time sync)");

      // Initialize NTP config with defaults
      out->ntp.enabled = 0;
      strncpy(out->ntp.server, "pool.ntp.org", sizeof(out->ntp.server) - 1);
      out->ntp.server[sizeof(out->ntp.server) - 1] = '\0';
      strncpy(out->ntp.timezone, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(out->ntp.timezone) - 1);
      out->ntp.timezone[sizeof(out->ntp.timezone) - 1] = '\0';
      out->ntp.sync_interval_min = 60;

      out->schema_version = 16;

      debug_println("CONFIG LOAD: Migration 15→16 complete");
    }

    if (out->schema_version == 16) {
      debug_println("CONFIG LOAD: Migrating schema 16 → 17 (dashboard layout)");

      // Initialize dashboard card order with empty string (default order)
      memset(out->dashboard_card_order, 0, sizeof(out->dashboard_card_order));

      out->schema_version = 17;

      debug_println("CONFIG LOAD: Migration 16→17 complete");
    }

    if (out->schema_version == 17) {
      debug_println("CONFIG LOAD: Migrating schema 17 → 18 (dashboard tabs)");

      memset(out->dashboard_card_tabs, 0, sizeof(out->dashboard_card_tabs));
      memset(out->dashboard_card_hidden, 0, sizeof(out->dashboard_card_hidden));

      out->schema_version = 18;

      debug_println("CONFIG LOAD: Migration 17→18 complete");
    }

    if (out->schema_version == 18) {
      debug_println("CONFIG LOAD: Migrating schema 18 → 19 (cache/queue size)");

      out->modbus_master.cache_max_entries = MB_CACHE_MAX_ENTRIES_DEFAULT;
      out->modbus_master.queue_max_size = MB_ASYNC_QUEUE_SIZE_DEFAULT;

      out->schema_version = 19;

      debug_println("CONFIG LOAD: Migration 18→19 complete");
    }

    if (out->schema_version == 19) {
      debug_println("CONFIG LOAD: Migrating schema 19 → 20 (analog I/O, FEAT-034/035/036)");

      // BUG-339: nye felter i PersistConfig KRAEVER en migrationsblok her —
      // uden den forbliver schema_version uaendret, ingen migration triggres,
      // og CRC-tjekket fejler paa den forskudte struct -> hele configen
      // (inkl. WiFi) nulstilles til fabriksdefault ved naeste boot.
      analog_io_set_defaults(out);

      out->schema_version = 20;

      debug_println("CONFIG LOAD: Migration 19→20 complete");
    }

    if (out->schema_version == 20) {
      debug_println("CONFIG LOAD: Migrating schema 20 → 21 (dedikeret HTTPS-port, BUG-350)");

      // BUG-339/BUG-350: se analog I/O-migrationen ovenfor for hvorfor denne
      // blok er ufravigelig. https_port var 0 i den raa NVS-blob (felt
      // fandtes ikke foer schema 21) — 0 er ikke en gyldig lytte-port, saa
      // sæt den eksplicit til default her i stedet for at lade den staa nul.
      out->https_port = HTTPS_SERVER_PORT;

      out->schema_version = 21;

      debug_println("CONFIG LOAD: Migration 20→21 complete");
    }

    if (out->schema_version == 21) {
      debug_println("CONFIG LOAD: Migrating schema 21 → 22 (password-hashing, BUG-352)");

      // BUG-339/BUG-350/BUG-352: se migrationerne ovenfor. network.http.password
      // og hvert AKTIVE rbac.users[i].password staar her stadig i klartekst
      // (saadan har det vaeret siden schema 9/15) — hash dem NU, med et frisk
      // salt hver, og overskriv feltet med den 32-byte hash. Tomt password
      // (auth_enabled=0 / ingen reel adgangskode sat) hashes stadig — harmloest,
      // da rbac_legacy_auth()/rbac_authenticate() aldrig naar frem til
      // hash-sammenligningen naar auth slet ikke er aktiveret.
      if (out->network.http.password[0] != '\0') {
        rbac_hash_and_store_legacy_password(out, out->network.http.password);
      }
      for (int i = 0; i < RBAC_MAX_USERS; i++) {
        if (!out->rbac.users[i].active) continue;
        char old_plain[RBAC_PASSWORD_MAX + 1];
        strncpy(old_plain, out->rbac.users[i].password, RBAC_PASSWORD_MAX);
        old_plain[RBAC_PASSWORD_MAX] = '\0';
        rbac_generate_salt(out->rbac_salt[i]);
        rbac_hash_password(old_plain, out->rbac_salt[i], (uint8_t *)out->rbac.users[i].password);
      }

      out->schema_version = 22;

      debug_println("CONFIG LOAD: Migration 21→22 complete");
    }

    if (out->schema_version == 22) {
      debug_println("CONFIG LOAD: Migrating schema 22 → 23 (dashboard Custom-fane medlemsskab)");

      // Nyt felt, fandtes ikke foer schema 23 — staar som nul-bytes i den
      // raa NVS-blob (partial-fill ind i en stoerre struct), hvilket for et
      // char[] allerede ER en tom streng. Ingen reel transformation
      // noedvendig, men saet den eksplicit for tydelighedens skyld/for at
      // vaere konsistent med de oevrige migrationsblokke.
      out->dashboard_card_custom[0] = '\0';

      out->schema_version = 23;

      debug_println("CONFIG LOAD: Migration 22→23 complete");
    }

    if (out->schema_version == 23) {
      debug_println("CONFIG LOAD: Migrating schema 23 → 24 (HTTP auth_mode none/basic/bearer)");

      // Nyt felt, fandtes ikke foer schema 24. Brugerens eksplicitte valg
      // (denne sessions AskUserQuestion): migrerede (allerede-konfigurerede)
      // enheder skal OGSAA gaa direkte til BEARER, ikke bevare Basic som
      // "sikrere default-adfaerd" — samme vaerdi som en frisk fabriksdefault
      // (network_config.cpp). Kun `auth_mode` selv saettes her — `auth_enabled`
      // (og dermed en evt. eksisterende "None"-tilstand) er upaavirket.
      out->http_auth_mode = HTTP_AUTH_MODE_BEARER;

      out->schema_version = 24;

      debug_println("CONFIG LOAD: Migration 23→24 complete");
      // Fall through to v24→v25 migration
    }

    if (out->schema_version == 24) {
      debug_println("CONFIG LOAD: Migrating schema 24 → 25 (var_maps 32 → 64 kapacitet, FEAT-397i)");
      // Selve var_maps-gen-positioneringen er allerede udfoert LAENGERE OPPE
      // (foer denne if-kaede overhovedet startede) — se kommentaren ved
      // "VAR_MAPS RE-POSITIONERING" ovenfor for hvorfor det skal ske foer
      // schema_version-kaeden, ikke som et almindeligt kaede-trin her.
      out->schema_version = 25;
      debug_println("CONFIG LOAD: Migration 24→25 complete");
    }

    if (out->schema_version == 25) {
      debug_println("CONFIG LOAD: Migrating schema 25 → 26 (IP Access Control List, FEAT-399)");

      // Nye felter, fandtes ikke foer schema 26 — tilfoejet HELT TIL SIDST
      // (efter http_auth_mode, foer crc16), saa dette er en simpel append,
      // IKKE en mid-struct-indsaettelse (modsat var_maps ovenfor) — ingen
      // shadow-struct/gen-positionering noedvendig. Staar allerede som nul-
      // bytes i den raa NVS-blob (partial-fill ind i en stoerre struct), men
      // saettes eksplicit her for tydelighedens skyld, samme stil som
      // dashboard_card_custom (schema 22→23). ACL er OFF by default, ogsaa
      // for allerede-konfigurerede enheder — ingen adfaerdsaendring foer
      // brugeren selv aktivt slaar det til.
      out->acl_enabled = 0;
      out->acl_rule_count = 0;
      memset(out->acl_rules, 0, sizeof(out->acl_rules));

      out->schema_version = 26;

      debug_println("CONFIG LOAD: Migration 25→26 complete");
    }

    if (out->schema_version == 26) {
      debug_println("CONFIG LOAD: Migrating schema 26 → 27 (ACL permit/deny action-felt, FEAT-401)");

      // AclRule.action genbruger det tidligere ubrugte "reserved"-byte —
      // ingen struct-stoerrelses- eller PersistConfig-layoutaendring, kun en
      // semantisk omtolkning. Alle EKSISTERENDE persisterede regler betoed
      // udelukkende "bloker" under v1's ordensuafhaengige model — de skal
      // fortsaette med at goere praecis det efter opgraderingen, saa
      // action saettes eksplicit til ACL_ACTION_DENY her (IKKE 0/ALLOW, som
      // det raa "reserved"-byte ellers ville have givet og som ville have
      // vendt enhver eksisterende regels betydning fuldstaendig om).
      for (uint8_t i = 0; i < out->acl_rule_count && i < ACL_MAX_RULES; i++) {
        out->acl_rules[i].action = ACL_ACTION_DENY;
      }

      out->schema_version = 27;

      debug_println("CONFIG LOAD: Migration 26→27 complete");
    }

    if (out->schema_version == 27) {
      debug_println("CONFIG LOAD: Migrating schema 27 → 28 (offentlig statusside kort-liste, FEAT-407)");

      // public_dashboard_cards er tilfoejet HELT TIL SIDST (lige foer crc16,
      // samme "ren append"-moenster som http_auth_mode ved schema 24, IKKE
      // var_maps' mid-struct-udvidelse ved schema 25) — nvs_get_blob()
      // ovenfor efterlader allerede denne "hale" nul for en kortere, aeldre
      // blob, saa dette er reelt en no-op, men saettes eksplicit for klarhed
      // og for at vaere robust mod evt. genbrugt (ikke-nulstillet) buffer.
      memset(out->public_dashboard_cards, 0, sizeof(out->public_dashboard_cards));

      out->schema_version = 28;

      debug_println("CONFIG LOAD: Migration 27→28 complete");
    }

    if (out->schema_version == 28) {
      debug_println("CONFIG LOAD: Migrating schema 28 → 29 (reserveret felt, FEAT-408 rullet tilbage)");

      // modbus_master2/uart2_role: FEAT-408 (Modbus Master #2) blev rullet
      // tilbage efter et hardware-blocker-fund (se BUGS_INDEX.md) — men
      // schema-bumpet og feltet BEVARES, da allerede migrerede enheder har
      // gemt NVS-data i dette layout. Migrationen koeres stadig for enheder
      // der endnu ikke har naaet schema 29, saa layoutet forbliver
      // konsistent paa tvaers af alle enheder uanset opgraderings-historik.
      memset(&out->modbus_master2, 0, sizeof(out->modbus_master2));
      out->modbus_master2.baudrate = MODBUS_MASTER_DEFAULT_BAUDRATE;
      out->modbus_master2.parity = MODBUS_MASTER_DEFAULT_PARITY;
      out->modbus_master2.stop_bits = MODBUS_MASTER_DEFAULT_STOP_BITS;
      out->modbus_master2.timeout_ms = MODBUS_MASTER_DEFAULT_TIMEOUT;
      out->modbus_master2.max_requests_per_cycle = MODBUS_MASTER_DEFAULT_MAX_REQUESTS;
      out->modbus_master2.cache_max_entries = MB_CACHE_MAX_ENTRIES_DEFAULT;
      out->modbus_master2.queue_max_size = MB_ASYNC_QUEUE_SIZE_DEFAULT;
      out->uart2_role = 0;

      out->schema_version = 29;

      debug_println("CONFIG LOAD: Migration 28→29 complete");
    }

    if (out->schema_version == 29) {
      debug_println("CONFIG LOAD: Migrating schema 29 → 30 (FEAT-409: Modbus Expansion Board-registrering)");

      // expansion_board_count/expansion_boards[]: ren append (samme moenster
      // som public_dashboard_cards ved schema 27→28) — nvs_get_blob() ovenfor
      // efterlader allerede denne "hale" nul for en kortere, aeldre blob, saa
      // dette er reelt en no-op, men saettes eksplicit for klarhed og for at
      // vaere robust mod evt. genbrugt (ikke-nulstillet) buffer.
      out->expansion_board_count = 0;
      memset(out->expansion_boards, 0, sizeof(out->expansion_boards));

      out->schema_version = 30;

      debug_println("CONFIG LOAD: Migration 29→30 complete");
    }

    if (out->schema_version == 30) {
      debug_println("CONFIG LOAD: Migrating schema 30 → 31 (FEAT-409c: board_type paa ExpansionBoard)");

      // board_type er ny — enhver allerede-konfigureret board fra schema 30
      // KAN kun have vaeret det ene board-design der eksisterede dengang
      // (EXPANSION_BOARD_TYPE_MODBUS_2CH), saa det er det korrekte,
      // ikke-gaettede backfill-valg her (ikke en tilfaeldig default).
      for (uint8_t i = 0; i < EXPANSION_BOARD_MAX; i++) {
        memset(out->expansion_boards[i].board_type, 0, sizeof(out->expansion_boards[i].board_type));
        if (out->expansion_boards[i].configured) {
          strncpy(out->expansion_boards[i].board_type, EXPANSION_BOARD_TYPE_MODBUS_2CH,
                  sizeof(out->expansion_boards[i].board_type) - 1);
        }
      }

      out->schema_version = 31;

      debug_println("CONFIG LOAD: Migration 30→31 complete");
    }

    if (out->schema_version != CONFIG_SCHEMA_VERSION) {
      debug_print("ERROR: Unsupported schema version (stored=");
      debug_print_uint(out->schema_version);
      debug_print(", current=");
      debug_print_uint(CONFIG_SCHEMA_VERSION);
      debug_println("), reinitializing with defaults");
      config_init_defaults(out);
      return true;
    }
  }

  if (migrated) {
    // BUG-351: en migration har AENDRET dataen (nye defaultvaerdier, evt.
    // forskudte felter) — den gamle stored_crc (fra FOER migrationen) kan
    // aldrig matche en frisk beregning paa den NU migrerede struct, saa det
    // sammenligner vi bevidst ikke imod her (se kommentaren ved `migrated`
    // ovenfor for hvorfor det tidligere gjorde det og nulstillede hele
    // configen). I stedet: stol paa den migrerede data og gem den STRAKS med
    // en frisk, korrekt CRC, saa naeste boot igen kan validere normalt.
    out->crc16 = config_calculate_crc16(out);
    if (dbg->config_load) {
      debug_println("[LOAD_DEBUG] Migreret config - springer CRC-sammenligning over, gemmer frisk CRC");
    }
    if (!config_save_to_nvs(out)) {
      debug_println("WARNING: Kunne ikke gemme migreret config til NVS — forbliver migreret kun i RAM indtil naeste 'save'");
    } else {
      debug_println("CONFIG LOAD: Migreret config gemt til NVS med frisk CRC");
    }
  } else {
    if (dbg->config_load) {
      debug_println("[LOAD_DEBUG] Schema version OK, checking CRC...");
    }

    // Validate CRC
    uint16_t stored_crc = out->crc16;
    uint16_t calculated_crc = config_calculate_crc16(out);

    if (stored_crc != calculated_crc) {
      debug_print("ERROR: CRC mismatch (stored=");
      debug_print_uint(stored_crc);
      debug_print(", calculated=");
      debug_print_uint(calculated_crc);
      debug_print(") - CONFIG CORRUPTED, REJECTING");
      debug_println("");
      debug_println("SECURITY: Corrupt config detected and rejected");
      debug_println("  Reinitializing with factory defaults");
      config_init_defaults(out);
      return false;  // CRITICAL FIX: Return false to indicate load failure
    }
  }

  // BUG-140: Sanitize count fields to prevent out-of-bounds access
  bool sanitized = false;

  if (out->var_map_count > MAX_VAR_MAPPINGS) {
    debug_print("WARN: var_map_count=");
    debug_print_uint(out->var_map_count);
    debug_println(" exceeds max, clamping to MAX_VAR_MAPPINGS");
    out->var_map_count = MAX_VAR_MAPPINGS;
    sanitized = true;
  }

  if (out->persist_regs.group_count > PERSIST_MAX_GROUPS) {
    debug_print("WARN: persist group_count=");
    debug_print_uint(out->persist_regs.group_count);
    debug_println(" exceeds max, clamping to 8");
    out->persist_regs.group_count = PERSIST_MAX_GROUPS;
    sanitized = true;
  }

  if (out->static_reg_count > MAX_DYNAMIC_REGS) {
    debug_print("WARN: static_reg_count=");
    debug_print_uint(out->static_reg_count);
    debug_println(" exceeds max, clamping");
    out->static_reg_count = MAX_DYNAMIC_REGS;
    sanitized = true;
  }

  if (out->static_coil_count > MAX_DYNAMIC_COILS) {
    debug_print("WARN: static_coil_count=");
    debug_print_uint(out->static_coil_count);
    debug_println(" exceeds max, clamping");
    out->static_coil_count = MAX_DYNAMIC_COILS;
    sanitized = true;
  }

  // Print summary
  debug_print("CONFIG LOADED: schema=");
  debug_print_uint(out->schema_version);
  debug_print(", slave_id=");
  debug_print_uint(out->modbus_slave.slave_id);
  debug_print(", baudrate=");
  debug_print_uint(out->modbus_slave.baudrate);
  debug_print(", var_maps=");
  debug_print_uint(out->var_map_count);
  debug_print(", static_regs=");
  debug_print_uint(out->static_reg_count);
  debug_print(", static_coils=");
  debug_print_uint(out->static_coil_count);
  debug_print(", CRC=");
  debug_print_uint(out->crc16);  // BUG-351: altid korrekt her, uanset migreret eller ej
  debug_println(" OK");

  if (sanitized) {
    debug_println("WARN: Config had out-of-bounds values (sanitized)");
  }

  // Debug: Print loaded GPIO mappings
  if (out->var_map_count > 0) {
    debug_println("  Loaded variable mappings:");
    for (uint8_t i = 0; i < out->var_map_count; i++) {
      const VariableMapping* map = &out->var_maps[i];
      debug_print("    [");
      debug_print_uint(i);
      debug_print("] source_type=");
      debug_print_uint(map->source_type);
      debug_print(" gpio_pin=");
      debug_print_uint(map->gpio_pin);
      debug_print(" is_input=");
      debug_print_uint(map->is_input);
      debug_print(" input_reg=");
      debug_print_uint(map->input_reg);
      debug_print(" output_reg=");
      debug_print_uint(map->output_reg);
      debug_println("");
    }
  }

  return true;
}
