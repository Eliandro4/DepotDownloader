#include <errno.h>
#include "hash_map.h"
#include "../steam3_session.h"
#include "hash_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

struct sk_steam3_session {
    sk_steam_client_t *steam_client;
    sk_steam_user_t *steam_user;
    sk_steam_apps_t *steam_apps;
    sk_steam_content_t *steam_content;
    sk_steam_cloud_t *steam_cloud;
    sk_steam_unified_messages_t *steam_unified_messages;
    sk_steam_published_file_t *steam_published_file;

    steam3_logon_details_t logon_details;
    bool authenticated_user;
    bool b_aborted;
    bool b_connecting;
    bool b_did_disconnect;
    bool b_is_connection_recovery;
    int connection_backoff;
    uint64_t seq;

    pthread_mutex_t steam_lock;
    pthread_cond_t steam_cond;

    hash_map_t *app_tokens;
    hash_map_t *package_tokens;
    hash_map_t *depot_keys;
    hash_map_t *cdn_auth_tokens;
    hash_map_t *app_info;
    hash_map_t *package_info;
    hash_map_t *app_beta_passwords;

    sk_steam_id_t *steam_id;
    void *auth_session;
};

static sk_steam3_session_t *g_instance = NULL;

static void on_steamclient_connected(void *user_data) {
    (void)user_data;
    sk_steam3_session_t *session = g_instance;
    if (!session) return;
    printf(" Done!\n");
    session->b_connecting = false;
}

static void on_steamclient_disconnected(void *user_data, bool user_initiated) {
    (void)user_data;
    (void)user_initiated;
    sk_steam3_session_t *session = g_instance;
    if (!session) return;
    session->b_did_disconnect = true;
}

sk_steam3_session_t *steam3_session_create(const steam3_logon_details_t *details) {
    sk_steam3_session_t *session = (sk_steam3_session_t *)calloc(1, sizeof(sk_steam3_session_t));
    if (!session) return NULL;

    session->logon_details.username = details->username ? strdup(details->username) : NULL;
    session->logon_details.password = details->password ? strdup(details->password) : NULL;
    session->logon_details.remember_password = details->remember_password;
    session->logon_details.use_qr_code = details->use_qr_code;
    session->logon_details.skip_app_confirmation = details->skip_app_confirmation;
    session->logon_details.cell_id = details->cell_id;
    session->logon_details.login_id = details->login_id;

    session->authenticated_user = (details->username != NULL) || details->use_qr_code;

    pthread_mutex_init(&session->steam_lock, NULL);
    pthread_cond_init(&session->steam_cond, NULL);

    session->app_tokens = hash_map_create(64);
    session->package_tokens = hash_map_create(64);
    session->depot_keys = hash_map_create(64);
    session->cdn_auth_tokens = hash_map_create(64);
    session->app_info = hash_map_create(64);
    session->package_info = hash_map_create(64);
    session->app_beta_passwords = hash_map_create(64);

    session->steam_client = sk_steam_client_create();
    if (!session->steam_client) {
        steam3_session_destroy(session);
        return NULL;
    }

    session->steam_user = (sk_steam_user_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_USER);
    session->steam_apps = (sk_steam_apps_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_APPS);
    session->steam_content = (sk_steam_content_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_CONTENT);
    session->steam_cloud = (sk_steam_cloud_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_CLOUD);
    session->steam_unified_messages = (sk_steam_unified_messages_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_UNIFIED_MESSAGES);
    session->steam_published_file = (sk_steam_published_file_t *)sk_steam_client_get_handler(session->steam_client, SK_HANDLER_STEAM_PUBLISHED_FILE);

    g_instance = session;
    return session;
}

void steam3_session_destroy(sk_steam3_session_t *session) {
    if (!session) return;
    steam3_session_disconnect(session);

    pthread_mutex_destroy(&session->steam_lock);
    pthread_cond_destroy(&session->steam_cond);

    hash_map_destroy(session->app_tokens, free);
    hash_map_destroy(session->package_tokens, free);
    hash_map_destroy(session->depot_keys, free);
    hash_map_destroy(session->cdn_auth_tokens, free);
    hash_map_destroy(session->app_info, NULL);
    hash_map_destroy(session->package_info, NULL);
    hash_map_destroy(session->app_beta_passwords, free);

    sk_steam_client_destroy(session->steam_client);

    free(session->logon_details.username);
    free(session->logon_details.password);
    free(session);
    g_instance = NULL;
}

