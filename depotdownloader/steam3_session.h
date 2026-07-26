#ifndef DEPOT_DOWNLOADER_STEAM3_SESSION_H
#define DEPOT_DOWNLOADER_STEAM3_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "steamkit/steamkit.h"

typedef struct sk_steam3_session sk_steam3_session_t;

typedef struct {
    char *username;
    char *password;
    bool remember_password;
    bool use_qr_code;
    bool skip_app_confirmation;
    int cell_id;
    uint32_t login_id;
} steam3_logon_details_t;

sk_steam3_session_t *steam3_session_create(const steam3_logon_details_t *details);
void steam3_session_destroy(sk_steam3_session_t *session);

bool steam3_session_wait_for_credentials(sk_steam3_session_t *session);
bool steam3_session_is_logged_on(sk_steam3_session_t *session);
void steam3_session_disconnect(sk_steam3_session_t *session);
int steam3_session_tick_callbacks(sk_steam3_session_t *session, int timeout_ms);

uint64_t steam3_session_get_manifest_request_code(sk_steam3_session_t *session,
    uint32_t depot_id, uint32_t app_id, uint64_t manifest_id, const char *branch);
void steam3_session_request_cdn_auth_token(sk_steam3_session_t *session,
    uint32_t app_id, uint32_t depot_id, const char *host);
sk_cdn_auth_token_callback_t *steam3_session_get_cdn_auth_token(sk_steam3_session_t *session,
    uint32_t app_id, uint32_t depot_id, const char *host);

const char *steam3_session_get_steam_id(const sk_steam3_session_t *session);

#ifdef __cplusplus
}
#endif

#endif