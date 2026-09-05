/**
 * @file cli_commands_analog.cpp
 * @brief CLI `set analog` / `show analog` command handlers (FEAT-034/035/036/037)
 */

#include "cli_commands_analog.h"
#include "constants.h"

#if defined(ANALOG_IO_ENABLED)

#include "config_struct.h"
#include "analog_driver.h"
#include "registers.h"
#include "wifi_driver.h"
#include "debug.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * KANAL-OPSLAG (case-insensitiv: vi1-vi4, ii1-ii4, ao1-ao2)
 * ============================================================================ */

typedef enum { ACH_AI_V, ACH_AI_I, ACH_AO, ACH_INVALID } AnalogChGroup;

typedef struct {
  AnalogChGroup group;
  uint8_t idx;        // 0-3 for AI_V/AI_I, 0-1 for AO
  const char *label;  // til visning, fx "Vi1"
} AnalogChRef;

static AnalogChRef analog_resolve_channel(const char *name) {
  AnalogChRef ref = { ACH_INVALID, 0, "?" };
  if (!name) return ref;

  static const char *V_NAMES[4]  = { "VI1", "VI2", "VI3", "VI4" };
  static const char *I_NAMES[4]  = { "II1", "II2", "II3", "II4" };
  static const char *AO_NAMES[2] = { "AO1", "AO2" };
  static const char *V_LABELS[4]  = { "Vi1", "Vi2", "Vi3", "Vi4" };
  static const char *I_LABELS[4]  = { "Ii1", "Ii2", "Ii3", "Ii4" };
  static const char *AO_LABELS[2] = { "AO1", "AO2" };

  for (uint8_t i = 0; i < 4; i++) {
    if (!strcasecmp(name, V_NAMES[i])) { ref.group = ACH_AI_V; ref.idx = i; ref.label = V_LABELS[i]; return ref; }
    if (!strcasecmp(name, I_NAMES[i])) { ref.group = ACH_AI_I; ref.idx = i; ref.label = I_LABELS[i]; return ref; }
  }
  for (uint8_t i = 0; i < 2; i++) {
    if (!strcasecmp(name, AO_NAMES[i])) { ref.group = ACH_AO; ref.idx = i; ref.label = AO_LABELS[i]; return ref; }
  }
  return ref;
}

/* ============================================================================
 * SET ANALOG
 * ============================================================================ */

void cli_cmd_set_analog(uint8_t argc, char **argv) {
  // set analog <kanal> enabled on|off
  // set analog <kanal> scale <float>
  // set analog <kanal> offset <float>
  if (argc < 3) {
    debug_println("SET ANALOG: mangler parametre");
    debug_println("  Brug: set analog <vi1-4|ii1-4|ao1-2> enabled on|off");
    debug_println("        set analog <kanal> scale <tal>");
    debug_println("        set analog <kanal> offset <tal>");
    return;
  }

  AnalogChRef ref = analog_resolve_channel(argv[0]);
  if (ref.group == ACH_INVALID) {
    debug_printf("SET ANALOG: ukendt kanal '%s' (gyldige: vi1-4, ii1-4, ao1-2)\n", argv[0]);
    return;
  }

  const char *param = argv[1];
  const char *value = argv[2];

  bool *p_enabled = NULL;
  float *p_scale = NULL;
  float *p_offset = NULL;

  if (ref.group == ACH_AI_V) {
    p_enabled = &g_persist_config.analog_ai_v[ref.idx].enabled;
    p_scale   = &g_persist_config.analog_ai_v[ref.idx].scale;
    p_offset  = &g_persist_config.analog_ai_v[ref.idx].offset;
  } else if (ref.group == ACH_AI_I) {
    p_enabled = &g_persist_config.analog_ai_i[ref.idx].enabled;
    p_scale   = &g_persist_config.analog_ai_i[ref.idx].scale;
    p_offset  = &g_persist_config.analog_ai_i[ref.idx].offset;
  } else {
    p_enabled = &g_persist_config.analog_ao[ref.idx].enabled;
    p_scale   = &g_persist_config.analog_ao[ref.idx].scale;
    p_offset  = &g_persist_config.analog_ao[ref.idx].offset;
  }

  if (!strcasecmp(param, "ENABLED")) {
    bool on = (!strcasecmp(value, "ON") || !strcasecmp(value, "1") || !strcasecmp(value, "TRUE"));
    bool off = (!strcasecmp(value, "OFF") || !strcasecmp(value, "0") || !strcasecmp(value, "FALSE"));
    if (!on && !off) {
      debug_println("SET ANALOG ENABLED: brug on|off");
      return;
    }
    *p_enabled = on;
    debug_printf("%s enabled sat til: %s\n", ref.label, on ? "ON" : "OFF");
    if (ref.group == ACH_AI_V && analog_driver_ai_v_is_adc2(ref.idx) && on) {
      debug_println("  OBS: denne kanal er ADC2 — ubrugelig mens WiFi er tilsluttet");
    }
    debug_println("  Kraever 'save' + reboot (register-allokering sker ved boot)");
  } else if (!strcasecmp(param, "SCALE")) {
    *p_scale = strtof(value, NULL);
    debug_printf("%s scale sat til: %g\n", ref.label, (double)*p_scale);
    debug_println("  Kraever 'save' for at persistere (virker straks i RAM)");
  } else if (!strcasecmp(param, "OFFSET")) {
    *p_offset = strtof(value, NULL);
    debug_printf("%s offset sat til: %g\n", ref.label, (double)*p_offset);
    debug_println("  Kraever 'save' for at persistere (virker straks i RAM)");
  } else {
    debug_printf("SET ANALOG: ukendt parameter '%s' (gyldige: enabled, scale, offset)\n", param);
  }
}

