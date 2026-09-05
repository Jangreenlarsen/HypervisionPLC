/**
 * @file cli_history.cpp
 * @brief CLI command history and navigation implementation (LAYER 7)
 *
 * Circular buffer for storing command history with navigation support
 */

#include "cli_history.h"
#include <string.h>
#include <stdlib.h>
#include <esp_heap_caps.h>

/* ============================================================================
 * HISTORY BUFFER
 * ============================================================================ */

/* FEAT-154: flyttet fra intern DRAM til PSRAM (~2,5 KB frigjort).
 * Kommandohistorikken er den koldest taenkelige buffer — den roeres kun naar
 * et menneske taster en kommando eller blaedrer med piletasterne. Ingen
 * ISR-adgang, ingen DMA, ingen timingkrav, saa PSRAM'ens hoejere latenstid
 * er fuldstaendig ligegyldig her.
 *
 * Pegertypen `char (*)[CLI_HISTORY_LINE_LENGTH]` bevarer den oprindelige
 * 2D-indeksering, saa cli_history_buffer[i] fortsat giver en char* til
 * linje i — alle brugssteder er uaendrede. */
static char (*cli_history_buffer)[CLI_HISTORY_LINE_LENGTH] = NULL;
static uint8_t cli_history_count = 0;      // Number of valid entries
static uint8_t cli_history_head = 0;       // Next write position
static int8_t cli_history_nav_pos = -1;   // Navigation position (-1 = not navigating)

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/* FEAT-154: allokerer ved foerste brug (PSRAM, ellers intern heap).
 * Returnerer false hvis hukommelsen ikke kunne skaffes — saa springes
 * historikken over i stedet for at dereferere NULL. Historik er en
 * bekvemmelighed, ikke noget der maa vaelte systemet. */
static bool cli_history_ready(void) {
  if (cli_history_buffer) return true;
  size_t bytes = (size_t)CLI_HISTORY_SIZE * CLI_HISTORY_LINE_LENGTH;
  cli_history_buffer = (char (*)[CLI_HISTORY_LINE_LENGTH])
                        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!cli_history_buffer) {
    cli_history_buffer = (char (*)[CLI_HISTORY_LINE_LENGTH])malloc(bytes);
  }
  if (!cli_history_buffer) return false;
  memset(cli_history_buffer, 0, bytes);
  return true;
}

void cli_history_init(void) {
  if (cli_history_ready()) {
    memset(cli_history_buffer, 0, (size_t)CLI_HISTORY_SIZE * CLI_HISTORY_LINE_LENGTH);
  }
  cli_history_count = 0;
  cli_history_head = 0;
  cli_history_nav_pos = -1;
}

/* ============================================================================
 * ADD TO HISTORY
 * ============================================================================ */

void cli_history_add(const char* command) {
  if (!command || strlen(command) == 0) {
    return;  // Don't store empty commands
  }
  if (!cli_history_ready()) {
    return;  // FEAT-154: ingen buffer — spring historik over, fejl ikke
  }

  // Store command in circular buffer
  strncpy(cli_history_buffer[cli_history_head], command, CLI_HISTORY_LINE_LENGTH - 1);
  cli_history_buffer[cli_history_head][CLI_HISTORY_LINE_LENGTH - 1] = '\0';

  // Update counters
  cli_history_head = (cli_history_head + 1) % CLI_HISTORY_SIZE;
  if (cli_history_count < CLI_HISTORY_SIZE) {
    cli_history_count++;
  }

  // Reset navigation when new command added
  cli_history_nav_pos = -1;
}

/* ============================================================================
 * NAVIGATION
 * ============================================================================ */

const char* cli_history_get_prev(void) {
  if (cli_history_count == 0) {
    return NULL;  // No history
  }

  // First press of up arrow: start from most recent
  if (cli_history_nav_pos == -1) {
    cli_history_nav_pos = cli_history_count - 1;
  } else if (cli_history_nav_pos > 0) {
    // Go further back
    cli_history_nav_pos--;
  } else {
    // Already at oldest, stay there
    cli_history_nav_pos = 0;
  }

  // Calculate buffer index for this position
  // Formula: index = (head - count + nav_pos + SIZE) % SIZE
  uint8_t index = (cli_history_head - cli_history_count + cli_history_nav_pos + CLI_HISTORY_SIZE) % CLI_HISTORY_SIZE;
  return cli_history_buffer[index];
}

const char* cli_history_get_next(void) {
  if (cli_history_count == 0 || cli_history_nav_pos == -1) {
    return NULL;  // Not navigating
  }

  // Move forward in history (towards newest)
  if (cli_history_nav_pos < cli_history_count - 1) {
    cli_history_nav_pos++;
  } else {
    // At newest, go back to "not navigating" state
    cli_history_nav_pos = -1;
    return "";  // Return empty string to clear line
  }

  // Calculate buffer index for this position
  uint8_t index = (cli_history_head - cli_history_count + cli_history_nav_pos + CLI_HISTORY_SIZE) % CLI_HISTORY_SIZE;
  return cli_history_buffer[index];
}

void cli_history_reset_nav(void) {
  cli_history_nav_pos = -1;
}
