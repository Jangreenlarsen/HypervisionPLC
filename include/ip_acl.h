/**
 * @file ip_acl.h
 * @brief IP Access Control List (FEAT-399) — IP/CIDR-baseret blokering pr.
 *        netvaerkstjeneste, med et lockout-recovery-flow for "management"-
 *        paavirkende aendringer (HTTP/Telnet).
 *
 * LAYER 2: Security (samme lag som rbac.cpp — begge daekker autorisation,
 * ikke autentificering; ACL afgoer "maa denne IP overhovedet PROeVE at tale
 * med denne tjeneste", RBAC afgoer "hvem er brugeren, og hvad maa de").
 *
 * DESIGN (FEAT-401: ordnet permit/deny-firewall) — hver AclRule (types.h)
 * angiver en CIDR + hvilken tjeneste den gaelder for + en action (ALLOW/
 * permit eller DENY/deny, constants.h). Regel-listen evalueres i INDEX-
 * raekkefolge; foerste AKTIVE, MATCHENDE regel vinder (klassisk Cisco/
 * iptables/pf-semantik) og dens action afgoer udfaldet. Ingen matchende
 * regel efter hele listen => default ALLOW (uaendret v1-adfaerd — en tom
 * regelliste er stadig sikker; en eksplicit "deny 0.0.0.0/0" nederst er
 * dermed det der reelt lukker for resten, ikke en implicit systemdefault).
 * ACL er OFF (acl_enabled=0) by default, ogsaa for allerede-konfigurerede
 * enheder.
 *
 * HAaNDHAEVELSES-OVERFLADER (fire i alt, tre forskellige mekanismer):
 * - Plain HTTP: `httpd_config.open_fn` (src/http_server.cpp) — kaldes af
 *   ESP-IDF lige efter accept(), foer noget parses. 100% sikkert: non-ESP_OK
 *   retur laader RAMMEN selv lukke socket'en (httpd_sess_delete()).
 * - HTTPS: **IKKE** paa accept/handshake-laget. Verificeret direkte i
 *   ESP-IDF's esp_https_server-kilde: httpd_ssl_open() udfoerer HELE
 *   TLS-haandtrykket FOeR den kalder et evt. chainet open_fn, OG ignorerer
 *   dets returvaerdi fuldstaendigt (returnerer altid ESP_OK til rammen). At
 *   forsoege at afvise ved selv at kalde close(sockfd) her ville frigive
 *   fd-nummeret til OS'et mens esp_tls-konteksten stadig tror forbindelsen
 *   lever — praecis den brug-efter-frigivelse-fejlklasse https_wrapper.c's
 *   eget filhoved allerede advarer imod. Haandhaeves i stedet paa
 *   REQUEST-laget (CHECK_AUTH*-makroerne, src/api_handlers.cpp) — kendt,
 *   dokumenteret restbegraensning: en blokeret IP kan stadig gennemfoere
 *   selve TCP+TLS-haandtrykket foer afvisning.
 * - Telnet: src/tcp_server.cpp, tcp_server_accept() — lige efter accept(),
 *   FOeR den ene klientplads optages.
 * - SSE: src/sse_events.cpp, sse_accept_task() — lige efter accept(), foer
 *   ressourceallokering.
 *
 * LOCKOUT-RECOVERY (pending-confirm/auto-rollback): en aendring der kan NYT
 * begraense HTTP/Telnet-adgang ("gates") anvendes STRAKS til en RAM-only
 * staging-kopi (IKKE g_persist_config — se implementeringskommentaren i
 * ip_acl.cpp for hvorfor), haandhaeves med det samme (saa den reelt kan
 * testes), men persisteres FoeRST naar eksplicit bekraeftet inden for
 * ACL_PENDING_CONFIRM_TIMEOUT_MS — ellers rulles den automatisk tilbage
 * (ip_acl_tick(), kaldt fra hovedloopet). Bekraeftelse kraever en FRISK
 * RBAC-session (alle eksisterende revokeres naar en gated aendring staged'es,
 * jf. rbac_session_token_revoke_all()), som i sig selv er beviset for at
 * login stadig virker under de nye regler.
 */

#ifndef IP_ACL_H
#define IP_ACL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <esp_http_server.h>  // httpd_req_t (ip_acl_check_req())
#include "types.h"  // AclRule

