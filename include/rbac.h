/**
 * @file rbac.h
 * @brief Role-Based Access Control (RBAC) multi-user system (v7.6.2)
 *
 * LAYER 2: Security
 * Provides centralized authentication and authorization for all interfaces:
 * HTTP REST API, SSE, Web UI, CLI (Serial/Telnet), and Web CLI.
 *
 * Up to 8 user accounts with configurable roles and privilege levels.
 */

#ifndef RBAC_H
#define RBAC_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_http_server.h>
#include "types.h"

// Structs (RbacUser, RbacConfig) and constants (ROLE_*, PRIV_*) defined in types.h / constants.h

/* ============================================================================
 * AUTHENTICATION
 * ============================================================================ */

/**
 * Authenticate user by username and password.
 * @return User index (0-7) on success, -1 on failure
 */
int rbac_authenticate(const char *username, const char *password);

/**
 * Authenticate from HTTP Basic Auth header (httpd_req_t).
 * Handles Base64 decoding and credential lookup.
 * @return User index (0-7) on success, -1 on failure.
 *         If RBAC disabled, returns 99 (virtual admin).
 */
int rbac_check_http(httpd_req_t *req);

/**
 * Authenticate from raw SSE Authorization header value.
 * @return User index (0-7) on success, -1 on failure.
 *         If RBAC disabled, returns 99 (virtual admin).
 */
int rbac_check_sse(const char *auth_header);

/* ============================================================================
 * AUTHORIZATION CHECKS
 * ============================================================================ */

/**
 * Check if user has a specific role (or combination of roles).
 * Virtual admin (index 99) always returns true.
 */
bool rbac_has_role(int user_index, uint8_t role_mask);

/**
 * Check if user has write privilege.
 * Virtual admin (index 99) always returns true.
 */
bool rbac_has_write(int user_index);

/**
 * Check if a CLI command is allowed for a user.
 * Read-only users can only use: show, help, ?, ping, who
 */
bool rbac_cli_allowed(int user_index, const char *command);

/* ============================================================================
 * USER MANAGEMENT
 * ============================================================================ */

/**
 * Add or update a user. If username exists, update; otherwise add to first empty slot.
 * @return User index (0-7) on success, -1 if full or invalid params
 */
int rbac_set_user(const char *username, const char *password, uint8_t roles, uint8_t privilege);

/**
 * Delete a user by username.
 * @return true if user found and deleted
 */
bool rbac_delete_user(const char *username);

/**
 * Find user by username.
 * @return User index (0-7) or -1 if not found
 */
int rbac_find_user(const char *username);

/**
 * Get user by index. Returns NULL if index invalid or slot inactive.
 */
const RbacUser* rbac_get_user(int index);

/**
 * Get number of active users.
 */
int rbac_get_user_count(void);

/* ============================================================================
 * MIGRATION & INIT
 * ============================================================================ */

/**
 * Migrate legacy single-user config to RBAC user[0].
 * Called during schema migration (14 → 15).
 */
void rbac_migrate_legacy(void *persist_config);

/**
 * Format role bitmask as human-readable string.
 * @param roles Role bitmask
 * @param buf Output buffer (min 40 bytes)
 */
void rbac_roles_to_str(uint8_t roles, char *buf, size_t buf_len);

/**
 * Parse role string (e.g., "api,cli,editor,monitor" or "all") to bitmask.
 */
uint8_t rbac_parse_roles(const char *str);

/**
 * Parse privilege string ("read" or "write" or "read/write" or "rw") to bitmask.
 */
uint8_t rbac_parse_privilege(const char *str);

#endif // RBAC_H
