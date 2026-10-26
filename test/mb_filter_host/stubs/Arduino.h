// PC-stub til mb_filter-testen (FEAT-508)
#pragma once
#include <stdint.h>
#include <string.h>
#include <stddef.h>
uint32_t millis();
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) (void)(x)
#define portEXIT_CRITICAL(x) (void)(x)
