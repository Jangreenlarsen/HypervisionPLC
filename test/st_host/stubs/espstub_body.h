// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Stub for ESP-IDF/FreeRTOS-headers — kun til PC-testen af ST-compiler/VM (BUG-433).
#ifndef ESPSTUB_BODY
#define ESPSTUB_BODY
#include <stdint.h>
#include <stddef.h>
static inline uint32_t esp_get_free_heap_size(void){return 200000;}
static inline size_t heap_caps_get_largest_free_block(uint32_t){return 100000;}
static inline size_t heap_caps_get_free_size(uint32_t){return 200000;}
#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 0
#endif
typedef void* SemaphoreHandle_t; typedef void* TaskHandle_t; typedef int BaseType_t; typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFF
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGI(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)

#include <stdlib.h>
static inline void* heap_caps_malloc(size_t n, uint32_t){return malloc(n);}
static inline void* heap_caps_calloc(size_t a, size_t n, uint32_t){return calloc(a,n);}
#endif
