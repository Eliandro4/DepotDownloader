#ifndef DEPOT_DOWNLOADER_STEAM3_SESSION_H
#define DEPOT_DOWNLOADER_STEAM3_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <steamkit/types/key_value.h>
#include <steamkit/steam/handlers/steam_published_file.h>
#include <steamkit/steam/handlers/steam_cloud.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEPOT_DOWNLOADER_INVALID_APP_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_DEPOT_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_MANIFEST_ID UINT64_MAX

typedef struct sk_steam3_session sk_steam3_session_t;

sk_steam3_session_t* steam3_session_create(const char* username, const char* password, uint32_t app_id, uint32_t cell_id);
void steam3_session_destroy(sk_steam3_session_t* session);
void steam3_session_set_access_token(sk_steam3_session_t* session, const char* access_token);
void steam3_session_set_skip_mobile_confirmation(sk_steam3_session_t* session, bool skip);
int steam3_session_authenticate_via_qr(sk_steam3_session_t* session, const char* username, const char* password, bool remember_password);

int steam3_session_connect(sk_steam3_session_t* session);
int steam3_session_log_on(sk_steam3_session_t* session);
int steam3_session_wait_for_callback(sk_steam3_session_t* session, int timeout_ms);
int steam3_session_request_app_info(sk_steam3_session_t* session, uint32_t app_id);
int steam3_session_request_manifest(sk_steam3_session_t* session, uint32_t depot_id, uint32_t app_id, uint64_t manifest_id, const char* branch);
uint64_t steam3_session_get_manifest_request_code(const sk_steam3_session_t* session);

void* steam3_session_download_manifest(sk_steam3_session_t* session, uint32_t depot_id, uint64_t manifest_id, uint64_t request_code, const uint8_t* depot_key, size_t key_len);
int steam3_session_download_depot_chunk(sk_steam3_session_t* session, uint32_t depot_id, const void* chunk, const uint8_t* depot_key, size_t key_len, uint8_t* buffer, size_t buffer_size);

int steam3_session_get_depot_key(sk_steam3_session_t* session, uint32_t depot_id, uint32_t app_id, uint8_t* out_key, size_t key_len);
const char* steam3_session_request_cdn_auth_token(sk_steam3_session_t* session, uint32_t app_id, uint32_t depot_id, const char* host);

sk_key_value_t* steam3_session_get_pics_app_info(sk_steam3_session_t* session, uint32_t app_id, int timeout_ms);
sk_steam_published_file_t* steam3_session_get_published_file(sk_steam3_session_t* session);
sk_steam_cloud_t* steam3_session_get_cloud(sk_steam3_session_t* session);

#ifdef __cplusplus
}
#endif

#endif