bool steam3_session_wait_for_credentials(sk_steam3_session_t *session) {
    if (!session) return false;
    if (session->b_aborted) return false;

    pthread_mutex_lock(&session->steam_lock);
    while (!session->b_aborted && !session->steam_id) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30;
        int rc = pthread_cond_timedwait(&session->steam_cond, &session->steam_lock, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&session->steam_lock);
            return false;
        }
    }
    bool logged_on = session->steam_id != NULL && !session->b_aborted;
    pthread_mutex_unlock(&session->steam_lock);
    return logged_on;
}

bool steam3_session_is_logged_on(sk_steam3_session_t *session) {
    return session && session->steam_id != NULL && !session->b_aborted;
}

void steam3_session_disconnect(sk_steam3_session_t *session) {
    if (!session) return;
    session->b_aborted = true;
    pthread_cond_signal(&session->steam_cond);
    if (session->steam_client) {
        sk_steam_client_disconnect(session->steam_client, true);
    }
}

int steam3_session_tick_callbacks(sk_steam3_session_t *session, int timeout_ms) {
    (void)session;
    (void)timeout_ms;
    return 0;
}

static sk_key_value_t *parse_kv_from_buffer(const uint8_t *buf, size_t len) {
    sk_key_value_t *kv = sk_key_value_create("root");
    sk_key_value_load_from_buffer(kv, buf, len, true);
    return kv;
}

static hash_map_t *app_info_from_pics_response(sk_pics_product_info_callback_t *cb) {
    hash_map_t *map = hash_map_create(32);
    if (!cb || !cb->app_info_ids || cb->num_app_info == 0) return map;
    for (uint32_t i = 0; i < cb->num_app_info; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "%u", cb->app_info_ids[i]);
        sk_key_value_t *kv = sk_key_value_create(key);
        if (cb->app_info_kv && cb->app_info_kv[i]) {
            size_t sv_len = 0;
            char *sv = sk_key_value_write_vdf(cb->app_info_kv[i], &sv_len);
            if (sv) {
                sk_key_value_load_from_buffer(kv, (const uint8_t *)sv, sv_len, true);
                free(sv);
            }
        }
        hash_map_set(map, key, kv);
    }
    return map;
}

static hash_map_t *package_info_from_pics_response(sk_pics_product_info_callback_t *cb) {
    hash_map_t *map = hash_map_create(32);
    if (!cb || !cb->package_info_ids || cb->num_package_info == 0) return map;
    for (uint32_t i = 0; i < cb->num_package_info; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "%u", cb->package_info_ids[i]);
        sk_key_value_t *kv = sk_key_value_create(key);
        if (cb->package_info_kv && cb->package_info_kv[i]) {
            size_t sv_len = 0;
            char *sv = sk_key_value_write_vdf(cb->package_info_kv[i], &sv_len);
            if (sv) {
                sk_key_value_load_from_buffer(kv, (const uint8_t *)sv, sv_len, true);
                free(sv);
            }
        }
        hash_map_set(map, key, kv);
    }
    return map;
}

static void steam3_session_request_app_info(sk_steam3_session_t *session, uint32_t app_id) {
    if (!session || !session->steam_apps) return;
    if (!session->app_info) return;
    if (hash_map_get(session->app_info, NULL) != NULL) return;

    sk_steam_apps_request_app_info(session->steam_apps, app_id);

    char key[32];
    snprintf(key, sizeof(key), "%u", app_id);
    hash_map_set(session->app_info, key, NULL);
}

static void steam3_session_request_package_info(sk_steam3_session_t *session, uint32_t package_id) {
    if (!session || !session->steam_apps) return;
    char key[32];
    snprintf(key, sizeof(key), "%u", package_id);
    if (hash_map_get(session->package_info, key) != NULL) return;

    sk_steam_apps_request_package_info(session->steam_apps, package_id);
}

uint64_t steam3_session_get_manifest_request_code(sk_steam3_session_t *session,
    uint32_t depot_id, uint32_t app_id, uint64_t manifest_id, const char *branch) {
    if (!session || !session->steam_content) return 0;

    sk_manifest_request_code_callback_t *cb = sk_steam_content_get_manifest_request_code(
        session->steam_content, depot_id, app_id, manifest_id, branch);
    if (!cb) return 0;
    uint64_t code = cb->request_code;
    sk_manifest_request_code_callback_destroy(cb);
    return code;
}

void steam3_session_request_cdn_auth_token(sk_steam3_session_t *session,
    uint32_t app_id, uint32_t depot_id, const char *host) {
    if (!session || !session->steam_content) return;
    sk_cdn_auth_token_callback_t *cb = sk_steam_content_get_cdn_auth_token(
        session->steam_content, app_id, depot_id, host);
    if (cb) {
        char key[256];
        snprintf(key, sizeof(key), "%u_%s", depot_id, host);
        hash_map_set(session->cdn_auth_tokens, key, cb);
    }
}

