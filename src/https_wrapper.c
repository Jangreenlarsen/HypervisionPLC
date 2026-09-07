/**
 * @file https_wrapper.c
 * @brief HTTPS/TLS server wrapper using ESP-IDF esp_https_server (FEAT-016)
 *
 * Uses the official esp_https_server component (httpd_ssl_start) which handles
 * TLS session lifecycle internally. This avoids heap corruption issues that
 * occur with custom open_fn/close_fn callbacks + esp_tls direct usage.
 *
 * IMPORTANT: This file MUST be compiled as C (not C++) because esp_https_server.h
 * includes esp_tls.h which pulls in lwIP headers through sys/socket.h. In
 * Arduino-ESP32 v2.x C++ builds, this causes a compilation error in ip4_addr.h
 * (u32_t not defined). By wrapping in a .c file, we avoid the C++ include chain.
 */

#include "https_wrapper.h"
#include <string.h>
#include <stdio.h>
#include <esp_https_server.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include "debug_flags.h"
#include "debug.h"

static const char *TAG = "HTTPS_WRAP";

/* Embedded TLS certificates (generated at build time via board_build.embed_txtfiles)
 * Symbol names include path prefix: certs/ -> certs_ */
extern const uint8_t servercert_pem_start[] asm("_binary_certs_servercert_pem_start");
extern const uint8_t servercert_pem_end[]   asm("_binary_certs_servercert_pem_end");
extern const uint8_t prvtkey_pem_start[]    asm("_binary_certs_prvtkey_pem_start");
extern const uint8_t prvtkey_pem_end[]      asm("_binary_certs_prvtkey_pem_end");

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

int https_wrapper_start(httpd_handle_t *handle,
                        uint16_t port,
                        uint16_t max_uri,
                        uint32_t stack_size,
                        uint8_t priority,
                        int core_id)
{
  if (!handle) {
    ESP_LOGE(TAG, "handle is NULL");
    return -1;
  }

  ESP_LOGI(TAG, "Free heap: %u bytes (largest block: %u)",
           esp_get_free_heap_size(),
           heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  /* Use the official ESP-IDF HTTPS server component.
   * It handles TLS session create/delete internally via its own
   * open_fn/close_fn callbacks - no manual esp_tls management needed. */
  httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();

  /* Server certificate and private key (embedded at build time).
   * NOTE: In ESP-IDF 4.x, the server cert field is confusingly named "cacert_pem"
   * (fixed in ESP-IDF 5.0 to "servercert"). This IS the server certificate. */
  ssl_config.cacert_pem  = servercert_pem_start;
  ssl_config.cacert_len  = (size_t)(servercert_pem_end - servercert_pem_start);
  ssl_config.prvtkey_pem = prvtkey_pem_start;
  ssl_config.prvtkey_len = (size_t)(prvtkey_pem_end - prvtkey_pem_start);

  /* httpd configuration (accessible via .httpd member) */
  ssl_config.httpd.server_port      = port;
  ssl_config.httpd.max_uri_handlers = max_uri;
  ssl_config.httpd.stack_size       = stack_size;
  // BUG-369d: sat ned fra 3 til 2 — 132KB fri intern heap lige efter boot
  // (bekraeftet stabil over 1+ minut UDEN nogen web-klient overhovedet) delt
  // paa 3 samtidige TLS-sessioner giver kun ~44KB til hver, meget taet paa
  // mbedTLS's egen ~32-40KB minimumsbehov pr. session (se BUG-369).
  // Rammes graensen, evicter esp_https_server (lru_purge_enable) den
  // aeldste session for at give plads til en ny — men ser ikke ud til at
  // give den nye session nok "frisk" heap i praksis (bekraeftet: gentagne
  // "mbedtls_ssl_setup returned -0x7F00"-fejl naar flere browser-faner/-
  // requests presser paa). 2 samtidige giver hver session markant mere
  // luft (~66KB), paa bekostning af at en 3. samtidig klient nu maa vente
  // paa at en af de to eksisterende lukker foerst.
  ssl_config.httpd.max_open_sockets = 2;
  ssl_config.httpd.backlog_conn     = 2;
  ssl_config.httpd.uri_match_fn     = httpd_uri_match_wildcard;
  ssl_config.httpd.lru_purge_enable = true;
  ssl_config.httpd.recv_wait_timeout  = 15;
  ssl_config.httpd.send_wait_timeout  = 10;
  ssl_config.httpd.core_id          = core_id;
  ssl_config.httpd.task_priority    = priority;

  /* Force HTTPS transport mode */
  ssl_config.transport_mode = HTTPD_SSL_TRANSPORT_SECURE;

  /* Port for HTTPS (overrides the default 443) */
  ssl_config.port_secure = port;

  esp_err_t err = httpd_ssl_start(handle, &ssl_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "httpd_ssl_start failed: %d (0x%x)", err, err);
    return -1;
  }

  ESP_LOGI(TAG, "HTTPS server on port %d (esp_https_server)", port);
  return 0;
}

void https_wrapper_stop(httpd_handle_t handle)
{
  if (handle) {
    httpd_ssl_stop(handle);
    ESP_LOGI(TAG, "HTTPS server stopped");
  }
}

int https_wrapper_get_cert_info(char *buf, size_t buflen)
{
  if (!buf || buflen == 0) return -1;

  mbedtls_x509_crt crt;
  mbedtls_x509_crt_init(&crt);

  size_t cert_len = (size_t)(servercert_pem_end - servercert_pem_start);
  int ret = mbedtls_x509_crt_parse(&crt, servercert_pem_start, cert_len);
  if (ret != 0) {
    mbedtls_x509_crt_free(&crt);
    snprintf(buf, buflen, "parse error (%d)", ret);
    return -1;
  }

  mbedtls_pk_type_t pk_type = mbedtls_pk_get_type(&crt.pk);
  size_t key_bits = mbedtls_pk_get_bitlen(&crt.pk);

  if (pk_type == MBEDTLS_PK_ECKEY || pk_type == MBEDTLS_PK_ECDSA) {
    const char *curve = "unknown";
    mbedtls_ecp_keypair *ec = mbedtls_pk_ec(crt.pk);
    if (ec) {
      mbedtls_ecp_group_id gid = ec->grp.id;
      if (gid == MBEDTLS_ECP_DP_SECP256R1)      curve = "P-256";
      else if (gid == MBEDTLS_ECP_DP_SECP384R1)  curve = "P-384";
      else if (gid == MBEDTLS_ECP_DP_SECP521R1)  curve = "P-521";
    }
    snprintf(buf, buflen, "ECDSA %s (%u-bit)", curve, (unsigned)key_bits);
  } else if (pk_type == MBEDTLS_PK_RSA) {
    snprintf(buf, buflen, "RSA (%u-bit)", (unsigned)key_bits);
  } else {
    snprintf(buf, buflen, "%s (%u-bit)", mbedtls_pk_get_name(&crt.pk), (unsigned)key_bits);
  }

  mbedtls_x509_crt_free(&crt);
  return 0;
}
