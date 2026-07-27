#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "hash_map.h"
#include "../steam3_session.h"
#include <steamkit/steamkit.h>
#include <steamkit/steam/handlers/steam_user.h>
#include <steamkit/steam/handlers/steam_published_file.h>
#include <steamkit/steam/handlers/steam_cloud.h>
#include <steamkit/cdn/cdn_client.h>
#include <steamkit/steam/callbacks.h>
#include <steamkit/types/key_value.h>
#include <steamkit/steam/authentication/steam_authentication.h>
#include <steamkit/steam/handlers/client_msg_handler.h>
#include <steamkit/steam/cm_client.h>

struct sk_steam3_session {
    sk_steam_client_t* steam_client;
    sk_steam_user_t* steam_user;
    sk_steam_apps_t* steam_apps;
    sk_steam_content_t* steam_content;
    sk_cdn_client_t* cdn_client;
    sk_steam_published_file_t* steam_published_file;
    sk_steam_cloud_t* steam_cloud;
    char* username;
    char* password;
    char* access_token;
    uint32_t app_id;
    uint32_t cell_id;
    bool connected;
    bool logged_on;
    uint64_t last_manifest_request_code;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    hash_map_t* tokens;
    hash_map_t* keys;
    hash_map_t* cdn_auth_tokens;
};

static void steam3_session_setup_user_handler(sk_steam3_session_t* session) {
    if (!session || !session->steam_client) return;
    session->steam_user = sk_steam_user_create();
    if (session->steam_user) {
        sk_client_msg_handler_setup((struct sk_client_msg_handler*)session->steam_user, session->steam_client);
        sk_steam_client_add_handler(session->steam_client, (struct sk_client_msg_handler*)session->steam_user);
    }
}

sk_steam3_session_t* steam3_session_create(const char* username, const char* password, uint32_t app_id, uint32_t cell_id) {
    sk_steam3_session_t* session = (sk_steam3_session_t*)calloc(1, sizeof(sk_steam3_session_t));
    if (!session) return NULL;

    session->steam_client = sk_steam_client_create();
    if (!session->steam_client) {
        free(session);
        return NULL;
    }

    steam3_session_setup_user_handler(session);

    session->steam_apps = sk_steam_apps_create();
    if (session->steam_apps) {
        sk_client_msg_handler_setup((struct sk_client_msg_handler*)session->steam_apps, session->steam_client);
        sk_steam_client_add_handler(session->steam_client, (struct sk_client_msg_handler*)session->steam_apps);
    }

    session->steam_content = sk_steam_content_create();
    if (session->steam_content) {
        sk_client_msg_handler_setup((struct sk_client_msg_handler*)session->steam_content, session->steam_client);
        sk_steam_client_add_handler(session->steam_client, (struct sk_client_msg_handler*)session->steam_content);
    }

    session->cdn_client = sk_cdn_client_create(session->steam_client);

    session->steam_published_file = sk_steam_published_file_create();
    if (session->steam_published_file) {
        sk_client_msg_handler_setup((struct sk_client_msg_handler*)session->steam_published_file, session->steam_client);
        sk_steam_client_add_handler(session->steam_client, (struct sk_client_msg_handler*)session->steam_published_file);
    }

    session->steam_cloud = sk_steam_cloud_create();
    if (session->steam_cloud) {
        sk_client_msg_handler_setup((struct sk_client_msg_handler*)session->steam_cloud, session->steam_client);
        sk_steam_client_add_handler(session->steam_client, (struct sk_client_msg_handler*)session->steam_cloud);
    }
    session->username = username ? strdup(username) : NULL;
    session->password = password ? strdup(password) : NULL;
    session->access_token = NULL;
    printf("[steam3] Session created for username: %s\n", session->username ? session->username : "(null)");
    session->app_id = app_id;
    session->cell_id = cell_id;
    session->connected = false;
    session->logged_on = false;
    session->last_manifest_request_code = 0;
    pthread_mutex_init(&session->mutex, NULL);
    pthread_cond_init(&session->cond, NULL);
    session->tokens = hash_map_create(16);
    session->keys = hash_map_create(16);
    session->cdn_auth_tokens = hash_map_create(16);

    return session;
}