typedef enum {
  ACL_ACTION_OK = 0,
  ACL_ACTION_ERR_PENDING,      // en anden aendring afventer allerede bekraeftelse
  ACL_ACTION_ERR_FULL,         // regel-tabellen er fuld (ACL_MAX_RULES)
  ACL_ACTION_ERR_INVALID,      // ugyldigt index/CIDR/service
  ACL_ACTION_ERR_NOT_PENDING,  // confirm kaldt uden nogen ventende aendring
  ACL_ACTION_ERR_DRAFT_ACTIVE, // FEAT-402: en kladde er i gang — brug kladde-CRUD'en,
                                // eller afslut den (apply/discard) foer direkte mutation
  ACL_ACTION_ERR_NO_DRAFT      // FEAT-402: kladde-operation kaldt uden aktiv kladde
} IpAclResult;

/* ============================================================================
 * HAaNDHAEVELSE — kaldt fra de fire accept/request-hooks
 * ============================================================================ */

/**
 * @param ip_network_order Klientens IPv4-adresse i NETWORK byte order (samme
 *        format som inet_aton()/sockaddr_in.sin_addr.s_addr).
 * @return true = tilladt, false = blokeret af en aktiv, matchende regel.
 *         Konsulterer den "effektive" tilstand (staging hvis en aendring
 *         afventer bekraeftelse, ellers den bekraeftede g_persist_config).
 */
bool ip_acl_check(uint32_t ip_network_order, uint8_t service);

/**
 * Bekvemmelighedsvariant af ip_acl_check() der selv udtraekker klient-IP'en fra
 * en httpd_req_t (getpeername() + IPv4-mapped-IPv6-udpakning). BEVIDST
 * genbrugt paa TVAeRS af BÅDE de statiske side-handlers (web_system.cpp
 * m.fl. — INGEN af dem kalder CHECK_AUTH* i dag, kun denne, saa siden ALDRIG
 * serveres til en blokeret IP) OG api_handlers.cpp's CHECK_IP_ACL-makro:
 * en blokeret IP maa IKKE naa saa langt som til at faa selve login-skaermen
 * at se paa HTTPS (plain HTTP afvises allerede endnu tidligere, i
 * httpd_config.open_fn, FOeR noget som helst serveres — se ip_acl_check()s
 * dokumentation ovenfor). Fail-open (true) hvis IP'en ikke kan afgoeres.
 */
bool ip_acl_check_req(httpd_req_t *req, uint8_t service);

/**
 * Kaldes periodisk fra hovedloopet (main.cpp). Ruller automatisk en ventende,
 * ubekraeftet aendring tilbage hvis ACL_PENDING_CONFIRM_TIMEOUT_MS er
 * overskredet.
 */
void ip_acl_tick(void);

/* ============================================================================
 * REGEL-CRUD (kaldt af CLI + REST — se cli_parser.cpp/api_handlers.cpp)
 * Alle mutationer virker paa den BEKRAEFTEDE (g_persist_config) tilstand
 * medmindre andet er noteret; en operation der "gates" lander i stedet i
 * staging og kraever ip_acl_confirm(). Mens en aendring afventer, afvises
 * ALLE andre mutationer med ACL_ACTION_ERR_PENDING (undgaar at staging og
 * g_persist_config kan divergere uafhaengigt af hinanden).
 * ============================================================================ */

/** Slaar hele ACL-haandhaevelsen til/fra. Til (0→1) "gates" hvis mindst én
 *  aktiv regel med service HTTP/TELNET/ALL findes. Fra (1→0) anvendes altid
 *  straks (kan aldrig forårsage ny lockout). */
IpAclResult ip_acl_set_enabled(bool enabled, bool *out_now_pending);

/** Tilfoejer en ny regel til ENDEN af listen (index = acl_rule_count FOeR
 *  tilfoejelsen — brug ip_acl_rule_move() bagefter for at placere den et
 *  andet sted). "Gates" hvis service ∈ {HTTP,TELNET,ALL} OG enabled=true,
 *  UANSET action (se ip_acl.cpp for den fulde FEAT-401-begrundelse: en ny
 *  aktiv mgmt-regel kan altid aendre udfaldet for eksisterende trafik). */
IpAclResult ip_acl_rule_add(uint32_t network_addr, uint8_t prefix_len,
                             uint8_t service, uint8_t action, bool enabled,
                             bool *out_now_pending, int *out_index);

/** Til/fra-slaar en eksisterende regel uden at slette/flytte den. Aktivering
 *  (false→true) af en HTTP/TELNET/ALL-regel "gates" altid; deaktivering
 *  (true→false) gates KUN hvis regelens action er ALLOW (en DENY-regel kan
 *  trygt deaktiveres uden bekraeftelse — kan kun aabne adgang, aldrig
 *  stramme, se ip_acl.cpp). */
IpAclResult ip_acl_rule_set_enabled(int index, bool enabled, bool *out_now_pending);

