// PC-stub af modbus_expansion_async.h: kun det mb_filter.cpp bruger
#pragma once
#include "Arduino.h"
typedef struct { int32_t cand_value; uint8_t cand_count; } mbx_cache_entry_t;
typedef struct { mbx_cache_entry_t entries[64]; uint16_t entry_count; } mbx_async_state_t;
extern mbx_async_state_t g_mbx_async;
extern portMUX_TYPE mbx_cache_spinlock;
