// PC-stub af mb_async.h: kun det mb_filter.cpp bruger
#pragma once
#include <stdint.h>
enum { MB_REQ_READ_HOLDING = 3, MB_REQ_READ_INPUT_REG = 4 };
typedef int portMUX_TYPE;
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
extern portMUX_TYPE mb_cache_spinlock;
typedef struct { int32_t cand_value; uint8_t cand_count; } mb_cache_entry_t;
typedef struct { mb_cache_entry_t entries[32]; uint8_t entry_count; } mb_async_state_t;
extern mb_async_state_t g_mb_async;
