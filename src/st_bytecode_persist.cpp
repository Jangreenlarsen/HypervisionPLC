/**
 * @file st_bytecode_persist.cpp
 * @brief Bytecode persistence to SPIFFS
 *
 * Serializes/deserializes compiled ST bytecode to /logic_N.bc files.
 * CRC32 of source code validates cache freshness.
 */

#include "st_bytecode_persist.h"
#include "debug.h"
#include "debug_flags.h"
#include <string.h>
#include <stdlib.h>
#include <FS.h>
#include <SPIFFS.h>

/* ============================================================================
 * CRC32 (standard polynomial 0xEDB88320)
 * ============================================================================ */

uint32_t st_crc32(const uint8_t *data, uint32_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc >>= 1;
    }
  }
  return ~crc;
}

/* ============================================================================
 * HELPER: Build filename
 * ============================================================================ */

static void bc_filename(uint8_t program_id, char *buf, size_t buf_size) {
  snprintf(buf, buf_size, "/logic_%d.bc", program_id);
}

/* ============================================================================
 * SAVE bytecode to SPIFFS
 * ============================================================================ */

bool st_bytecode_save(uint8_t program_id, const st_bytecode_program_t *bytecode,
                      const char *source, uint32_t source_size) {
  if (program_id >= 4 || !bytecode || !bytecode->instructions || bytecode->instr_count == 0) {
    return false;
  }

  char filename[32];
  bc_filename(program_id, filename, sizeof(filename));

  File file = SPIFFS.open(filename, FILE_WRITE);
  if (!file) {
    debug_printf("[BC] Save failed: cannot open %s\n", filename);
    return false;
  }

  // Build header
  st_bc_header_t header;
  memset(&header, 0, sizeof(header));
  header.magic = ST_BYTECODE_MAGIC;
  header.version = ST_BYTECODE_VERSION;
  header.instr_count = bytecode->instr_count;
  header.var_count = bytecode->var_count;
  header.exported_var_count = bytecode->exported_var_count;
  header.has_func_registry = (bytecode->func_registry != NULL) ? 1 : 0;
  header.source_crc32 = st_crc32((const uint8_t *)source, source_size);

  // Write header (16 bytes)
  if (file.write((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SPIFFS.remove(filename);
    return false;
  }

  // Write program name (32 bytes)
  if (file.write((uint8_t *)bytecode->name, 32) != 32) {
    file.close();
    SPIFFS.remove(filename);
    return false;
  }

  // Write variable table: name[16] + type(1) + export_flag(1) + initial_value(4) per variable
  for (uint8_t v = 0; v < bytecode->var_count; v++) {
    file.write((uint8_t *)bytecode->var_names[v], 16);
    uint8_t type_byte = (uint8_t)bytecode->var_types[v];
    file.write(type_byte);
    file.write(bytecode->var_export_flags[v]);
    // v2: Write initial value (4 bytes)
    file.write((uint8_t *)&bytecode->var_initial[v], sizeof(st_value_t));
  }

  // Write instructions (instr_count × 8 bytes, flat POD)
  size_t instr_bytes = (size_t)bytecode->instr_count * sizeof(st_bytecode_instr_t);
  if (file.write((uint8_t *)bytecode->instructions, instr_bytes) != instr_bytes) {
    file.close();
    SPIFFS.remove(filename);
    return false;
  }

  // Write function registry (optional)
  if (bytecode->func_registry) {
    const st_function_registry_t *reg = bytecode->func_registry;
    file.write(reg->user_count);
    file.write(reg->builtin_count);

    uint8_t total = reg->builtin_count + reg->user_count;
    for (uint8_t f = 0; f < total; f++) {
      const st_function_entry_t *entry = &reg->functions[f];
      file.write((uint8_t *)entry->name, 32);
      file.write((uint8_t)entry->return_type);
      file.write(entry->param_count);
      for (uint8_t p = 0; p < 8; p++) {
        file.write((uint8_t)entry->param_types[p]);
      }
      file.write((uint8_t *)&entry->bytecode_addr, 2);
      file.write((uint8_t *)&entry->bytecode_size, 2);
      file.write(entry->is_builtin);
      file.write(entry->is_function_block);
      file.write(entry->instance_size);
    }
  }

  file.close();

  debug_printf("[BC] Saved %s: %u instr, %u vars, %u bytes\n",
               filename, header.instr_count, header.var_count, (unsigned)instr_bytes + sizeof(header));

  return true;
}

/* ============================================================================
 * LOAD bytecode from SPIFFS
 * ============================================================================ */

bool st_bytecode_load(uint8_t program_id, st_bytecode_program_t *bytecode,
                      const char *source, uint32_t source_size) {
  if (program_id >= 4 || !bytecode || !source || source_size == 0) {
    return false;
  }

  char filename[32];
  bc_filename(program_id, filename, sizeof(filename));

  if (!SPIFFS.exists(filename)) {
    return false;
  }

  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) {
    return false;
  }

  // Read and validate header
  st_bc_header_t header;
  if (file.read((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
    file.close();
    return false;
  }

  if (header.magic != ST_BYTECODE_MAGIC) {
    debug_printf("[BC] %s: bad magic 0x%08X\n", filename, (unsigned)header.magic);
    file.close();
    return false;
  }

  if (header.version != ST_BYTECODE_VERSION) {
    debug_printf("[BC] %s: version mismatch (file=%u, expected=%u) -> recompile\n",
                 filename, header.version, ST_BYTECODE_VERSION);
    file.close();
    return false;
  }

  // Validate CRC32 against current source
  uint32_t current_crc = st_crc32((const uint8_t *)source, source_size);
  if (header.source_crc32 != current_crc) {
    debug_printf("[BC] %s: source changed (CRC 0x%08X != 0x%08X) -> recompile\n",
                 filename, (unsigned)header.source_crc32, (unsigned)current_crc);
    file.close();
    return false;
  }

  // Sanity checks
  if (header.instr_count == 0 || header.instr_count > 4096 ||
      header.var_count > 32 || header.exported_var_count > 32) {
    debug_printf("[BC] %s: invalid counts (instr=%u var=%u)\n",
                 filename, header.instr_count, header.var_count);
    file.close();
    return false;
  }

  // Read program name (32 bytes)
  if (file.read((uint8_t *)bytecode->name, 32) != 32) {
    file.close();
    return false;
  }

  // Read variable table
  bytecode->var_count = header.var_count;
  bytecode->exported_var_count = header.exported_var_count;
  for (uint8_t v = 0; v < header.var_count; v++) {
    if (file.read((uint8_t *)bytecode->var_names[v], 16) != 16) {
      file.close();
      return false;
    }
    uint8_t type_byte = file.read();
    bytecode->var_types[v] = (st_datatype_t)type_byte;
    bytecode->var_export_flags[v] = file.read();

    // v2: Read initial value and use it as starting value
    if (file.read((uint8_t *)&bytecode->var_initial[v], sizeof(st_value_t)) != sizeof(st_value_t)) {
      file.close();
      return false;
    }
    bytecode->variables[v] = bytecode->var_initial[v];
  }

  // Allocate and read instructions
  size_t instr_bytes = (size_t)header.instr_count * sizeof(st_bytecode_instr_t);
  bytecode->instructions = (st_bytecode_instr_t *)malloc(instr_bytes);
  if (!bytecode->instructions) {
    debug_printf("[BC] %s: malloc failed (%u bytes)\n", filename, (unsigned)instr_bytes);
    file.close();
    return false;
  }

  if (file.read((uint8_t *)bytecode->instructions, instr_bytes) != instr_bytes) {
    free(bytecode->instructions);
    bytecode->instructions = NULL;
    file.close();
    return false;
  }

  bytecode->instr_count = header.instr_count;
  bytecode->instr_capacity = header.instr_count;

  // Read function registry (optional)
  bytecode->func_registry = NULL;
  if (header.has_func_registry && file.available() >= 2) {
    uint8_t user_count = file.read();
    uint8_t builtin_count = file.read();
    uint8_t total = builtin_count + user_count;

    if (total > 0 && total <= 64) {
      st_function_registry_t *reg = (st_function_registry_t *)malloc(sizeof(st_function_registry_t));
      if (!reg) {
        // Non-fatal: bytecode works without registry (no user functions callable)
        debug_printf("[BC] %s: registry malloc failed\n", filename);
      } else {
        memset(reg, 0, sizeof(st_function_registry_t));
        reg->user_count = user_count;
        reg->builtin_count = builtin_count;

        bool reg_ok = true;
        for (uint8_t f = 0; f < total && reg_ok; f++) {
          st_function_entry_t *entry = &reg->functions[f];
          if (file.read((uint8_t *)entry->name, 32) != 32) { reg_ok = false; break; }
          entry->return_type = (st_datatype_t)file.read();
          entry->param_count = file.read();
          for (uint8_t p = 0; p < 8; p++) {
            entry->param_types[p] = (st_datatype_t)file.read();
          }
          if (file.read((uint8_t *)&entry->bytecode_addr, 2) != 2) { reg_ok = false; break; }
          if (file.read((uint8_t *)&entry->bytecode_size, 2) != 2) { reg_ok = false; break; }
          entry->is_builtin = file.read();
          entry->is_function_block = file.read();
          entry->instance_size = file.read();
        }

        if (reg_ok) {
          bytecode->func_registry = reg;
        } else {
          free(reg);
          debug_printf("[BC] %s: registry read error\n", filename);
        }
      }
    }
  }

  // stateful pointer initialized to NULL — allocated on first execution
  bytecode->stateful = NULL;

  file.close();

  debug_printf("[BC] Loaded %s: %u instr, %u vars (cached)\n",
               filename, header.instr_count, header.var_count);

  return true;
}

/* ============================================================================
 * INVALIDATE (delete cached bytecode)
 * ============================================================================ */

void st_bytecode_invalidate(uint8_t program_id) {
  if (program_id >= 4) return;

  char filename[32];
  bc_filename(program_id, filename, sizeof(filename));

  if (SPIFFS.exists(filename)) {
    SPIFFS.remove(filename);
    debug_printf("[BC] Invalidated %s\n", filename);
  }
}
