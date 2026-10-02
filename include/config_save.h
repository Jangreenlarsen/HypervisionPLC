/**
 * @file config_save.h
 * @brief Configuration persistence - save to NVS (LAYER 6)
 *
 * LAYER 6: Persistence - Config Save
 * Responsibility: Save configuration to non-volatile storage (NVS/EEPROM)
 *
 * This file handles:
 * - CRC16 calculation for config integrity
 * - Atomic write to NVS
 * - Error handling
 *
 * Does NOT handle:
 * - Loading from NVS (→ config_load.h)
 * - Applying config to system (→ config_apply.h)
 */

#ifndef config_save_H
#define config_save_H

#include <stdint.h>
#include "types.h"

/**
 * @brief Calculate CRC16 checksum for config
 * @param cfg Configuration to checksum
 * @return CRC16 value
 */
uint16_t config_calculate_crc16(const PersistConfig* cfg);

/**
 * @brief Save configuration to NVS
 * @param cfg Configuration to save
 * @return true if successful, false on error
 */
bool config_save_to_nvs(const PersistConfig* cfg);

/* BUG-430: ugemte aendringer. Fingeraftryk (CRC) af PersistConfig uden
 * runtime-statistik; sammenlignes med det sidst gemte/indlaeste. */
uint16_t config_fingerprint(const PersistConfig* cfg);
void config_mark_saved(const PersistConfig* cfg);
bool config_has_unsaved_changes(void);

#endif // config_save_H
