/**
 * @file ip_acl.cpp
 * @brief IP Access Control List (FEAT-399) — implementering. Se ip_acl.h for
 *        den fulde arkitektur-begrundelse.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>  // millis(), portMUX_TYPE/taskENTER_CRITICAL
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <esp_log.h>

#include "ip_acl.h"
#include "constants.h"
#include "config_struct.h"
#include "config_save.h"
#include "network_config.h"   // network_config_str_to_ip/ip_to_str
#include "rbac.h"              // rbac_session_token_revoke_all()
#include "network_manager.h"   // network_manager_disconnect_telnet_client()
#include "debug.h"

static const char *TAG = "IP_ACL";

extern PersistConfig g_persist_config;

/* ============================================================================
 * PENDING-CONFIRM STAGING STATE
 *
 * KRITISK: en ventende (endnu ikke bekraeftet) ACL-aendring ligger HER, i en
 * helt separat RAM-only kopi — ALDRIG direkte i g_persist_config, foer den er
 * bekraeftet. Begrundelse: kodebasens konvention er at ALT andet i
 * g_persist_config kun persisteres naar brugeren selv trykker "Gem"/`save`,
 * og der findes ~15 uafhaengige kaldesteder af config_save_to_nvs() spredt
 * over REST/CLI/ST Logic. Laa den ventende ACL-regel i g_persist_config,
 * kunne EN HVILKEN SOM HELST anden, urelateret "Gem"-handling andetsteds i
 * UI'et ved et uheld persistere den utestede, potentielt selv-udelukkende
 * regel til NVS FoeR brugeren fik bekraeftet at den ikke laaste dem selv ude
 * — og ramte enheden en genstart i det vindue, ville lockout'en vaere
 * permanent. Med staging helt uden for g_persist_config er dette strukturelt
 * umuligt: intet andet "Gem"-kald kan nogensinde roere ACL-staging.
 * ============================================================================ */

static portMUX_TYPE acl_mux = portMUX_INITIALIZER_UNLOCKED;

static bool     acl_change_pending = false;
static uint32_t acl_pending_deadline_ms = 0;

static uint8_t  acl_staged_enabled = 0;
static uint8_t  acl_staged_rule_count = 0;
static AclRule  acl_staged_rules[ACL_MAX_RULES];

/* ============================================================================
 * FEAT-402: KLADDE-TILSTAND (draft mode)
 *
 * Endnu en RAM-only kopi, samme begrundelse som staging ovenfor (roerer
 * ALDRIG g_persist_config foer et apply) — men modsat staging haandhaeves
 * en kladde ALDRIG (ip_acl_check() kender ikke til den overhovedet), og
 * kraever ingen deadline/session-revoke i sig selv. Kun ÉN af {kladde,
 * pending} kan vaere aktiv ad gangen (se guards i de direkte CRUD-funktioner
 * og i ip_acl_draft_begin()).
 * ============================================================================ */

static bool     acl_draft_active = false;
static uint8_t  acl_draft_enabled = 0;
static uint8_t  acl_draft_rule_count = 0;
static AclRule  acl_draft_rules[ACL_MAX_RULES];

/* ============================================================================
 * CIDR-MATCHING
 * ============================================================================ */

static uint32_t prefix_mask_network_order(uint8_t prefix_len)
{
  if (prefix_len == 0) {
    return 0;  // /0 = match alt — undgaar UB ved shift med fuld bredde
  }
  if (prefix_len >= 32) {
    return 0xFFFFFFFFu;
  }
  uint32_t host_order_mask = 0xFFFFFFFFu << (32 - prefix_len);
  return htonl(host_order_mask);
}

static bool cidr_contains(uint32_t network_addr, uint8_t prefix_len, uint32_t candidate_ip)
{
  uint32_t mask = prefix_mask_network_order(prefix_len);
  return (network_addr & mask) == (candidate_ip & mask);
}

// FEAT-401: foerste-match-vinder-evaluering (klassisk Cisco/iptables/pf-ACL-
// semantik) — itererer regel-listen i INDEX-raekkefolge, springer deaktiverede/
// ikke-matchende (service+CIDR) regler over, og returnerer den FoeRSTE
// matchende regels action. Intet match efter hele listen => default ALLOW
// (uaendret fra v1 — en tom/ufuldstaendig regelliste er stadig sikker, og en
// eksplicit "deny 0.0.0.0/0" nederst i listen er dermed det der reelt lukker
// for alt andet, ikke en implicit systemdefault).
static bool evaluate_allowed(const AclRule *rules, uint8_t count, uint32_t ip, uint8_t service)
{
  for (uint8_t i = 0; i < count; i++) {
    const AclRule *r = &rules[i];
    if (!r->enabled) continue;
    if (r->service != service && r->service != ACL_SVC_ALL) continue;
    if (cidr_contains(r->network_addr, r->prefix_len, ip)) {
      return r->action == ACL_ACTION_ALLOW;
    }
  }
  return true;  // Intet match => default ALLOW
}

