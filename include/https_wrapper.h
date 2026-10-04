// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file https_wrapper.h
 * @brief C wrapper for ESP-IDF esp_https_server (FEAT-016)
 *
 * This wrapper exists because esp_https_server.h includes esp_tls.h which
 * pulls in lwIP headers through sys/socket.h. In Arduino-ESP32 v2.x C++ builds,
 * this causes a compilation error in ip4_addr.h (u32_t not defined).
 * By wrapping in a .c file, we avoid the C++ include chain issue.
 */

#ifndef HTTPS_WRAPPER_H
#define HTTPS_WRAPPER_H

#include <stdint.h>
#include <esp_http_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start HTTPS server with TLS using embedded certificates
 *
 * @param[out] handle  Server handle (set on success)
 * @param port         HTTPS port number
 * @param max_uri      Max URI handlers
 * @param stack_size   Task stack size
 * @param priority     Task priority
 * @param core_id      Core affinity
 * @return 0 on success, -1 on error
 */
int https_wrapper_start(httpd_handle_t *handle,
                        uint16_t port,
                        uint16_t max_uri,
                        uint32_t stack_size,
                        uint8_t priority,
                        int core_id);

/**
 * Stop HTTPS server and free TLS resources
 *
 * @param handle  Server handle from https_wrapper_start()
 */
void https_wrapper_stop(httpd_handle_t handle);

/**
 * Get embedded TLS certificate info string
 *
 * Parses the build-time embedded certificate and returns a human-readable
 * summary, e.g. "ECDSA P-256 (256-bit)" or "RSA (2048-bit)".
 *
 * @param[out] buf     Output buffer
 * @param      buflen  Buffer size
 * @return 0 on success, -1 on parse error
 */
int https_wrapper_get_cert_info(char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif // HTTPS_WRAPPER_H
