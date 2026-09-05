/**
 * @file cli_commands_analog.h
 * @brief CLI `set analog` / `show analog` command handlers (FEAT-034/035/036/037)
 *
 * LAYER 7: User Interface - CLI Analog I/O Commands
 * ES32D26 only — 4x 0-10V (Vi1-4), 4x 4-20mA (Ii1-4), 2x DAC-udgang (AO1-2).
 */

#ifndef CLI_COMMANDS_ANALOG_H
#define CLI_COMMANDS_ANALOG_H

#include <stdint.h>

/**
 * @brief `set analog <kanal> enabled on|off|scale <f>|offset <f>`
 * Kanal: vi1-vi4 (spaending), ii1-ii4 (stroem), ao1-ao2 (udgang) — case-insensitiv.
 */
void cli_cmd_set_analog(uint8_t argc, char **argv);

/**
 * @brief `show analog` — tabel over alle 10 kanaler: enabled, raw, kalibreret
 * vaerdi, register, evt. "utilgaengelig (WiFi aktiv)" for Vi1/Vi3 (ADC2).
 */
void cli_cmd_show_analog(void);

#endif // CLI_COMMANDS_ANALOG_H
