/**
 * @file rbac.cpp
 * @brief Role-Based Access Control (RBAC) implementation (v7.6.2)
 *
 * LAYER 2: Security
 * Centralized authentication and authorization for HTTP, SSE, CLI, and Web UI.
 */

#include <string.h>
#include <stdio.h>
#include <mbedtls/base64.h>
#include <esp_http_server.h>
#include <esp_log.h>

#include "rbac.h"
#include "config_struct.h"
#include "debug.h"

static const char *TAG = "RBAC";

// Forward reference to global config
extern PersistConfig g_persist_config;

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
    if (strcmp(u->username, username) == 0 &&
        strcmp(u->password, password) == 0) {
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
  return (strcmp(username, g_persist_config.network.http.username) == 0 &&
          strcmp(password, g_persist_config.network.http.password) == 0);
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
      strncpy(cfg->users[i].password, password, RBAC_PASSWORD_MAX - 1);
      cfg->users[i].password[RBAC_PASSWORD_MAX - 1] = '\0';
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
      strncpy(cfg->users[i].password, password, RBAC_PASSWORD_MAX - 1);
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