void steam3_session_destroy(sk_steam3_session_t* session) {
    if (!session) return;
    if (session->cdn_client) sk_cdn_client_destroy(session->cdn_client);
    if (session->steam_content) sk_steam_content_destroy(session->steam_content);
    if (session->steam_apps) sk_steam_apps_destroy(session->steam_apps);
    if (session->steam_user) sk_steam_user_destroy(session->steam_user);
    if (session->steam_published_file) sk_steam_published_file_destroy(session->steam_published_file);
    if (session->steam_cloud) sk_steam_cloud_destroy(session->steam_cloud);
    if (session->steam_client) {
        sk_steam_client_disconnect(session->steam_client, true);
        sk_steam_client_destroy(session->steam_client);
    }
    pthread_mutex_destroy(&session->mutex);
    pthread_cond_destroy(&session->cond);
    free(session->username);
    free(session->password);
    free(session->access_token);
    hash_map_clear(session->tokens, free);
    hash_map_destroy(session->tokens, free);
    hash_map_clear(session->keys, free);
    hash_map_destroy(session->keys, free);
    hash_map_clear(session->cdn_auth_tokens, free);
    hash_map_destroy(session->cdn_auth_tokens, free);
    free(session);
}

void steam3_session_set_access_token(sk_steam3_session_t* session, const char* access_token) {
    if (!session) return;
    free(session->access_token);
    session->access_token = access_token ? strdup(access_token) : NULL;
}

int steam3_session_authenticate_via_qr(sk_steam3_session_t* session, const char* username, const char* password, bool remember_password) {
    if (!session || !session->steam_client) return -1;

    sk_steam_authentication_t* auth = sk_steam_authentication_create(session->steam_client);
    if (!auth) return -1;

    sk_auth_session_details_t* details = sk_auth_session_details_create(username, password);
    if (!details) {
        sk_steam_authentication_destroy(auth);
        return -1;
    }

    details->is_remember_password = remember_password;

    sk_qr_auth_session_t* qr = sk_auth_begin_session_via_qr(auth, details);
    if (!qr) {
        sk_auth_session_details_destroy(details);
        sk_steam_authentication_destroy(auth);
        return -1;
    }

    const char* challenge_url = sk_qr_auth_session_challenge_url(qr);
    if (challenge_url) {
        printf("[steam3] QR Login URL: %s\n", challenge_url);
    }

    sk_auth_poll_result_t* result = sk_qr_auth_session_poll_wait_for_result(qr);
    if (!result || !result->access_token) {
        printf("[steam3] QR authentication failed or timed out\n");
        sk_qr_auth_session_destroy(qr);
        sk_auth_session_details_destroy(details);
        sk_steam_authentication_destroy(auth);
        return -1;
    }

    printf("[steam3] QR authentication successful\n");
    steam3_session_set_access_token(session, result->access_token);

    if (remember_password && result->refresh_token) {
        char key[256];
        snprintf(key, sizeof(key), "qr_token:%s", session->username ? session->username : "unknown");
        hash_map_set(session->tokens, key, strdup(result->refresh_token));
    }

    sk_auth_poll_result_destroy(result);
    sk_qr_auth_session_destroy(qr);
    sk_auth_session_details_destroy(details);
    sk_steam_authentication_destroy(auth);
    return 0;
}

int steam3_session_connect(sk_steam3_session_t* session) {
    if (!session || !session->steam_client) return -1;
    sk_steam_client_set_cell_id(session->steam_client, session->cell_id);
    sk_steam_client_connect(session->steam_client);
    session->connected = true;
    return 0;
}

