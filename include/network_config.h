// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2025-2026 Jan Green Larsen
/**
 * @file network_config.h
 * @brief Network configuration validation and defaults
 *
 * Handles NetworkConfig struct initialization, validation, and parameter checking.
 */

#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include <stdint.h>
#include "types.h"

/* ============================================================================
 * INITIALIZATION & DEFAULTS
 * ============================================================================ */

/**
 * Initialize NetworkConfig with safe defaults
 * @param config NetworkConfig struct to initialize
 */
void network_config_init_defaults(NetworkConfig *config);

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

/**
 * Validate NetworkConfig struct
 * @param config NetworkConfig to validate
 * @return 1 if valid, 0 if invalid
 */
uint8_t network_config_validate(const NetworkConfig *config);

/**
 * Validate SSID
 * @param ssid SSID string
 * @return 1 if valid, 0 if invalid
 */
uint8_t network_config_is_valid_ssid(const char *ssid);

/**
 * Validate password
 * @param password Password string
 * @return 1 if valid, 0 if invalid
 */
uint8_t network_config_is_valid_password(const char *password);

/**
 * Validate IP address (network byte order)
 * @param ip IP address in network byte order
 * @return 1 if valid (non-zero), 0 if invalid (0.0.0.0)
 */
uint8_t network_config_is_valid_ip(uint32_t ip);

/**
 * Validate netmask
 * @param netmask Netmask in network byte order
 * @return 1 if valid, 0 if invalid
 */
uint8_t network_config_is_valid_netmask(uint32_t netmask);

/* ============================================================================
 * CONVERSION UTILITIES (IP address string ↔ network byte order)
 * ============================================================================ */

/**
 * Convert IP string to network byte order
 * @param ip_str IP address string (e.g., "192.168.1.1")
 * @param out_ip Output IP in network byte order
 * @return 1 on success, 0 if invalid format
 */
uint8_t network_config_str_to_ip(const char *ip_str, uint32_t *out_ip);

/**
 * Convert network byte order IP to string
 * @param ip IP address in network byte order
 * @param out_str Output buffer (min 16 bytes for "XXX.XXX.XXX.XXX")
 * @return pointer to out_str
 */
char* network_config_ip_to_str(uint32_t ip, char *out_str);

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * Print NetworkConfig to console
 * @param config NetworkConfig to print
 */
void network_config_print(const NetworkConfig *config);

#endif // NETWORK_CONFIG_H