/** FEAT-401: erstatter ALLE felter for en eksisterende regel paa sin
 *  nuvaerende plads (samme "kraev alle felter"-stil som rbac_set_user() —
 *  ingen delvis-opdatering-tvetydighed). Gates hvis den GAMLE tilstand var
 *  en aktiv ALLOW-regel for HTTP/TELNET/ALL (fjernelsen kan stramme) ELLER
 *  den NYE tilstand er en aktiv HTTP/TELNET/ALL-regel (tilfoejelsen kan
 *  stramme, uanset dens action). */
IpAclResult ip_acl_rule_edit(int index, uint32_t network_addr, uint8_t prefix_len,
                              uint8_t service, uint8_t action, bool enabled,
                              bool *out_now_pending);

/** FEAT-401: flytter en regel fra `from_index` til `to_index` (resten af
 *  listen forskydes tilsvarende — regel-raekkefoelge er nu semantisk
 *  betydningsfuld, foerste match vinder). Gates hvis den flyttede regel er
 *  aktiv med service ∈ {HTTP,TELNET,ALL} — BEVIDST uafhaengigt af retning
 *  eller action (se ip_acl.cpp for hvorfor en praecis retningsanalyse er
 *  fravalgt som for risikabel at stole 100% paa). */
IpAclResult ip_acl_rule_move(int from_index, int to_index, bool *out_now_pending);

/** Sletter en regel (shift-ned). FEAT-401: gates hvis regelen er en aktiv
 *  ALLOW-regel for HTTP/TELNET/ALL (kan afsloere en DENY laengere nede —
 *  stramning); en DENY-regel (eller en deaktiveret/SSE-only regel) kan
 *  derimod trygt slettes uden bekraeftelse, som i v1. */
IpAclResult ip_acl_rule_delete(int index, bool *out_now_pending);

/** Bekraefter en ventende aendring: staging → g_persist_config, gemmes til
 *  NVS med det samme (config_save_to_nvs()). Kraever at en session-token
 *  udstedt EFTER aendringen gik i pending blev brugt til at naa hertil —
 *  haandhaevet af kaldestedet (CHECK_AUTH_WRITE + revoke-alle sikrer dette
 *  strukturelt, se rbac_session_token_revoke_all()), ikke af denne funktion
 *  selv. */
IpAclResult ip_acl_confirm(void);

/* ============================================================================
 * FEAT-402: KLADDE-TILSTAND (draft mode)
 *
 * En kladde er en RAM-only arbejdskopi af regelsaettet, ALDRIG konsulteret af
 * ip_acl_check() — man kan tilfoeje/redigere/slette/flytte/til-fra-slaa
 * frit, i vilkaarlig raekkefoelge, uden nogensinde at risikere sig selv,
 * fordi intet nogensinde haandhaeves foer et eksplicit "apply". Loeser det
 * problem at et flertrins-regelsaet (fx "tilfoej permit, tilfoej deny, byt
 * deres raekkefoelge") ellers kraevede at HVERT mellemtrin var levende og
 * potentielt selv-udelukkende samtidig (FEAT-401s per-operation-gating).
 *
 * Kun ÉN kladde kan eksistere ad gangen, og kun naar INGEN aendring allerede
 * afventer bekraeftelse (og omvendt — se ACL_ACTION_ERR_DRAFT_ACTIVE paa de
 * direkte CRUD-funktioner ovenfor). ip_acl_draft_apply() er det eneste sted
 * en kladde kan paavirke haandhaevelsen: enten straks (hvis kun SSE-relevante
 * regler aendrede sig) eller via den EKSISTERENDE pending-confirm-mekanisme
 * (hvis mgmt-relevante regler — HTTP/TELNET/ALL — aendrede sig), praecis
 * samme lockout-recovery-net som FEAT-399/401, nu blot anvendt paa HELE
 * kladdens slutresultat som ét atomisk skridt.
 * ============================================================================ */

/** Starter en ny kladde: kopierer den BEKRAEFTEDE tilstand ind som
 *  udgangspunkt. Fejler med ACL_ACTION_ERR_DRAFT_ACTIVE hvis en kladde
 *  allerede er i gang, eller ACL_ACTION_ERR_PENDING hvis en aendring
 *  afventer bekraeftelse (afslut/bekraeft den foerst). */
IpAclResult ip_acl_draft_begin(void);

/** Kasserer kladden uden at roere noget haandhaevet. */
IpAclResult ip_acl_draft_discard(void);

/** Nedenstaaende kladde-CRUD-funktioner virker UDELUKKENDE paa
 *  acl_draft_rules[] — ingen gating, ingen haandhaevelse, ingen
 *  session-revoke. Samme validering (CIDR/service/action/index) som deres
 *  direkte modstykker ovenfor. Alle returnerer ACL_ACTION_ERR_NO_DRAFT hvis
 *  ingen kladde er aktiv. */