/* ============================================================================
 * HAaNDHAEVELSE
 * ============================================================================ */

bool ip_acl_check(uint32_t ip_network_order, uint8_t service)
{
  bool enabled;
  bool allowed;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_change_pending) {
    enabled = acl_staged_enabled;
    allowed = !enabled || evaluate_allowed(acl_staged_rules, acl_staged_rule_count, ip_network_order, service);
  } else {
    enabled = g_persist_config.acl_enabled;
    allowed = !enabled || evaluate_allowed(g_persist_config.acl_rules, g_persist_config.acl_rule_count, ip_network_order, service);
  }
  taskEXIT_CRITICAL(&acl_mux);

  return allowed;
}

bool ip_acl_check_req(httpd_req_t *req, uint8_t service)
{
  int sockfd = httpd_req_to_sockfd(req);
  struct sockaddr_in6 addr6;
  socklen_t addr_len = sizeof(addr6);
  if (sockfd < 0 || getpeername(sockfd, (struct sockaddr *)&addr6, &addr_len) != 0) {
    return true;  // Kan ikke afgoere IP — fail-open, samme som rate-limiteren
  }

  uint32_t ip;
  if (addr6.sin6_family == AF_INET) {
    ip = ((struct sockaddr_in *)&addr6)->sin_addr.s_addr;
  } else if (addr6.sin6_family == AF_INET6) {
    // IPv4-mapped IPv6 (::ffff:x.x.x.x) — samme udpakning som
    // http_get_client_info() (api_handlers.cpp) bruger.
    struct in_addr mapped;
    memcpy(&mapped, &addr6.sin6_addr.un.u32_addr[3], 4);
    ip = mapped.s_addr;
  } else {
    return true;
  }

  return ip_acl_check(ip, service);
}

void ip_acl_tick(void)
{
  bool timed_out = false;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_change_pending && (int32_t)(millis() - acl_pending_deadline_ms) >= 0) {
    acl_change_pending = false;
    timed_out = true;
  }
  taskEXIT_CRITICAL(&acl_mux);

  if (timed_out) {
    debug_println("IP ACL: pending-aendring IKKE bekraeftet i tide — automatisk rullet tilbage");
    ESP_LOGW(TAG, "Pending ACL change timed out — auto rolled back");
  }
}

/* ============================================================================
 * INTERN HJAELP: afgoer om en regel/tilstand-aendring "gates"
 * ============================================================================ */

static bool service_affects_mgmt(uint8_t service)
{
  return service == ACL_SVC_HTTP || service == ACL_SVC_TELNET || service == ACL_SVC_ALL;
}

// Starter en pending-periode: kopierer den AKTUELLE bekraeftede tilstand ind
// i staging som udgangspunkt (kaldt FOeR selve mutationen anvendes paa
// staging-kopien af den specifikke CRUD-funktion).
static void begin_pending_from_confirmed_locked(void)
{
  acl_staged_enabled = g_persist_config.acl_enabled;
  acl_staged_rule_count = g_persist_config.acl_rule_count;
  memcpy(acl_staged_rules, g_persist_config.acl_rules, sizeof(acl_staged_rules));
  acl_change_pending = true;
  acl_pending_deadline_ms = millis() + ACL_PENDING_CONFIRM_TIMEOUT_MS;
}

// Kaldt EFTER acl_change_pending er sat — tvinger frisk login/reconnect paa
// de management-flader den nye regel paavirker.
static void force_fresh_auth_on_mgmt_surfaces(void)
{
  rbac_session_token_revoke_all();
  network_manager_disconnect_telnet_client();
}

/* ============================================================================
 * REGEL-CRUD
 * ============================================================================ */

