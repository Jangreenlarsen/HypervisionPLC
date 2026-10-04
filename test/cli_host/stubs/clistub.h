// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
// Ekstra ESP-IDF-stubs til PC-testen af CLI-parseren (test/cli_host).
#pragma once
#include "espstub_body.h"
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef struct httpd_req { int dummy; } httpd_req_t;
typedef void* httpd_handle_t;
typedef void* QueueHandle_t;
typedef struct { int dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
void esp_restart(void);
#ifdef __cplusplus
extern "C" {
#endif
int setenv(const char*, const char*, int);
void tzset(void);
#ifdef __cplusplus
}
#endif