/* ============================================================================
 * SHOW ANALOG
 * ============================================================================ */

static void print_channel_row(const char *label, bool enabled, uint16_t raw_reg,
                               uint16_t value_reg, float scale, float offset,
                               bool unavailable) {
  if (unavailable) {
    debug_printf("  %-5s %-3s  %6s  %8s  raw_reg=%-4u val_reg=%-4u scale=%-8g offset=%-8g  ** WiFi aktiv, ADC2 utilgaengelig **\n",
                 label, enabled ? "ON" : "off", "-", "-", raw_reg, value_reg, (double)scale, (double)offset);
    return;
  }
  uint16_t raw = registers_get_holding_register(raw_reg);
  uint16_t val = registers_get_holding_register(value_reg);
  char valbuf[16];
  snprintf(valbuf, sizeof(valbuf), "%d.%02d", val / 100, val % 100);
  debug_printf("  %-5s %-3s  %5umV  %8s  raw_reg=%-4u val_reg=%-4u scale=%-8g offset=%-8g\n",
               label, enabled ? "ON" : "off", raw, valbuf, raw_reg, value_reg, (double)scale, (double)offset);
}

void cli_cmd_show_analog(void) {
  bool wifi_active = wifi_driver_is_connected();

  debug_println("\n=== ANALOG I/O (ES32D26) ===");
  debug_println("\nSpaendingsindgange (0-10V):");
  debug_println("  Kanal En.  Raa     Vaerdi(V)  Register + kalibrering");
  for (uint8_t i = 0; i < 4; i++) {
    const AnalogInputConfig *c = &g_persist_config.analog_ai_v[i];
    bool unavail = wifi_active && analog_driver_ai_v_is_adc2(i) && c->enabled;
    char label[6];
    snprintf(label, sizeof(label), "Vi%d", i + 1);
    print_channel_row(label, c->enabled, c->raw_reg, c->value_reg, c->scale, c->offset, unavail);
  }

  debug_println("\nStroemindgange (4-20mA):");
  debug_println("  Kanal En.  Raa     Vaerdi(mA) Register + kalibrering");
  for (uint8_t i = 0; i < 4; i++) {
    const AnalogInputConfig *c = &g_persist_config.analog_ai_i[i];
    char label[6];
    snprintf(label, sizeof(label), "Ii%d", i + 1);
    print_channel_row(label, c->enabled, c->raw_reg, c->value_reg, c->scale, c->offset, false);
  }

  debug_println("\nAnaloge udgange (DAC, 8-bit):");
  debug_println("  Kanal En.  Mode      Setpoint(reg)  Kalibrering");
  for (uint8_t i = 0; i < 2; i++) {
    const AnalogOutputConfig *c = &g_persist_config.analog_ao[i];
    uint8_t mode = (i == 0) ? g_persist_config.ao1_mode : g_persist_config.ao2_mode;
    uint16_t setpoint = registers_get_holding_register(c->value_reg);
    char valbuf[16];
    snprintf(valbuf, sizeof(valbuf), "%d.%02d", setpoint / 100, setpoint % 100);
    debug_printf("  AO%-3d %-3s  %-8s  reg=%-4u (%s)  scale=%g offset=%g\n",
                 i + 1, c->enabled ? "ON" : "off",
                 mode == AO_MODE_CURRENT ? "4-20mA" : "0-10V",
                 c->value_reg, valbuf, (double)c->scale, (double)c->offset);
  }
  debug_println("  (AO-mode saettes via 'set ao1|ao2 mode voltage|current' — matcher fysisk DIP switch SW1)");
  debug_println("");
}

#else  // !ANALOG_IO_ENABLED

void cli_cmd_set_analog(uint8_t argc, char **argv) {
  (void)argc; (void)argv;
  debug_println("SET ANALOG: ikke understoettet paa dette board");
}
void cli_cmd_show_analog(void) {
  debug_println("SHOW ANALOG: ikke understoettet paa dette board");
}

#endif // ANALOG_IO_ENABLED