int steam3_session_log_on(sk_steam3_session_t* session) {
    if (!session || !session->steam_client || !session->steam_user) return -1;

    sk_log_on_details_t* details = sk_log_on_details_create();
    if (!details) return -1;

    details->username = session->username ? strdup(session->username) : NULL;
    details->password = session->password ? strdup(session->password) : NULL;
    details->access_token = session->access_token ? strdup(session->access_token) : NULL;
    details->cell_id = session->cell_id;
    details->login_id = 0x534B32;
    details->should_remember_password = true;
    details->account_instance = 0;
    details->machine_name = NULL;
    details->auth_code = NULL;
    details->two_factor_code = NULL;

    sk_steam_user_log_on(session->steam_user, details);

    printf("[steam3] Logon attempt for user: %s\n", details->username ? details->username : "(null)");

    sk_log_on_details_destroy(details);

    uint32_t callback_type = 0;
    uint64_t job_id = 0;
    bool logged_on = false;
    int result = 0;

    for (int retry = 0; retry < 30; ++retry) {
        void* data = sk_steam_client_get_next_callback(session->steam_client, &callback_type, &job_id, 5000);
        if (!data) continue;

        if (callback_type == SK_CLIENT_CALLBACK_LOGGED_ON) {
            sk_logged_on_callback_t* cb = (sk_logged_on_callback_t*)data;
            result = cb->result;
            logged_on = (result == 1);
            sk_steam_client_free_callback_data(data);
            break;
        }

        if (callback_type == SK_CLIENT_CALLBACK_LOGGED_OFF) {
            sk_logged_off_callback_t* cb = (sk_logged_off_callback_t*)data;
            result = cb->result;
            sk_steam_client_free_callback_data(data);
            break;
        }

        if (callback_type == SK_CLIENT_CALLBACK_ACCOUNT_INFO) {
            sk_account_info_callback_t* cb = (sk_account_info_callback_t*)data;
            if (cb) {
                printf("[steam3] AccountInfo: persona=%s country=%s\n",
                       cb->persona_name ? cb->persona_name : "(null)",
                       cb->country ? cb->country : "(null)");
            }
            sk_steam_client_free_callback_data(data);
            continue;
        }

        sk_steam_client_free_callback_data(data);
    }

    session->logged_on = logged_on;
    printf("[steam3] Logon result: %d, logged_on=%d\n", result, logged_on);
    return logged_on ? 0 : -1;
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
                printf("[steam3] Logged on, result=%d\n", cb->result);
                session->logged_on = (cb->result == 1);
            }
            break;
        }
        case SK_CLIENT_CALLBACK_LOGGED_OFF: {
            sk_logged_off_callback_t* cb = (sk_logged_off_callback_t*)data;
            if (cb) {
                printf("[steam3] Logged off, result=%d\n", cb->result);
                session->logged_on = false;
            }
            break;
        }
        case SK_CLIENT_CALLBACK_CONNECTED: {
            printf("[steam3] Connection established\n");
            session->connected = true;
            break;
        }
        case SK_CLIENT_CALLBACK_DISCONNECTED: {
            sk_disconnected_callback_t* cb = (sk_disconnected_callback_t*)data;
            printf("[steam3] Disconnected, user_initiated=%d\n", cb ? cb->user_initiated : 0);
            session->connected = false;
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
                uint8_t* key_copy = (uint8_t*)malloc(32);
                if (key_copy) {
                    memcpy(key_copy, cb->depot_key, 32);
                    hash_map_set(session->keys, key, key_copy);
                }
                printf("[steam3] Received depot key for depot=%u\n", cb->depot_id);
            }
            break;
        }
        case SK_CLIENT_CALLBACK_LICENSE_LIST: {
            sk_license_list_callback_t* cb = (sk_license_list_callback_t*)data;
            if (cb) {
                printf("[steam3] LicenseList: result=%u, packages=%u\n", cb->result, cb->num_packages);
            }
            break;
        }
        case SK_CLIENT_CALLBACK_FREE_LICENSE: {
            sk_free_license_callback_t* cb = (sk_free_license_callback_t*)data;
            if (cb) {
                printf("[steam3] FreeLicense: result=%u\n", cb->result);
            }
            break;
        }
        case SK_CLIENT_CALLBACK_ACCOUNT_INFO: {
            sk_account_info_callback_t* cb = (sk_account_info_callback_t*)data;
            if (cb) {
                printf("[steam3] AccountInfo: persona=%s country=%s\n",
                       cb->persona_name ? cb->persona_name : "(null)",
                       cb->country ? cb->country : "(null)");
            }
            break;
        }
        default:
            printf("[steam3] Unhandled callback type=%u\n", callback_type);
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

    sk_manifest_request_code_callback_t* cb = sk_steam_content_get_manifest_request_code(
        session->steam_content, depot_id, app_id, manifest_id, branch);

    if (cb) {
        if (cb->request_code != 0) {
            session->last_manifest_request_code = cb->request_code;
            printf("[steam3] Got manifest request code=%llu for depot=%u\n",
                   (unsigned long long)cb->request_code, depot_id);
        } else {
            printf("[steam3] Manifest request code callback returned result=%u\n", cb->result);
        }
        sk_manifest_request_code_callback_destroy(cb);
    }

    return (session->last_manifest_request_code != 0) ? 0 : -1;
}

