/**
 * @file expansion_api_client.h
 * @brief FEAT-409: PLC-side klient mod en "HypervisionPLC Extension Board"s
 * management-API (REST/JSON, port 8080, Bearer-token-auth).
 *
 * Dækker: board-registrering (CRUD i PersistConfig.expansion_boards[]) samt
 * udgående HTTP-kald mod boardets management-API (status/kanal-liste/
 * kanal-config/diagnostisk read-write). ALLE udgående kald er
 * brugerinitierede (ingen baggrunds-polling) — se SECURITY_INDEX.md #20.
 *
 * v1-scope: KUN management-API'et. Selve Modbus TCP-datatrafikken (port
 * 502/503 på boardet, høj-frekvent drift) er IKKE en del af dette lag —
 * det er en separat, større, fremtidig integration (kø/cache-genbrug fra
 * mb_async.cpp, ST Logic-builtins osv.), bevidst udenfor scope her.
 */

#ifndef EXPANSION_API_CLIENT_H
#define EXPANSION_API_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ============================================================================
 * BOARD CRUD — opererer direkte på g_persist_config.expansion_boards[]
 * ============================================================================ */

// Er `type` en af de kendte, understøttede board-typer (EXPANSION_BOARD_TYPE_*,
// constants.h)? Bruges af både add/edit og REST/CLI-laget til at afvise en
// ukendt/forkert stavet type FØR den ender i PersistConfig.
bool expansion_board_type_valid(const char *type);

// Iterér de kendte board-typer (til fx en REST/CLI-liste over gyldige typer).
// Returnerer false når index er ude over listens ende. ÉT sted (her) er
// autoritativt for typenavn+label — REST/CLI/web-UI bør altid slå op her
// fremfor at duplikere listen, så en fremtidig ny type kun skal tilføjes ét sted.
bool expansion_board_type_list(uint8_t index, const char **out_value, const char **out_label);

// Tilføjer et nyt board på et EKSPLICIT, brugervalgt nummer (1-8 — dette ER
// boardets "id" i resten af API'et, intet separat nummer-felt findes).
// Returnerer det anvendte index (number-1) ved succes, -1 hvis number er
// udenfor 1-8, navn/IP/type er ugyldigt, eller nummeret allerede er i brug.
int expansion_board_add(uint8_t number, const char *board_type, const char *name, const char *ip_str, const char *token);

// Redigerer et eksisterende board. token==NULL eller tom streng bevarer det
// eksisterende token uændret (så man kan omdøbe/flytte IP uden at skulle
// genindtaste tokenet). board_type==NULL eller tom streng bevarer den
// eksisterende type. Returnerer false hvis index er ugyldigt/ikke i brug.
bool expansion_board_edit(uint8_t index, const char *name, const char *ip_str, const char *token, const char *board_type);

// Fjerner et board (rydder slottet). Returnerer false hvis index er ugyldigt/ikke i brug.
bool expansion_board_remove(uint8_t index);

// Finder et boards index ud fra navn (case-insensitive). Returnerer -1 hvis ikke fundet.
int expansion_board_find_by_name(const char *name);

// Skriver boardets IP som streng ("a.b.c.d") til out. Returnerer false hvis
// index er ugyldigt/ikke i brug, eller out_size er for lille.
bool expansion_board_ip_str(uint8_t index, char *out, size_t out_size);

/* ============================================================================
 * ASYNC MANAGEMENT-API-KALD
 * ============================================================================
 * Samme haerdede moenster som src/ota_handler.cpp's github_check_worker():
 * ÉT globalt "in flight"-slot (kun ét kald ad gangen — passer til at dette
 * er brugerinitieret admin-/diagnose-arbejde, ikke samtidig produktionstrafik).
 * *_start()-funktionerne spawner en baggrundstask og returnerer STRAKS
 * (kalder ALDRIG HTTPClient/JSON-parsing direkte på den kaldende task's egen
 * stack — httpd's handler-stack er kun 8192 bytes, for lidt til at gøre det
 * sikkert der). REST-lag: poll bagefter via expansion_api_poll(). CLI-lag:
 * expansion_api_wait_result() blokerer synkront (sikkert fra CLI-tasken,
 * som har sin egen, større stack).
 */

typedef struct {
  bool    valid;              // false hvis intet kald er startet endnu
  bool    in_progress;        // true mens baggrundstasken kører
  bool    done;               // true når resultatet (nedenfor) er klar til at læses
  bool    transport_ok;       // true hvis HTTP-forbindelsen overhovedet lykkedes (uanset statuskode)
  int     http_status;        // HTTP-statuskode fra boardet, eller <0 ved transportfejl
  uint8_t board_index;        // hvilket board kaldet gjaldt
  char    kind[24];           // "status"/"channels"/"config"/"read"/"write" — til UI/CLI-visning
  char    response_json[1536];// raa JSON-body fra boardet, eller en synteseret fejlbesked
} ExpansionApiResult;

bool expansion_api_start_status(uint8_t board_index);
bool expansion_api_start_channels(uint8_t board_index);

bool expansion_api_start_config_push(uint8_t board_index, uint8_t channel,
                                      bool enabled, const char *mode, uint32_t baudrate,
                                      const char *parity, uint8_t stop_bits,
                                      uint16_t timeout_ms, uint16_t inter_frame_delay_ms);

// Diagnostisk læsning — function_code 1-4 (Read Coils/Discrete Inputs/Holding/Input Registers)
bool expansion_api_start_diag_read(uint8_t board_index, uint8_t channel,
                                    uint8_t function_code, uint8_t slave_id,
                                    uint16_t address, uint16_t quantity);

// Diagnostisk skrivning — function_code 5 (coil, value 0/1), 6 (ét register)
bool expansion_api_start_diag_write_single(uint8_t board_index, uint8_t channel,
                                            uint8_t function_code, uint8_t slave_id,
                                            uint16_t address, uint32_t value);

// Diagnostisk skrivning — function_code 16 (flere registre, maks 32 pr. boardets egen graense)
bool expansion_api_start_diag_write_multi(uint8_t board_index, uint8_t channel,
                                           uint8_t slave_id, uint16_t address,
                                           const uint16_t *values, uint8_t count);

// Kopierer det aktuelle resultat-snapshot til *out. Returnerer false hvis
// intet kald er startet endnu (out->valid vil da også være false).
bool expansion_api_poll(ExpansionApiResult *out);

// true hvis et kald allerede er i gang (bruges af REST-handlers til at give
// et praecist 409 fremfor et generisk 400, naar en *_start()-funktion afviser).
bool expansion_api_is_busy(void);

// Blokerer synkront indtil et startet kald er faerdigt (done==true) eller
// timeout_ms udloeber. Til CLI-brug (egen task-stack, ikke httpd). Returnerer
// false ved timeout — resultatet kan stadig kigges paa via expansion_api_poll()
// bagefter (in_progress vil da fortsat vaere true).
bool expansion_api_wait_result(uint32_t timeout_ms, ExpansionApiResult *out);

#endif // EXPANSION_API_CLIENT_H
