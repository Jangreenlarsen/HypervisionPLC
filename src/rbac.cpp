/**
 * @file rbac.cpp
 * @brief Role-Based Access Control (RBAC) implementation (v7.6.2)
 *
 * LAYER 2: Security
 * Centralized authentication and authorization for HTTP, SSE, CLI, and Web UI.
 */

#include <string.h>
#include <stdio.h>
#include <Arduino.h>  // BUG-353: millis(), portMUX_TYPE/taskENTER_CRITICAL for session tokens
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_random.h>

#include "rbac.h"
#include "config_struct.h"
#include "debug.h"

static const char *TAG = "RBAC";

// Forward reference to global config
extern PersistConfig g_persist_config;

/* ============================================================================
 * PASSWORD HASHING (schema 22+, BUG-352)
 * ============================================================================ */

void rbac_generate_salt(uint8_t out_salt[16])
{
  esp_fill_random(out_salt, 16);
}

void rbac_hash_password(const char *password, const uint8_t salt[16], uint8_t out_hash[32])
{
  size_t pw_len = password ? strlen(password) : 0;

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, salt, 16);
  if (pw_len > 0) {
    mbedtls_sha256_update(&ctx, (const unsigned char *)password, pw_len);
  }
  mbedtls_sha256_finish(&ctx, out_hash);
  mbedtls_sha256_free(&ctx);
}

bool rbac_hash_equal(const uint8_t a[32], const uint8_t b[32])
{
  // Constant-time compare — avoid a timing side-channel on the byte where
  // a submitted-password hash first diverges from the stored one.
  uint8_t diff = 0;
  for (int i = 0; i < 32; i++) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0;
}

void rbac_hash_and_store_legacy_password(PersistConfig *cfg, const char *password)
{
  if (!cfg) return;
  rbac_generate_salt(cfg->http_legacy_salt);
  uint8_t hash[32];
  rbac_hash_password(password, cfg->http_legacy_salt, hash);
  memcpy(cfg->network.http.password, hash, 32);
  // Resten af det 64-byte password[]-feltet er ubrugt af hashen — nulstil
  // det, saa der ikke ligger rester af et TIDLIGERE klartekst-password
  // (som kunne have vaeret laengere end 32 bytes) efter de foerste 32 bytes.
  memset(cfg->network.http.password + 32, 0, sizeof(cfg->network.http.password) - 32);
}

/* ============================================================================
 * AUTHENTICATION
 * ============================================================================ */

int rbac_authenticate(const char *username, const char *password)
{
  if (!username || !password) return -1;
  if (!g_persist_config.rbac.enabled) return -1;

  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    const RbacUser *u = &g_persist_config.rbac.users[i];
    if (!u->active) continue;
    if (strcmp(u->username, username) != 0) continue;

    // BUG-352: u->password er fra schema 22 en raw SHA-256-hash (32 bytes),
    // ikke laengere klartekst — hash det indsendte password med det gemte
    // salt og sammenlign hashes, i stedet for strcmp() paa klartekst.
    uint8_t computed[32];
    rbac_hash_password(password, g_persist_config.rbac_salt[i], computed);
    if (rbac_hash_equal(computed, (const uint8_t *)u->password)) {
      return i;
    }
  }
  return -1;
}

/**
 * Decode Base64 Basic Auth and authenticate.
 * @param auth_value Full "Basic <base64>" string
 * @return User index or -1
 */
static int rbac_auth_from_basic(const char *auth_value)
{
  if (!auth_value || !*auth_value) return -1;

  const char *b64 = strstr(auth_value, "Basic ");
  if (!b64) return -1;
  b64 += 6;

  // Skip whitespace
  while (*b64 == ' ') b64++;

  unsigned char decoded[128];
  size_t decoded_len = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
      (const unsigned char *)b64, strlen(b64)) != 0) {
    return -1;
  }
  decoded[decoded_len] = '\0';

  char *colon = (char *)strchr((const char *)decoded, ':');
  if (!colon) return -1;
  *colon = '\0';

  return rbac_authenticate((const char *)decoded, colon + 1);
}