void* steam3_session_download_manifest(sk_steam3_session_t* session, uint32_t depot_id, uint64_t manifest_id, uint64_t request_code, const uint8_t* depot_key, size_t key_len) {
    (void)depot_key;
    (void)key_len;
    if (!session || !session->steam_content || !session->cdn_client) return NULL;

    sk_cdn_server_list_callback_t* server_cb = sk_steam_content_get_servers_for_steam_pipe(session->steam_content);
    if (!server_cb || server_cb->num_servers == 0) {
        printf("[steam3] Failed to get CDN server list\n");
        if (server_cb) sk_cdn_server_list_callback_destroy(server_cb);
        return NULL;
    }

    sk_cdn_client_pool_t* pool = sk_cdn_client_pool_create();
    if (!pool) {
        sk_cdn_server_list_callback_destroy(server_cb);
        return NULL;
    }

    for (uint32_t i = 0; i < server_cb->num_servers; ++i) {
        sk_cdn_client_pool_add_server(pool, server_cb->servers[i]);
    }
    sk_cdn_server_list_callback_destroy(server_cb);

    const sk_cdn_server_t* server = sk_cdn_client_pool_select_server(pool, true);
    if (!server) {
        printf("[steam3] No CDN server available\n");
        sk_cdn_client_pool_destroy(pool);
        return NULL;
    }

    sk_cdn_auth_token_callback_t* auth_cb = sk_steam_content_get_cdn_auth_token(
        session->steam_content, session->app_id, depot_id, server->host);

    const char* cdn_token = auth_cb ? auth_cb->token : NULL;
    if (!cdn_token) {
        printf("[steam3] Failed to get CDN auth token for depot=%u host=%s\n", depot_id, server->host);
    }

    sk_depot_manifest_t* manifest = sk_cdn_client_download_manifest(
        session->cdn_client, depot_id, manifest_id, request_code,
        (sk_cdn_server_t*)server, depot_key, key_len, cdn_token);

    if (manifest) {
        printf("[steam3] Downloaded manifest for depot=%u with %u files\n", depot_id, manifest->num_files);
    } else {
        printf("[steam3] Failed to download manifest for depot=%u\n", depot_id);
    }

    if (auth_cb) sk_cdn_auth_token_callback_destroy(auth_cb);
    sk_cdn_client_pool_destroy(pool);
    return manifest;
}

int steam3_session_download_depot_chunk(sk_steam3_session_t* session, uint32_t depot_id, const void* chunk, const uint8_t* depot_key, size_t key_len, uint8_t* buffer, size_t buffer_size) {
    if (!session || !session->steam_content || !session->cdn_client || !chunk || !depot_key || key_len != 32 || !buffer) return -1;

    const sk_depot_chunk_t* dep_chunk = (const sk_depot_chunk_t*)chunk;

    sk_cdn_server_list_callback_t* server_cb = sk_steam_content_get_servers_for_steam_pipe(session->steam_content);
    if (!server_cb || server_cb->num_servers == 0) {
        if (server_cb) sk_cdn_server_list_callback_destroy(server_cb);
        return -1;
    }

    sk_cdn_client_pool_t* pool = sk_cdn_client_pool_create();
    if (!pool) {
        sk_cdn_server_list_callback_destroy(server_cb);
        return -1;
    }

    for (uint32_t i = 0; i < server_cb->num_servers; ++i) {
        sk_cdn_client_pool_add_server(pool, server_cb->servers[i]);
    }
    sk_cdn_server_list_callback_destroy(server_cb);

    const sk_cdn_server_t* server = sk_cdn_client_pool_select_server(pool, true);
    if (!server) {
        sk_cdn_client_pool_destroy(pool);
        return -1;
    }

    sk_cdn_auth_token_callback_t* auth_cb = sk_steam_content_get_cdn_auth_token(
        session->steam_content, session->app_id, depot_id, server->host);

    const char* cdn_token = auth_cb ? auth_cb->token : NULL;

    int written = sk_cdn_client_download_depot_chunk(
        session->cdn_client, depot_id, dep_chunk,
        (sk_cdn_server_t*)server, buffer, buffer_size,
        depot_key, key_len, cdn_token);

    if (auth_cb) sk_cdn_auth_token_callback_destroy(auth_cb);
    sk_cdn_client_pool_destroy(pool);

    return written;
}

int steam3_session_get_depot_key(sk_steam3_session_t* session, uint32_t depot_id, uint32_t app_id, uint8_t* out_key, size_t key_len) {
    if (!session || !session->steam_apps || !out_key || key_len != 32) return -1;

    char key_name[64];
    snprintf(key_name, sizeof(key_name), "depot_key_%u", depot_id);

    void* stored = hash_map_get(session->keys, key_name);
    if (stored) {
        memcpy(out_key, stored, 32);
        return 0;
    }

    sk_depot_key_callback_t* cb = sk_steam_apps_get_depot_decryption_key(session->steam_apps, depot_id, app_id);
    if (!cb) {
        printf("[steam3] Failed to request depot key for depot=%u\n", depot_id);
        return -1;
    }

    if (cb->result != 0) {
        printf("[steam3] Depot key request failed for depot=%u, result=%u\n", depot_id, cb->result);
        sk_depot_key_callback_destroy(cb);
        return -1;
    }

    memcpy(out_key, cb->depot_key, 32);
    uint8_t* key_copy = (uint8_t*)malloc(32);
    if (key_copy) {
        memcpy(key_copy, cb->depot_key, 32);
        hash_map_set(session->keys, key_name, key_copy);
    }
    sk_depot_key_callback_destroy(cb);

    printf("[steam3] Got depot key for depot=%u\n", depot_id);
    return 0;
}

