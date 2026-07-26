#include <errno.h>
#include "hash_map.h"
#include "../steam3_session.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <steamkit/steamkit.h>
#include <steamkit/cdn/cdn_client.h>

struct sk_steam3_session {
    sk_steam_client_t* steam_client;
    sk_steam_apps_t* steam_apps;
    sk_steam_content_t* steam_content;
    sk_cdn_client_t* cdn_client;
    char* username;
    char* password;
    uint32_t app_id;
    uint32_t cell_id;
    bool connected;
    bool logged_on;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    hash_map_t* tokens;
    hash_map_t* keys;
};

sk_steam3_session_t* steam3_session_create(const char* username, const char* password, uint32_t app_id, uint32_t cell_id) {
    sk_steam3_session_t* session = (sk_steam3_session_t*)calloc(1, sizeof(sk_steam3_session_t));
    if (!session) return NULL;

    session->steam_client = sk_steam_client_create();
    if (!session->steam_client) {
        free(session);
        return NULL;
    }

    session->steam_apps = sk_steam_apps_create();
    session->steam_content = sk_steam_content_create();
    session->cdn_client = sk_cdn_client_create(session->steam_client);
    session->username = username ? strdup(username) : NULL;
    session->password = password ? strdup(password) : NULL;
    session->app_id = app_id;
    session->cell_id = cell_id;
    session->connected = false;
    session->logged_on = false;
    pthread_mutex_init(&session->mutex, NULL);
    pthread_cond_init(&session->cond, NULL);
    session->tokens = hash_map_create(16);
    session->keys = hash_map_create(16);

    return session;
}

void steam3_session_destroy(sk_steam3_session_t* session) {
    if (!session) return;
    if (session->cdn_client) sk_cdn_client_destroy(session->cdn_client);
    if (session->steam_content) sk_steam_content_destroy(session->steam_content);
    if (session->steam_apps) sk_steam_apps_destroy(session->steam_apps);
    if (session->steam_client) sk_steam_client_destroy(session->steam_client);
    pthread_mutex_destroy(&session->mutex);
    pthread_cond_destroy(&session->cond);
    free(session->username);
    free(session->password);
    hash_map_clear(session->tokens, free);
    hash_map_destroy(session->tokens, free);
    hash_map_clear(session->keys, free);
    hash_map_destroy(session->keys, free);
    free(session);
}

int steam3_session_connect(sk_steam3_session_t* session) {
    if (!session || !session->steam_client) return -1;
    sk_steam_client_connect(session->steam_client);
    session->connected = true;
    return 0;
}

int steam3_session_log_on(sk_steam3_session_t* session) {
    if (!session || !session->steam_client) return -1;
    sk_steam_client_log_on(session->steam_client, session->username, session->password);
    session->logged_on = true;
    return 0;
}

int steam3_session_wait_for_callback(sk_steam3_session_t* session, int timeout_ms) {
    if (!session || !session->steam_client) return -1;
    uint32_t callback_type = 0;
    uint64_t job_id = 0;
    void* data = sk_steam_client_get_next_callback(session->steam_client, &callback_type, &job_id, timeout_ms);
    if (!data) return 0;

    switch (callback_type) {
        case SK_CLIENT_CALLBACK_LOGGED_ON: {
            sk_logged_on_callback_t* cb = (sk_logged_on_callback_t*)data;
            if (cb) {
                printf("[steam3] Logged on, result=%u\n", cb->result);
            }
            break;
        }
        case SK_CLIENT_CALLBACK_PICS_PRODUCT_INFO: {
            sk_pics_product_info_callback_t* cb = (sk_pics_product_info_callback_t*)data;
            if (cb) {
                for (uint32_t i = 0; i < cb->num_app_info; ++i) {
                    printf("[steam3] AppInfo: appid=%u\n", cb->app_info_ids[i]);
                }
                for (uint32_t i = 0; i < cb->num_package_info; ++i) {
                    printf("[steam3] PackageInfo: packageid=%u\n", cb->package_info_ids[i]);
                }
            }
            break;
        }
        case SK_CLIENT_CALLBACK_DEPOT_KEY: {
            sk_depot_key_callback_t* cb = (sk_depot_key_callback_t*)data;
            if (cb && cb->depot_id > 0) {
                char key[64];
                snprintf(key, sizeof(key), "depot_key_%u", cb->depot_id);
                hash_map_set(session->keys, key, (void*)(uintptr_t)cb->depot_id);
            }
            break;
        }
        default:
            break;
    }

    sk_steam_client_free_callback_data(data);
    return 1;
}

int steam3_session_request_app_info(sk_steam3_session_t* session, uint32_t app_id) {
    if (!session || !session->steam_apps) return -1;
    sk_steam_apps_request_app_info(session->steam_apps, app_id);
    return 0;
}

int steam3_session_request_manifest(sk_steam3_session_t* session, uint32_t depot_id, uint32_t app_id, uint64_t manifest_id, const char* branch) {
    if (!session || !session->steam_content) return -1;
    sk_manifest_request_code_callback_t* cb = sk_steam_content_get_manifest_request_code(session->steam_content, depot_id, app_id, manifest_id, branch);
    if (cb) {
        printf("[steam3] Got manifest request code callback for depot=%u\n", depot_id);
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