sk_cdn_auth_token_callback_t *steam3_session_get_cdn_auth_token(sk_steam3_session_t *session,
    uint32_t app_id, uint32_t depot_id, const char *host) {
    if (!session) return NULL;
    char key[256];
    snprintf(key, sizeof(key), "%u_%s", depot_id, host);
    return (sk_cdn_auth_token_callback_t *)hash_map_get(session->cdn_auth_tokens, key);
}

const char *steam3_session_get_steam_id(const sk_steam3_session_t *session) {
    if (!session || !session->steam_id) return NULL;
    return sk_steam_id_to_string(session->steam_id);
}

bool steam3_session_request_free_license(sk_steam3_session_t *session, uint32_t app_id) {
    if (!session || !session->steam_apps) return false;
    sk_free_license_callback_t *cb = sk_steam_apps_request_free_license(session->steam_apps, app_id);
    if (!cb) return false;
    bool granted = false;
    if (cb->result == 1 && cb->num_granted_apps > 0) {
        for (uint32_t i = 0; i < cb->num_granted_apps; ++i) {
            if (cb->granted_apps[i] == app_id) { granted = true; break; }
        }
    }
    sk_free_license_callback_destroy(cb);
    return granted;
}

bool steam3_session_request_app_ownership_ticket(sk_steam3_session_t *session, uint32_t app_id) {
    if (!session || !session->steam_apps) return false;
    sk_app_ownership_ticket_callback_t *cb = sk_steam_apps_get_app_ownership_ticket(session->steam_apps, app_id);
    if (!cb) return false;
    bool success = cb->result == 1;
    sk_app_ownership_ticket_callback_destroy(cb);
    return success;
}

bool steam3_session_check_app_beta_password(sk_steam3_session_t *session, uint32_t app_id, const char *password) {
    if (!session || !session->steam_apps) return false;
    (void)password;
    char key[32];
    snprintf(key, sizeof(key), "%u", app_id);
    hash_map_set(session->app_beta_passwords, key, (void *)1);
    return true;
}

bool steam3_session_request_depot_key(sk_steam3_session_t *session, uint32_t depot_id, uint32_t app_id) {
    if (!session || !session->steam_apps) return false;
    sk_depot_key_callback_t *cb = sk_steam_apps_get_depot_decryption_key(session->steam_apps, depot_id, app_id);
    if (!cb) return false;
    if (cb->result == 1) {
        char key[32];
        snprintf(key, sizeof(key), "%u", depot_id);
        uint8_t *key_copy = (uint8_t *)malloc(32);
        if (key_copy) {
            memcpy(key_copy, cb->depot_key, 32);
            hash_map_set(session->depot_keys, key, key_copy);
        }
    }
    bool success = cb->result == 1;
    sk_depot_key_callback_destroy(cb);
    return success;
}

static sk_key_value_t *steam3_session_get_app_info(sk_steam3_session_t *session, uint32_t app_id) {
    if (!session || !session->app_info) return NULL;
    char key[32];
    snprintf(key, sizeof(key), "%u", app_id);
    return (sk_key_value_t *)hash_map_get(session->app_info, key);
}

static sk_key_value_t *steam3_session_get_private_beta_section(sk_steam3_session_t *session, uint32_t app_id, const char *branch) {
    if (!session || !session->app_beta_passwords) return NULL;
    char key[32];
    snprintf(key, sizeof(key), "%u", app_id);
    if (!hash_map_get(session->app_beta_passwords, key)) return NULL;

    return NULL;
}

sk_published_file_details_t *steam3_session_get_published_file_details(sk_steam3_session_t *session, uint64_t published_file_id) {
    if (!session || !session->steam_published_file) return NULL;
    return sk_steam_published_file_get_details(session->steam_published_file, published_file_id);
}

sk_ugc_details_callback_t *steam3_session_get_ugc_details(sk_steam3_session_t *session, uint64_t ugc_id) {
    if (!session || !session->steam_cloud) return NULL;
    return sk_steam_cloud_request_ugc_details(session->steam_cloud, ugc_id);
}

bool steam3_session_request_pics_access_tokens(sk_steam3_session_t *session,
    const uint32_t *app_ids, uint32_t num_apps, const uint32_t *package_ids, uint32_t num_packages) {
    if (!session || !session->steam_apps) return false;
    sk_pics_access_token_callback_t *cb = sk_steam_apps_request_pics_access_tokens(
        session->steam_apps, app_ids, num_apps, package_ids, num_packages);
    if (!cb) return false;
    bool success = cb->num_app_tokens > 0 || cb->num_package_tokens > 0;
    sk_pics_access_token_callback_destroy(cb);
    return success;
}