/**
 * Legacy auth check: compare against HttpConfig username/password.
 * Used when RBAC is disabled.
 */
static bool rbac_legacy_auth(const char *username, const char *password)
{
  if (!g_persist_config.network.http.auth_enabled) return true;
  if (strcmp(username, g_persist_config.network.http.username) != 0) return false;

  // BUG-352: network.http.password er fra schema 22 en raw SHA-256-hash
  // (foerste 32 bytes af det 64-byte feltet), ikke laengere klartekst.
  uint8_t computed[32];
  rbac_hash_password(password, g_persist_config.http_legacy_salt, computed);
  return rbac_hash_equal(computed, (const uint8_t *)g_persist_config.network.http.password);
}

static int rbac_legacy_from_basic(const char *auth_value)
{
  if (!g_persist_config.network.http.auth_enabled) return 99; // No auth = virtual admin

  if (!auth_value || !*auth_value) return -1;

  const char *b64 = strstr(auth_value, "Basic ");
  if (!b64) return -1;
  b64 += 6;
  while (*b64 == ' ') b64++;

  unsigned char decoded[128];
  size_t decoded_len = 0;
  if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
      (const unsigned char *)b64, strlen(b64)) != 0) {
    return -1;
  }
  decoded[decoded_len] = '\0';

  char *colon = (char *)strchr((const char *)decoded, ':');
  if (!colon) return -1;
  *colon = '\0';

  if (rbac_legacy_auth((const char *)decoded, colon + 1)) {
    return 99; // Virtual admin
  }
  return -1;
}

/* ============================================================================
 * SESSION TOKENS (BUG-353, REST API auth-modernisering fase 2)
 *
 * Mirrors sse_events.cpp's sse_token_issue()/sse_token_check() pattern
 * (fixed slot array, spinlock, esp_random() hex token, TTL+GC, evict-oldest
 * when full) — separate table, separate lifetime semantics: this one is a
 * SLIDING 30-min idle timeout (extended on each valid use), not SSE's fixed
 * 5-min one-shot bridge TTL.
 * ============================================================================ */

#define RBAC_SESSION_TOKEN_SLOTS   8        // Max concurrent sessions
#define RBAC_SESSION_TOKEN_LEN     24       // 22 hex chars + null (88-bit, same as SSE's token)
#define RBAC_SESSION_TOKEN_TTL_MS  1800000  // 30 min, SLIDING (renewed on each valid check)

typedef struct {
  char     token[RBAC_SESSION_TOKEN_LEN];
  int      user_idx;
  uint32_t expires_ms;
  bool     active;
} RbacSessionToken;

static RbacSessionToken rbac_session_tokens[RBAC_SESSION_TOKEN_SLOTS];
static portMUX_TYPE rbac_session_token_mux = portMUX_INITIALIZER_UNLOCKED;

static void rbac_session_token_gc_locked(uint32_t now)
{
  for (int i = 0; i < RBAC_SESSION_TOKEN_SLOTS; i++) {
    if (rbac_session_tokens[i].active && (int32_t)(now - rbac_session_tokens[i].expires_ms) >= 0) {
      rbac_session_tokens[i].active = false;
    }
  }
}

