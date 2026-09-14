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
 * SESSION TOKENS (BUG-353, REST API auth-modernisering fase 2)
 *
 * In-RAM bearer tokens issued via POST /api/login after a normal Basic Auth
 * check, so the browser can stop resending username:password on every
 * request. Mirrors sse_events.cpp's sse_token_issue()/sse_token_check()
 * pattern (fixed slot array, spinlock, esp_random() hex token, TTL+GC) —
 * but with its own table: session tokens use a SLIDING 30-min idle timeout
 * (extended on each valid use) rather than SSE's fixed 5-min bridge TTL.
 * Basic Auth keeps working unchanged and forever — this is additive, not a
 * replacement (scripts/Node-RED/curl integrations are unaffected).
 * ============================================================================ */

/**
 * Issue a new session token for the given RBAC user index (or 99 for
 * virtual admin). Caller must have authenticated the user first via normal
 * HTTP Basic Auth. Returns pointer to token string (static storage, valid
 * until next issue) or NULL on failure.
 */
const char *rbac_session_token_issue(int user_idx);

/**
 * Validate a session token. On success, extends its expiry by another
 * RBAC_SESSION_TOKEN_TTL_MS (sliding idle timeout).
 * @return User index on success, -1 on invalid/expired/unknown token.
 */
int rbac_session_token_check(const char *token);

/**
 * Immediately invalidate a session token (logout). No-op if the token is
 * already invalid/unknown — logout should never visibly fail.
 */
void rbac_session_token_revoke(const char *token);

/**
 * FEAT-399 (IP ACL lockout-recovery): immediately invalidate ALL active
 * session tokens. Used when a "gated" ACL rule change goes live, to force
 * every client back through a fresh POST /api/login — since RbacSessionToken
 * has no issued_at timestamp, a token that validates AFTER this call is, by
 * construction, proof of a login that happened after the new rule took
 * effect. Same spinlock-protected pattern as rbac_session_token_revoke().
 */
void rbac_session_token_revoke_all(void);

/**
 * BUG-393: extract the "hfplc_session" cookie's value from this request's
 * Cookie header, if present. Used as a fallback session-token source for
 * browser clients (which no longer send a manual Authorization header) —
 * see rbac_check_http(). Never required for Basic/Bearer Authorization
 * header-based clients (curl/scripts), which are unaffected.
 * @return true and fills out[] (null-terminated) if the cookie was found.
 */
bool rbac_extract_cookie_token(httpd_req_t *req, char *out, size_t out_len);

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
 * PASSWORD HASHING (schema 22+, BUG-352)
 * ============================================================================ */

/**
 * Generate a fresh 16-byte random salt via esp_fill_random().
 */
void rbac_generate_salt(uint8_t out_salt[16]);

/**
 * Compute SHA-256(salt || password) into out_hash[32].
 * Used for both storing (hash a new password) and verifying (hash the
 * submitted password with the stored salt, then compare to the stored hash).
 */
void rbac_hash_password(const char *password, const uint8_t salt[16], uint8_t out_hash[32]);

/**
 * Constant-time comparison of two 32-byte hashes.
 */
bool rbac_hash_equal(const uint8_t a[32], const uint8_t b[32]);

/**
 * Hash `password` with a fresh salt and store both into the legacy
 * single-user HTTP config (cfg->network.http.password + cfg->http_legacy_salt).
 * Takes an explicit PersistConfig* (like rbac_migrate_legacy) so it can be
 * used both live (&g_persist_config) and on a scratch struct during
 * config_init_defaults()/migration.
 */
void rbac_hash_and_store_legacy_password(PersistConfig *cfg, const char *password);

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