const char* steam3_session_request_cdn_auth_token(sk_steam3_session_t* session, uint32_t app_id, uint32_t depot_id, const char* host) {
    if (!session || !session->steam_content || !host) return NULL;

    char cache_key[256];
    snprintf(cache_key, sizeof(cache_key), "%u:%s", depot_id, host);

    const char* cached = (const char*)hash_map_get(session->cdn_auth_tokens, cache_key);
    if (cached) return cached;

    sk_cdn_auth_token_callback_t* cb = sk_steam_content_get_cdn_auth_token(
        session->steam_content, app_id, depot_id, host);

    if (!cb || !cb->token) {
        if (cb) sk_cdn_auth_token_callback_destroy(cb);
        printf("[steam3] Failed to get CDN auth token for depot=%u host=%s\n", depot_id, host);
        return NULL;
    }

    const char* token_copy = strdup(cb->token);
    hash_map_set(session->cdn_auth_tokens, cache_key, (void*)token_copy);
    sk_cdn_auth_token_callback_destroy(cb);

    printf("[steam3] Got CDN auth token for depot=%u host=%s\n", depot_id, host);
    return token_copy;
}

uint64_t steam3_session_get_manifest_request_code(const sk_steam3_session_t* session) {
    if (!session) return 0;
    return session->last_manifest_request_code;
}

sk_steam_published_file_t* steam3_session_get_published_file(sk_steam3_session_t* session) {
    if (!session) return NULL;
    return session->steam_published_file;
}

sk_steam_cloud_t* steam3_session_get_cloud(sk_steam3_session_t* session) {
    if (!session) return NULL;
    return session->steam_cloud;
}

sk_key_value_t* steam3_session_get_pics_app_info(sk_steam3_session_t* session, uint32_t app_id, int timeout_ms) {
    if (!session || !session->steam_client) return NULL;

    int iterations = (timeout_ms + 4999) / 5000;
    if (iterations < 1) iterations = 1;

    for (int retry = 0; retry < iterations; ++retry) {
        uint32_t callback_type = 0;
        uint64_t job_id = 0;
        void* data = sk_steam_client_get_next_callback(session->steam_client, &callback_type, &job_id, 5000);
        if (!data) continue;

        if (callback_type == SK_CLIENT_CALLBACK_PICS_PRODUCT_INFO) {
            sk_pics_product_info_callback_t* cb = (sk_pics_product_info_callback_t*)data;
            for (uint32_t i = 0; i < cb->num_app_info; ++i) {
                if (cb->app_info_ids[i] == app_id && cb->app_info_kv && cb->app_info_kv[i]) {
                    sk_key_value_t* kv = cb->app_info_kv[i];
                    size_t buf_size = 65536;
                    uint8_t* buf = (uint8_t*)malloc(buf_size);
                    if (buf) {
                        size_t written = sk_key_value_serialize(kv, buf, buf_size);
                        if (written > 0 && written < buf_size) {
                            sk_key_value_t* copy = sk_key_value_create(NULL);
                            if (copy && sk_key_value_deserialize(copy, buf, written)) {
                                sk_steam_client_free_callback_data(data);
                                free(buf);
                                return copy;
                            }
                            sk_key_value_destroy(copy);
                        }
                        free(buf);
                    }
                }
            }
            sk_steam_client_free_callback_data(data);
            continue;
        }

        if (callback_type == SK_CLIENT_CALLBACK_LOGGED_ON) {
            sk_logged_on_callback_t* cb = (sk_logged_on_callback_t*)data;
            if (cb) {
                printf("[steam3] Logged on, result=%d\n", cb->result);
                session->logged_on = (cb->result == 1);
            }
            sk_steam_client_free_callback_data(data);
            continue;
        }

        if (callback_type == SK_CLIENT_CALLBACK_LOGGED_OFF) {
            sk_logged_off_callback_t* cb = (sk_logged_off_callback_t*)data;
            if (cb) {
                printf("[steam3] Logged off, result=%d\n", cb->result);
                session->logged_on = false;
            }
            sk_steam_client_free_callback_data(data);
            continue;
        }

        sk_steam_client_free_callback_data(data);
    }

    return NULL;
}