const char *rbac_session_token_issue(int user_idx)
{
  if (user_idx < -1) return NULL;  // allow 99 (virtual admin) and valid RBAC idx
  static char out_buf[RBAC_SESSION_TOKEN_LEN];
  uint32_t now = millis();

  taskENTER_CRITICAL(&rbac_session_token_mux);
  rbac_session_token_gc_locked(now);

  int slot = -1;
  for (int i = 0; i < RBAC_SESSION_TOKEN_SLOTS; i++) {
    if (!rbac_session_tokens[i].active) { slot = i; break; }
  }
  if (slot < 0) {
    // All slots busy — evict the one closest to expiring
    uint32_t oldest_exp = 0xFFFFFFFF;
    for (int i = 0; i < RBAC_SESSION_TOKEN_SLOTS; i++) {
      if (rbac_session_tokens[i].expires_ms < oldest_exp) {
        oldest_exp = rbac_session_tokens[i].expires_ms;
        slot = i;
      }
    }
  }

  // 22 hex chars from esp_random (88 bits entropy) — same generation as SSE's token
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  uint32_t r3 = esp_random();
  snprintf(rbac_session_tokens[slot].token, RBAC_SESSION_TOKEN_LEN, "%08lx%08lx%06lx",
           (unsigned long)r1, (unsigned long)r2, (unsigned long)(r3 & 0x00FFFFFF));
  rbac_session_tokens[slot].user_idx = user_idx;
  rbac_session_tokens[slot].expires_ms = now + RBAC_SESSION_TOKEN_TTL_MS;
  rbac_session_tokens[slot].active = true;

  strncpy(out_buf, rbac_session_tokens[slot].token, sizeof(out_buf));
  out_buf[sizeof(out_buf) - 1] = '\0';
  taskEXIT_CRITICAL(&rbac_session_token_mux);
  return out_buf;
}

int rbac_session_token_check(const char *token)
{
  if (!token || !*token) return -1;
  uint32_t now = millis();
  int user_idx = -1;

  taskENTER_CRITICAL(&rbac_session_token_mux);
  rbac_session_token_gc_locked(now);
  for (int i = 0; i < RBAC_SESSION_TOKEN_SLOTS; i++) {
    if (rbac_session_tokens[i].active && strcmp(rbac_session_tokens[i].token, token) == 0) {
      user_idx = rbac_session_tokens[i].user_idx;
      rbac_session_tokens[i].expires_ms = now + RBAC_SESSION_TOKEN_TTL_MS;  // sliding window
      break;
    }
  }
  taskEXIT_CRITICAL(&rbac_session_token_mux);
  return user_idx;
}

void rbac_session_token_revoke(const char *token)
{
  if (!token || !*token) return;
  taskENTER_CRITICAL(&rbac_session_token_mux);
  for (int i = 0; i < RBAC_SESSION_TOKEN_SLOTS; i++) {
    if (rbac_session_tokens[i].active && strcmp(rbac_session_tokens[i].token, token) == 0) {
      rbac_session_tokens[i].active = false;
      break;
    }
  }
  taskEXIT_CRITICAL(&rbac_session_token_mux);
}

int rbac_check_http(httpd_req_t *req)
{
  // Extract Authorization header
  char auth_buf[256] = {0};
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_buf, sizeof(auth_buf)) != ESP_OK) {
    // No header — check if auth is even required
    if (!g_persist_config.rbac.enabled && !g_persist_config.network.http.auth_enabled) {
      return 99; // No auth required, virtual admin
    }
    return -1;
  }

  // BUG-353: session tokens (issued via POST /api/login) checked first — an
  // unambiguous "Bearer " prefix, independent of RBAC-enabled/legacy mode.
  // Falls through to the unchanged Basic Auth logic below for anything else,
  // so scripts/Node-RED/curl using Basic Auth directly are unaffected.
  if (strncmp(auth_buf, "Bearer ", 7) == 0) {
    return rbac_session_token_check(auth_buf + 7);
  }

  if (g_persist_config.rbac.enabled) {
    return rbac_auth_from_basic(auth_buf);
  }
  return rbac_legacy_from_basic(auth_buf);
}

int rbac_check_sse(const char *auth_header)
{
  if (!g_persist_config.rbac.enabled) {
    return rbac_legacy_from_basic(auth_header);
  }
  return rbac_auth_from_basic(auth_header);
}

/* ============================================================================
 * AUTHORIZATION CHECKS
 * ============================================================================ */

bool rbac_has_role(int user_index, uint8_t role_mask)
{
  if (user_index == 99) return true; // Virtual admin (legacy/no-auth)
  if (user_index < 0 || user_index >= RBAC_MAX_USERS) return false;

  const RbacUser *u = &g_persist_config.rbac.users[user_index];
  if (!u->active) return false;
  return (u->roles & role_mask) != 0;
}