IpAclResult ip_acl_draft_set_enabled(bool enabled);
IpAclResult ip_acl_draft_rule_add(uint32_t network_addr, uint8_t prefix_len, uint8_t service,
                                   uint8_t action, bool enabled, int *out_index);
IpAclResult ip_acl_draft_rule_edit(int index, uint32_t network_addr, uint8_t prefix_len,
                                    uint8_t service, uint8_t action, bool enabled);
IpAclResult ip_acl_draft_rule_move(int from_index, int to_index);
IpAclResult ip_acl_draft_rule_delete(int index);

/** Anvender HELE kladden atomisk. Sammenligner kladdens og den bekraeftede
 *  tilstands "mgmt-relevante delmaengde" (regler med service != SSE, i
 *  bevaret indbyrdes raekkefoelge) — differerer den slet ikke (kun SSE-
 *  regler aendret, eller ingen aendring), anvendes kladden direkte og
 *  persisteres med det samme (out_now_pending=false). Differerer den,
 *  overgaar kladden i stedet til den eksisterende pending-confirm-mekanisme
 *  (haandhaeves straks, alle sessioner/telnet-forbindelser revokeres, 5 min.
 *  til ip_acl_confirm() eller automatisk rollback — out_now_pending=true).
 *  Kladden ryddes under begge udfald.
 *  @param caller_ip_or_0 Bruges KUN til en valgfri, informativ
 *         self_match_warning (ville kalderens egen IP gaa fra tilladt til
 *         blokeret for HTTP ved denne anvendelse?) — indgaar IKKE i selve
 *         gating-beslutningen. 0 for at springe over. */
IpAclResult ip_acl_draft_apply(uint32_t caller_ip_or_0, bool *out_now_pending,
                                bool *out_self_match_warning);

bool    ip_acl_draft_is_active(void);
bool    ip_acl_draft_get_enabled(void);
uint8_t ip_acl_draft_get_rule_count(void);
bool    ip_acl_draft_get_rule(uint8_t index, AclRule *out);

/* ============================================================================
 * STATUS/VISNING
 * ============================================================================ */

bool     ip_acl_is_pending(void);
uint32_t ip_acl_pending_remaining_ms(void);  // 0 hvis intet afventer

/** "Effektiv" tilstand — staging hvis pending, ellers g_persist_config.
 *  Bruges til GET /api/acl og `show acl`, saa brugeren ser PRAECIS hvad der
 *  reelt haandhaeves lige nu under en test-periode. */
bool    ip_acl_get_effective_enabled(void);
uint8_t ip_acl_get_effective_rule_count(void);

/** Kopierer regel `index` (effektiv tilstand) ind i *out. Caller-eget buffer
 *  (IKKE en statisk intern buffer) — ip_acl_check() koeres fra flere
 *  forskellige FreeRTOS-tasks (httpd worker, loop()-tasken, SSE-acceptor-
 *  tasken), og en statisk retur-buffer ville race ved samtidige kald (fx en
 *  CLI `show acl` og en REST GET /api/acl paa samme tid). */
bool ip_acl_get_effective_rule(uint8_t index, AclRule *out);

/* ============================================================================
 * CIDR/SERVICE PARSING OG FORMATERING
 * ============================================================================ */

/** Parser "a.b.c.d" eller "a.b.c.d/nn" (intet suffiks => /32). */
bool ip_acl_parse_cidr(const char *str, uint32_t *out_network_addr, uint8_t *out_prefix_len);

/** Formaterer til "a.b.c.d/nn". out skal vaere mindst 19 bytes. */
void ip_acl_format_cidr(uint32_t network_addr, uint8_t prefix_len, char *out, size_t out_len);

/** "http"/"telnet"/"sse"/"all" — NULL hvis ugyldig service-vaerdi. */
const char *ip_acl_service_name(uint8_t service);

/** "allow"/"deny" — NULL hvis ugyldig action-vaerdi. FEAT-401. */
const char *ip_acl_action_name(uint8_t action);

/** Parser "allow"/"permit"/"deny"/"block" (case-insensitive). FEAT-401. */
bool ip_acl_parse_action(const char *str, uint8_t *out_action);

/** Ren CIDR-containment-test (ingen enabled/service-filtrering) — bruges af
 *  REST-laget (FEAT-399-følgefejl) til at advare PROAKTIVT hvis en ny regel
 *  ville ramme kalderens EGEN aktuelle IP, FØR den anvendes. */
bool ip_acl_cidr_matches(uint32_t network_addr, uint8_t prefix_len, uint32_t ip_network_order);

/** Parser "http"/"telnet"/"sse"/"all" (case-insensitive). */
bool ip_acl_parse_service(const char *str, uint8_t *out_service);

#endif // IP_ACL_H