IpAclResult ip_acl_set_enabled(bool enabled, bool *out_now_pending)
{
  if (out_now_pending) *out_now_pending = false;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }

  if (!enabled) {
    // Fra: kan aldrig forårsage ny lockout — anvendes straks, ingen gate.
    g_persist_config.acl_enabled = 0;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  // Til: "gates" hvis mindst én aktiv HTTP/TELNET/ALL-regel findes.
  bool has_mgmt_rule = false;
  for (uint8_t i = 0; i < g_persist_config.acl_rule_count; i++) {
    if (g_persist_config.acl_rules[i].enabled && service_affects_mgmt(g_persist_config.acl_rules[i].service)) {
      has_mgmt_rule = true;
      break;
    }
  }

  if (!has_mgmt_rule) {
    g_persist_config.acl_enabled = 1;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  acl_staged_enabled = 1;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  return ACL_ACTION_OK;
}

// FEAT-401 gating-princip (brugt konsekvent i alle CRUD-funktioner nedenfor):
// en ALLOW-regel kan afskaerme en DENY-regel laengere nede i listen — fjerner/
// deaktiverer/flytter man en aktiv ALLOW vaek fra sin plads, kan trafik der
// foer slap igennem blive ramt af en DENY den ikke naaede foer (stramning).
// En DENY-regel kan derimod KUN nogensinde stramme ved at blive TILFOeJET/
// AKTIVERET — aldrig ved at forsvinde/deaktiveres (fjerner man en DENY, bliver
// resultatet kun mere aabent). Se planens §3 for den fulde begrundelse for
// hvorfor MOVE bevidst IKKE forsoeger en tilsvarende praecis retningsanalyse.

IpAclResult ip_acl_rule_add(uint32_t network_addr, uint8_t prefix_len, uint8_t service,
                             uint8_t action, bool enabled, bool *out_now_pending, int *out_index)
{
  if (out_now_pending) *out_now_pending = false;
  if (prefix_len > 32) return ACL_ACTION_ERR_INVALID;
  if (service > ACL_SVC_ALL) return ACL_ACTION_ERR_INVALID;
  if (action != ACL_ACTION_ALLOW && action != ACL_ACTION_DENY) return ACL_ACTION_ERR_INVALID;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }

  if (g_persist_config.acl_rule_count >= ACL_MAX_RULES) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_FULL;
  }

  // Tilfoejelse af en NY, aktiv mgmt-regel kan altid aendre udfaldet for
  // eksisterende trafik (uanset action — se rule_matters_for_gating()) => gates.
  bool gates = enabled && service_affects_mgmt(service);

  if (!gates) {
    AclRule *r = &g_persist_config.acl_rules[g_persist_config.acl_rule_count];
    r->network_addr = network_addr;
    r->prefix_len = prefix_len;
    r->service = service;
    r->enabled = enabled ? 1 : 0;
    r->action = action;
    int idx = g_persist_config.acl_rule_count;
    g_persist_config.acl_rule_count++;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    if (out_index) *out_index = idx;
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  AclRule *r = &acl_staged_rules[acl_staged_rule_count];
  r->network_addr = network_addr;
  r->prefix_len = prefix_len;
  r->service = service;
  r->enabled = 1;
  r->action = action;
  int idx = acl_staged_rule_count;
  acl_staged_rule_count++;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  if (out_index) *out_index = idx;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_rule_set_enabled(int index, bool enabled, bool *out_now_pending)
{
  if (out_now_pending) *out_now_pending = false;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }
  if (index < 0 || index >= g_persist_config.acl_rule_count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }

  AclRule *existing = &g_persist_config.acl_rules[index];
  bool gates;
  if (enabled && !existing->enabled) {
    // Aktivering af en mgmt-regel: gates altid (samme som en tilfoejelse),
    // uanset action.
    gates = service_affects_mgmt(existing->service);
  } else if (!enabled && existing->enabled) {
    // Deaktivering: kan KUN stramme hvis det er en ALLOW-regel (se
    // rule_matters_for_gating()) — en DENY-regel kan trygt fjernes/
    // deaktiveres uden bekraeftelse, praecis som i v1.
    gates = service_affects_mgmt(existing->service) && existing->action == ACL_ACTION_ALLOW;
  } else {
    gates = false;  // Ingen reel tilstandsaendring (allerede paa/fra)
  }

  if (!gates) {
    existing->enabled = enabled ? 1 : 0;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  acl_staged_rules[index].enabled = enabled ? 1 : 0;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_rule_edit(int index, uint32_t network_addr, uint8_t prefix_len, uint8_t service,
                              uint8_t action, bool enabled, bool *out_now_pending)
{
  if (out_now_pending) *out_now_pending = false;
  if (prefix_len > 32) return ACL_ACTION_ERR_INVALID;
  if (service > ACL_SVC_ALL) return ACL_ACTION_ERR_INVALID;
  if (action != ACL_ACTION_ALLOW && action != ACL_ACTION_DENY) return ACL_ACTION_ERR_INVALID;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }
  if (index < 0 || index >= g_persist_config.acl_rule_count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }

  const AclRule *old = &g_persist_config.acl_rules[index];
  // En redigering kan baade "fjerne" den gamle regels effekt og "tilfoeje"
  // den nyes — gates hvis ENTEN siden var en aktiv ALLOW-regel (fjernelsen
  // kan stramme) ELLER den nye tilstand er en aktiv mgmt-regel (tilfoejelsen
  // kan stramme, uanset dens action).
  bool old_mattered = old->enabled && service_affects_mgmt(old->service) && old->action == ACL_ACTION_ALLOW;
  bool new_matters = enabled && service_affects_mgmt(service);
  bool gates = old_mattered || new_matters;

  if (!gates) {
    AclRule *r = &g_persist_config.acl_rules[index];
    r->network_addr = network_addr;
    r->prefix_len = prefix_len;
    r->service = service;
    r->action = action;
    r->enabled = enabled ? 1 : 0;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  AclRule *r = &acl_staged_rules[index];
  r->network_addr = network_addr;
  r->prefix_len = prefix_len;
  r->service = service;
  r->action = action;
  r->enabled = enabled ? 1 : 0;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_rule_move(int from_index, int to_index, bool *out_now_pending)
{
  if (out_now_pending) *out_now_pending = false;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }
  int count = g_persist_config.acl_rule_count;
  if (from_index < 0 || from_index >= count || to_index < 0 || to_index >= count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }
  if (from_index == to_index) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_OK;  // No-op
  }

  // FEAT-401: en flytning kan i princippet aendre om en DENY vinder foerst
  // over en ALLOW (eller omvendt) for trafik der matcher begge — bevidst
  // IKKE forsoegt beregnet praecist retnings-/action-afhaengigt (se planens
  // begrundelse: for kompleks en analyse til at stole 100% paa i noget
  // saa sikkerhedskritisk). Enhver flytning af en AKTIV mgmt-regel gates,
  // uanset retning eller action.
  const AclRule *moved = &g_persist_config.acl_rules[from_index];
  bool gates = moved->enabled && service_affects_mgmt(moved->service);

  if (!gates) {
    AclRule tmp = g_persist_config.acl_rules[from_index];
    if (from_index < to_index) {
      for (int i = from_index; i < to_index; i++) g_persist_config.acl_rules[i] = g_persist_config.acl_rules[i + 1];
    } else {
      for (int i = from_index; i > to_index; i--) g_persist_config.acl_rules[i] = g_persist_config.acl_rules[i - 1];
    }
    g_persist_config.acl_rules[to_index] = tmp;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  AclRule tmp = acl_staged_rules[from_index];
  if (from_index < to_index) {
    for (int i = from_index; i < to_index; i++) acl_staged_rules[i] = acl_staged_rules[i + 1];
  } else {
    for (int i = from_index; i > to_index; i--) acl_staged_rules[i] = acl_staged_rules[i - 1];
  }
  acl_staged_rules[to_index] = tmp;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_rule_delete(int index, bool *out_now_pending)
{
  if (out_now_pending) *out_now_pending = false;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }
  if (index < 0 || index >= g_persist_config.acl_rule_count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }

  // FEAT-401: sletning af en ALLOW-regel kan afsloere en DENY laengere nede
  // (stramning) => gates. Sletning af en DENY-regel (eller en deaktiveret/
  // SSE-only regel) kan kun aabne adgang => aldrig gated, som i v1.
  const AclRule *existing = &g_persist_config.acl_rules[index];
  bool gates = existing->enabled && service_affects_mgmt(existing->service) && existing->action == ACL_ACTION_ALLOW;

  if (!gates) {
    // Shift-ned, samme moenster som registers_persist_group_delete().
    for (int i = index; i < g_persist_config.acl_rule_count - 1; i++) {
      g_persist_config.acl_rules[i] = g_persist_config.acl_rules[i + 1];
    }
    memset(&g_persist_config.acl_rules[g_persist_config.acl_rule_count - 1], 0, sizeof(AclRule));
    g_persist_config.acl_rule_count--;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    return ACL_ACTION_OK;
  }

  begin_pending_from_confirmed_locked();
  for (int i = index; i < acl_staged_rule_count - 1; i++) {
    acl_staged_rules[i] = acl_staged_rules[i + 1];
  }
  memset(&acl_staged_rules[acl_staged_rule_count - 1], 0, sizeof(AclRule));
  acl_staged_rule_count--;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_confirm(void)
{
  taskENTER_CRITICAL(&acl_mux);
  if (!acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NOT_PENDING;
  }

  g_persist_config.acl_enabled = acl_staged_enabled;
  g_persist_config.acl_rule_count = acl_staged_rule_count;
  memcpy(g_persist_config.acl_rules, acl_staged_rules, sizeof(g_persist_config.acl_rules));
  acl_change_pending = false;
  taskEXIT_CRITICAL(&acl_mux);

  config_save_to_nvs(&g_persist_config);
  debug_println("IP ACL: pending-aendring bekraeftet og persisteret");
  return ACL_ACTION_OK;
}

/* ============================================================================
 * FEAT-402: KLADDE-TILSTAND (draft mode) — se ip_acl.h for arkitekturen.
 * Alle kladde-CRUD-funktionerne herunder er BEVIDST uden gating/haandhaevelse
 * — det er hele pointen. Kun ip_acl_draft_apply() kan paavirke haandhaevelsen.
 * ============================================================================ */

IpAclResult ip_acl_draft_begin(void)
{
  taskENTER_CRITICAL(&acl_mux);
  if (acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_DRAFT_ACTIVE;
  }
  if (acl_change_pending) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }

  acl_draft_enabled = g_persist_config.acl_enabled;
  acl_draft_rule_count = g_persist_config.acl_rule_count;
  memcpy(acl_draft_rules, g_persist_config.acl_rules, sizeof(acl_draft_rules));
  acl_draft_active = true;
  taskEXIT_CRITICAL(&acl_mux);

  debug_println("IP ACL: kladde startet");
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_discard(void)
{
  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  acl_draft_active = false;
  acl_draft_rule_count = 0;
  taskEXIT_CRITICAL(&acl_mux);

  debug_println("IP ACL: kladde kasseret");
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_set_enabled(bool enabled)
{
  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  acl_draft_enabled = enabled ? 1 : 0;
  taskEXIT_CRITICAL(&acl_mux);
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_rule_add(uint32_t network_addr, uint8_t prefix_len, uint8_t service,
                                   uint8_t action, bool enabled, int *out_index)
{
  if (prefix_len > 32) return ACL_ACTION_ERR_INVALID;
  if (service > ACL_SVC_ALL) return ACL_ACTION_ERR_INVALID;
  if (action != ACL_ACTION_ALLOW && action != ACL_ACTION_DENY) return ACL_ACTION_ERR_INVALID;

  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  if (acl_draft_rule_count >= ACL_MAX_RULES) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_FULL;
  }

  AclRule *r = &acl_draft_rules[acl_draft_rule_count];
  r->network_addr = network_addr;
  r->prefix_len = prefix_len;
  r->service = service;
  r->enabled = enabled ? 1 : 0;
  r->action = action;
  int idx = acl_draft_rule_count;
  acl_draft_rule_count++;
  taskEXIT_CRITICAL(&acl_mux);

  if (out_index) *out_index = idx;
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_rule_edit(int index, uint32_t network_addr, uint8_t prefix_len,
                                    uint8_t service, uint8_t action, bool enabled)
{
  if (prefix_len > 32) return ACL_ACTION_ERR_INVALID;
  if (service > ACL_SVC_ALL) return ACL_ACTION_ERR_INVALID;
  if (action != ACL_ACTION_ALLOW && action != ACL_ACTION_DENY) return ACL_ACTION_ERR_INVALID;

  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  if (index < 0 || index >= acl_draft_rule_count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }

  AclRule *r = &acl_draft_rules[index];
  r->network_addr = network_addr;
  r->prefix_len = prefix_len;
  r->service = service;
  r->action = action;
  r->enabled = enabled ? 1 : 0;
  taskEXIT_CRITICAL(&acl_mux);
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_rule_move(int from_index, int to_index)
{
  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  int count = acl_draft_rule_count;
  if (from_index < 0 || from_index >= count || to_index < 0 || to_index >= count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }
  if (from_index == to_index) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_OK;
  }

  AclRule tmp = acl_draft_rules[from_index];
  if (from_index < to_index) {
    for (int i = from_index; i < to_index; i++) acl_draft_rules[i] = acl_draft_rules[i + 1];
  } else {
    for (int i = from_index; i > to_index; i--) acl_draft_rules[i] = acl_draft_rules[i - 1];
  }
  acl_draft_rules[to_index] = tmp;
  taskEXIT_CRITICAL(&acl_mux);
  return ACL_ACTION_OK;
}

IpAclResult ip_acl_draft_rule_delete(int index)
{
  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  if (index < 0 || index >= acl_draft_rule_count) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_INVALID;
  }

  for (int i = index; i < acl_draft_rule_count - 1; i++) {
    acl_draft_rules[i] = acl_draft_rules[i + 1];
  }
  memset(&acl_draft_rules[acl_draft_rule_count - 1], 0, sizeof(AclRule));
  acl_draft_rule_count--;
  taskEXIT_CRITICAL(&acl_mux);
  return ACL_ACTION_OK;
}

// Reducerer en regelliste til dens mgmt-relevante (service != SSE) delmaengde,
// i bevaret indbyrdes raekkefoelge — en flettet SSE-regel mellem to HTTP-
// regler aendrer ikke deres indbyrdes evaluering (evaluate_allowed()s
// service-filter), og skal derfor ikke tage del i sammenligningen.
struct MgmtRuleKey {
  uint32_t network_addr;
  uint8_t  prefix_len;
  uint8_t  service;
  uint8_t  action;
  uint8_t  enabled;
};

static uint8_t build_mgmt_subset(const AclRule *rules, uint8_t count, MgmtRuleKey *out)
{
  uint8_t n = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (!service_affects_mgmt(rules[i].service)) continue;
    out[n].network_addr = rules[i].network_addr;
    out[n].prefix_len = rules[i].prefix_len;
    out[n].service = rules[i].service;
    out[n].action = rules[i].action;
    out[n].enabled = rules[i].enabled;
    n++;
  }
  return n;
}

static bool mgmt_subset_differs(const AclRule *a, uint8_t na, const AclRule *b, uint8_t nb)
{
  MgmtRuleKey ka[ACL_MAX_RULES];
  MgmtRuleKey kb[ACL_MAX_RULES];
  uint8_t nka = build_mgmt_subset(a, na, ka);
  uint8_t nkb = build_mgmt_subset(b, nb, kb);
  if (nka != nkb) return true;
  return nka > 0 && memcmp(ka, kb, nka * sizeof(MgmtRuleKey)) != 0;
}

IpAclResult ip_acl_draft_apply(uint32_t caller_ip_or_0, bool *out_now_pending, bool *out_self_match_warning)
{
  if (out_now_pending) *out_now_pending = false;
  if (out_self_match_warning) *out_self_match_warning = false;

  taskENTER_CRITICAL(&acl_mux);
  if (!acl_draft_active) {
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_NO_DRAFT;
  }
  if (acl_change_pending) {
    // Skal strukturelt ikke kunne indtraeffe (draft_begin blokerer naar en
    // aendring allerede afventer) — bevaret som forsvar-i-dybden.
    taskEXIT_CRITICAL(&acl_mux);
    return ACL_ACTION_ERR_PENDING;
  }

  bool has_active_mgmt_rule_in_draft = false;
  for (uint8_t i = 0; i < acl_draft_rule_count; i++) {
    if (acl_draft_rules[i].enabled && service_affects_mgmt(acl_draft_rules[i].service)) {
      has_active_mgmt_rule_in_draft = true;
      break;
    }
  }

  bool gates;
  if (!acl_draft_enabled) {
    // Resulterende tilstand er ACL FRA — kan aldrig laase nogen ude, uanset
    // hvad reglerne siger (ip_acl_check() fail-aabner helt naar enabled=0).
    gates = false;
  } else if (!g_persist_config.acl_enabled) {
    // Ny-aktivering af hele ACL'en: gates hvis kladden indeholder mindst én
    // aktiv HTTP/TELNET/ALL-regel (samme princip som ip_acl_set_enabled()).
    gates = has_active_mgmt_rule_in_draft;
  } else {
    // Allerede TIL foer og efter: gates kun hvis den mgmt-relevante
    // delmaengde reelt er anderledes (kun-SSE-aendringer gater ikke).
    gates = mgmt_subset_differs(acl_draft_rules, acl_draft_rule_count,
                                 g_persist_config.acl_rules, g_persist_config.acl_rule_count);
  }

  if (caller_ip_or_0 != 0 && out_self_match_warning) {
    bool allowed_before = !g_persist_config.acl_enabled ||
        evaluate_allowed(g_persist_config.acl_rules, g_persist_config.acl_rule_count, caller_ip_or_0, ACL_SVC_HTTP);
    bool allowed_after = !acl_draft_enabled ||
        evaluate_allowed(acl_draft_rules, acl_draft_rule_count, caller_ip_or_0, ACL_SVC_HTTP);
    *out_self_match_warning = allowed_before && !allowed_after;
  }

  if (!gates) {
    g_persist_config.acl_enabled = acl_draft_enabled;
    g_persist_config.acl_rule_count = acl_draft_rule_count;
    memcpy(g_persist_config.acl_rules, acl_draft_rules, sizeof(g_persist_config.acl_rules));
    acl_draft_active = false;
    taskEXIT_CRITICAL(&acl_mux);
    config_save_to_nvs(&g_persist_config);
    debug_println("IP ACL: kladde anvendt direkte (ingen mgmt-relevant aendring)");
    return ACL_ACTION_OK;
  }

  // Overgiver kladden til den EKSISTERENDE pending-confirm-mekanisme som ÉT
  // atomisk skridt — samme haandhaevelse/session-revoke/5-min.-vindue/
  // auto-rollback som enhver anden gated FEAT-399/401-mutation, nu blot
  // anvendt paa hele kladdens slutresultat i stedet for et enkelt deltrin.
  acl_staged_enabled = acl_draft_enabled;
  acl_staged_rule_count = acl_draft_rule_count;
  memcpy(acl_staged_rules, acl_draft_rules, sizeof(acl_staged_rules));
  acl_change_pending = true;
  acl_pending_deadline_ms = millis() + ACL_PENDING_CONFIRM_TIMEOUT_MS;
  acl_draft_active = false;
  taskEXIT_CRITICAL(&acl_mux);

  force_fresh_auth_on_mgmt_surfaces();
  if (out_now_pending) *out_now_pending = true;
  debug_println("IP ACL: kladde anvendt — afventer bekraeftelse (frisk login paakraevet)");
  return ACL_ACTION_OK;
}

bool ip_acl_draft_is_active(void)
{
  bool active;
  taskENTER_CRITICAL(&acl_mux);
  active = acl_draft_active;
  taskEXIT_CRITICAL(&acl_mux);
  return active;
}

bool ip_acl_draft_get_enabled(void)
{
  bool enabled;
  taskENTER_CRITICAL(&acl_mux);
  enabled = acl_draft_enabled;
  taskEXIT_CRITICAL(&acl_mux);
  return enabled;
}

uint8_t ip_acl_draft_get_rule_count(void)
{
  uint8_t count;
  taskENTER_CRITICAL(&acl_mux);
  count = acl_draft_rule_count;
  taskEXIT_CRITICAL(&acl_mux);
  return count;
}

bool ip_acl_draft_get_rule(uint8_t index, AclRule *out)
{
  bool valid;
  taskENTER_CRITICAL(&acl_mux);
  valid = acl_draft_active && index < acl_draft_rule_count;
  if (valid && out) *out = acl_draft_rules[index];
  taskEXIT_CRITICAL(&acl_mux);
  return valid;
}

/* ============================================================================
 * STATUS/VISNING
 * ============================================================================ */

bool ip_acl_is_pending(void)
{
  bool pending;
  taskENTER_CRITICAL(&acl_mux);
  pending = acl_change_pending;
  taskEXIT_CRITICAL(&acl_mux);
  return pending;
}

uint32_t ip_acl_pending_remaining_ms(void)
{
  uint32_t remaining = 0;
  taskENTER_CRITICAL(&acl_mux);
  if (acl_change_pending) {
    uint32_t now = millis();
    remaining = ((int32_t)(acl_pending_deadline_ms - now) > 0) ? (acl_pending_deadline_ms - now) : 0;
  }
  taskEXIT_CRITICAL(&acl_mux);
  return remaining;
}

bool ip_acl_get_effective_enabled(void)
{
  bool enabled;
  taskENTER_CRITICAL(&acl_mux);
  enabled = acl_change_pending ? acl_staged_enabled : g_persist_config.acl_enabled;
  taskEXIT_CRITICAL(&acl_mux);
  return enabled;
}

uint8_t ip_acl_get_effective_rule_count(void)
{
  uint8_t count;
  taskENTER_CRITICAL(&acl_mux);
  count = acl_change_pending ? acl_staged_rule_count : g_persist_config.acl_rule_count;
  taskEXIT_CRITICAL(&acl_mux);
  return count;
}

// Kopierer ind i CALLERENS buffer (ikke en statisk intern) — ip_acl_check()
// og disse status-funktioner koeres fra flere forskellige FreeRTOS-tasks
// (httpd worker, loop()-tasken, SSE-acceptor-tasken), saa en delt statisk
// retur-buffer ville race ved samtidige kald (fx CLI `show acl` og REST
// GET /api/acl paa samme tid).
bool ip_acl_get_effective_rule(uint8_t index, AclRule *out)
{
  bool valid;

  taskENTER_CRITICAL(&acl_mux);
  if (acl_change_pending) {
    valid = index < acl_staged_rule_count;
    if (valid && out) *out = acl_staged_rules[index];
  } else {
    valid = index < g_persist_config.acl_rule_count;
    if (valid && out) *out = g_persist_config.acl_rules[index];
  }
  taskEXIT_CRITICAL(&acl_mux);

  return valid;
}

/* ============================================================================
 * CIDR/SERVICE PARSING OG FORMATERING
 * ============================================================================ */

bool ip_acl_parse_cidr(const char *str, uint32_t *out_network_addr, uint8_t *out_prefix_len)
{
  if (!str || !*str || !out_network_addr || !out_prefix_len) return false;

  char buf[32];
  strncpy(buf, str, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  uint8_t prefix = 32;
  char *slash = strchr(buf, '/');
  if (slash) {
    *slash = '\0';
    char *endptr = NULL;
    long val = strtol(slash + 1, &endptr, 10);
    if (endptr == slash + 1 || *endptr != '\0' || val < 0 || val > 32) {
      return false;
    }
    prefix = (uint8_t)val;
  }

  uint32_t ip;
  if (!network_config_str_to_ip(buf, &ip)) {
    return false;
  }

  // Normalisér: nulstil vaertsbittene, saa lagrede regler altid er den rene
  // netvaerksadresse (undgaar overraskende mismatches hvis en bruger
  // indtaster "192.168.1.5/24" og forventer det matcher hele 192.168.1.0/24).
  uint32_t mask = prefix_mask_network_order(prefix);
  *out_network_addr = ip & mask;
  *out_prefix_len = prefix;
  return true;
}

void ip_acl_format_cidr(uint32_t network_addr, uint8_t prefix_len, char *out, size_t out_len)
{
  if (!out || out_len < 4) return;
  char ip_str[16];
  network_config_ip_to_str(network_addr, ip_str);
  snprintf(out, out_len, "%s/%u", ip_str, prefix_len);
}

const char *ip_acl_service_name(uint8_t service)
{
  switch (service) {
    case ACL_SVC_HTTP:   return "http";
    case ACL_SVC_TELNET: return "telnet";
    case ACL_SVC_SSE:    return "sse";
    case ACL_SVC_ALL:    return "all";
    default:              return NULL;
  }
}

const char *ip_acl_action_name(uint8_t action)
{
  switch (action) {
    case ACL_ACTION_ALLOW: return "allow";
    case ACL_ACTION_DENY:  return "deny";
    default:                return NULL;
  }
}

bool ip_acl_parse_action(const char *str, uint8_t *out_action)
{
  if (!str || !out_action) return false;
  if (!strcasecmp(str, "allow") || !strcasecmp(str, "permit")) { *out_action = ACL_ACTION_ALLOW; return true; }
  if (!strcasecmp(str, "deny") || !strcasecmp(str, "block"))   { *out_action = ACL_ACTION_DENY;  return true; }
  return false;
}

bool ip_acl_cidr_matches(uint32_t network_addr, uint8_t prefix_len, uint32_t ip_network_order)
{
  return cidr_contains(network_addr, prefix_len, ip_network_order);
}

bool ip_acl_parse_service(const char *str, uint8_t *out_service)
{
  if (!str || !out_service) return false;
  if (!strcasecmp(str, "http"))   { *out_service = ACL_SVC_HTTP;   return true; }
  if (!strcasecmp(str, "telnet")) { *out_service = ACL_SVC_TELNET; return true; }
  if (!strcasecmp(str, "sse"))    { *out_service = ACL_SVC_SSE;    return true; }
  if (!strcasecmp(str, "all"))    { *out_service = ACL_SVC_ALL;    return true; }
  return false;
}