bool rbac_has_write(int user_index)
{
  if (user_index == 99) return true; // Virtual admin
  if (user_index < 0 || user_index >= RBAC_MAX_USERS) return false;

  const RbacUser *u = &g_persist_config.rbac.users[user_index];
  if (!u->active) return false;
  return (u->privilege & PRIV_WRITE) != 0;
}

bool rbac_cli_allowed(int user_index, const char *command)
{
  if (!command) return false;
  if (user_index == 99) return true; // Virtual admin

  // If RBAC not enabled, allow everything
  if (!g_persist_config.rbac.enabled) return true;

  if (user_index < 0 || user_index >= RBAC_MAX_USERS) return false;
  const RbacUser *u = &g_persist_config.rbac.users[user_index];
  if (!u->active) return false;

  // Must have CLI role
  if (!(u->roles & ROLE_CLI)) return false;

  // Write privilege allows all commands
  if (u->privilege & PRIV_WRITE) return true;

  // Read-only: only allow safe commands
  // Skip leading whitespace
  while (*command == ' ' || *command == '\t') command++;

  if (strncasecmp(command, "show ", 5) == 0) return true;
  if (strncasecmp(command, "sh ", 3) == 0) return true;
  if (strncasecmp(command, "help", 4) == 0) return true;
  if (strncasecmp(command, "ping ", 5) == 0) return true;
  if (strncasecmp(command, "who", 3) == 0) return true;
  if (command[0] == '?' && (command[1] == '\0' || command[1] == ' ')) return true;

  return false; // All other commands blocked for read-only
}

/* ============================================================================
 * USER MANAGEMENT
 * ============================================================================ */

int rbac_set_user(const char *username, const char *password, uint8_t roles, uint8_t privilege)
{
  if (!username || !password || !username[0] || !password[0]) return -1;
  if (strlen(username) >= RBAC_USERNAME_MAX || strlen(password) >= RBAC_PASSWORD_MAX) return -1;

  RbacConfig *cfg = &g_persist_config.rbac;

  // Check if user already exists — update
  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    if (cfg->users[i].active && strcmp(cfg->users[i].username, username) == 0) {
      // BUG-352: hash med et FRISK salt (aendring af password roterer altid
      // saltet — undgaar at genbruge et gammelt salt paa et nyt password).
      rbac_generate_salt(g_persist_config.rbac_salt[i]);
      rbac_hash_password(password, g_persist_config.rbac_salt[i], (uint8_t *)cfg->users[i].password);
      cfg->users[i].roles = roles;
      cfg->users[i].privilege = privilege;
      ESP_LOGI(TAG, "Updated user '%s' (slot %d, roles=0x%02x, priv=0x%02x)",
        username, i, roles, privilege);
      return i;
    }
  }

  // Find empty slot
  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    if (!cfg->users[i].active) {
      memset(&cfg->users[i], 0, sizeof(RbacUser));
      cfg->users[i].active = 1;
      strncpy(cfg->users[i].username, username, RBAC_USERNAME_MAX - 1);
      rbac_generate_salt(g_persist_config.rbac_salt[i]);
      rbac_hash_password(password, g_persist_config.rbac_salt[i], (uint8_t *)cfg->users[i].password);
      cfg->users[i].roles = roles;
      cfg->users[i].privilege = privilege;
      cfg->user_count++;
      cfg->enabled = 1;  // Auto-enable RBAC when first user is added
      ESP_LOGI(TAG, "Added user '%s' (slot %d, roles=0x%02x, priv=0x%02x)",
        username, i, roles, privilege);
      return i;
    }
  }

  ESP_LOGW(TAG, "Cannot add user '%s': all %d slots full", username, RBAC_MAX_USERS);
  return -1;
}

bool rbac_delete_user(const char *username)
{
  if (!username) return false;
  RbacConfig *cfg = &g_persist_config.rbac;

  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    if (cfg->users[i].active && strcmp(cfg->users[i].username, username) == 0) {
      ESP_LOGI(TAG, "Deleted user '%s' (slot %d)", username, i);
      memset(&cfg->users[i], 0, sizeof(RbacUser));
      if (cfg->user_count > 0) cfg->user_count--;

      // If no users left, disable RBAC
      bool any_active = false;
      for (int j = 0; j < RBAC_MAX_USERS; j++) {
        if (cfg->users[j].active) { any_active = true; break; }
      }
      if (!any_active) {
        cfg->enabled = 0;
        cfg->user_count = 0;
        ESP_LOGW(TAG, "No users remaining — RBAC disabled");
      }
      return true;
    }
  }
  return false;
}

