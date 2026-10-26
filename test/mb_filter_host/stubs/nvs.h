// PC-stub: NVS findes ikke paa PC — aabning fejler, saa intet gemmes/indlaeses
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef int esp_err_t;
typedef uint32_t nvs_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
inline esp_err_t nvs_open(const char *, nvs_open_mode_t, nvs_handle_t *) { return ESP_FAIL; }
inline esp_err_t nvs_get_blob(nvs_handle_t, const char *, void *, size_t *) { return ESP_FAIL; }
inline esp_err_t nvs_set_blob(nvs_handle_t, const char *, const void *, size_t) { return ESP_FAIL; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_FAIL; }
inline void nvs_close(nvs_handle_t) {}