int rbac_find_user(const char *username)
{
  if (!username) return -1;
  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    if (g_persist_config.rbac.users[i].active &&
        strcmp(g_persist_config.rbac.users[i].username, username) == 0) {
      return i;
    }
  }
  return -1;
}

const RbacUser* rbac_get_user(int index)
{
  if (index < 0 || index >= RBAC_MAX_USERS) return NULL;
  if (!g_persist_config.rbac.users[index].active) return NULL;
  return &g_persist_config.rbac.users[index];
}

int rbac_get_user_count(void)
{
  int count = 0;
  for (int i = 0; i < RBAC_MAX_USERS; i++) {
    if (g_persist_config.rbac.users[i].active) count++;
  }
  return count;
}

/* ============================================================================
 * UTILITY: String conversion
 * ============================================================================ */

void rbac_roles_to_str(uint8_t roles, char *buf, size_t buf_len)
{
  buf[0] = '\0';
  if (roles == ROLE_ALL) {
    snprintf(buf, buf_len, "all");
    return;
  }

  bool first = true;
  if (roles & ROLE_API)     { snprintf(buf + strlen(buf), buf_len - strlen(buf), "%sapi", first ? "" : ","); first = false; }
  if (roles & ROLE_CLI)     { snprintf(buf + strlen(buf), buf_len - strlen(buf), "%scli", first ? "" : ","); first = false; }
  if (roles & ROLE_EDITOR)  { snprintf(buf + strlen(buf), buf_len - strlen(buf), "%seditor", first ? "" : ","); first = false; }
  if (roles & ROLE_MONITOR) { snprintf(buf + strlen(buf), buf_len - strlen(buf), "%smonitor", first ? "" : ","); first = false; }
  if (buf[0] == '\0') snprintf(buf, buf_len, "none");
}

uint8_t rbac_parse_roles(const char *str)
{
  if (!str) return 0;
  uint8_t roles = 0;
  if (strcasestr(str, "all"))     return ROLE_ALL;
  if (strcasestr(str, "api"))     roles |= ROLE_API;
  if (strcasestr(str, "cli"))     roles |= ROLE_CLI;
  if (strcasestr(str, "editor"))  roles |= ROLE_EDITOR;
  if (strcasestr(str, "monitor")) roles |= ROLE_MONITOR;
  return roles;
}

uint8_t rbac_parse_privilege(const char *str)
{
  if (!str) return PRIV_READ;
  if (strcasestr(str, "read/write") || strcasestr(str, "rw"))
    return PRIV_RW;
  if (strcasestr(str, "write"))
    return PRIV_WRITE;
  return PRIV_READ;
}

/* ============================================================================
 * MIGRATION
 * ============================================================================ */

void rbac_migrate_legacy(void *persist_config_ptr)
{
  PersistConfig *cfg = (PersistConfig *)persist_config_ptr;

  memset(&cfg->rbac, 0, sizeof(RbacConfig));

  // Migrate existing single user if auth was enabled
  if (cfg->network.http.auth_enabled && cfg->network.http.username[0] != '\0') {
    cfg->rbac.enabled = 1;
    cfg->rbac.user_count = 1;
    cfg->rbac.users[0].active = 1;
    strncpy(cfg->rbac.users[0].username,
            cfg->network.http.username, RBAC_USERNAME_MAX - 1);
    strncpy(cfg->rbac.users[0].password,
            cfg->network.http.password, RBAC_PASSWORD_MAX - 1);
    cfg->rbac.users[0].roles = ROLE_ALL;
    cfg->rbac.users[0].privilege = PRIV_RW;
    ESP_LOGI(TAG, "Migrated legacy user '%s' to RBAC user[0] (all roles, read/write)",
      cfg->network.http.username);
  }
}
