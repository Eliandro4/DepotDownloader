#ifndef DEPOT_DOWNLOADER_PERSISTENCE_H
#define DEPOT_DOWNLOADER_PERSISTENCE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *key;
    char *value;
} string_pair_t;

typedef struct {
    string_pair_t *pairs;
    size_t count;
    size_t capacity;
} string_map_t;

typedef struct {
    string_map_t content_server_penalty;
    string_map_t login_tokens;
    string_map_t guard_data;
    char *filename;
    bool loaded;
} account_settings_store_t;

typedef struct {
    uint32_t *depot_ids;
    uint64_t *manifest_ids;
    size_t count;
    size_t capacity;
    char *filename;
    bool loaded;
} depot_config_store_t;

account_settings_store_t *account_settings_store_create(void);
void account_settings_store_destroy(account_settings_store_t *store);
int account_settings_store_load_from_file(account_settings_store_t *store, const char *filename);
int account_settings_store_save(account_settings_store_t *store);
void account_settings_store_set_login_token(account_settings_store_t *store, const char *username, const char *token);
const char *account_settings_store_get_login_token(account_settings_store_t *store, const char *username);
void account_settings_store_set_guard_data(account_settings_store_t *store, const char *username, const char *guard_data);
const char *account_settings_store_get_guard_data(account_settings_store_t *store, const char *username);
int account_settings_store_penalty_add(account_settings_store_t *store, const char *host, int penalty);
int account_settings_store_penalty_get(account_settings_store_t *store, const char *host, int *out_penalty);

depot_config_store_t *depot_config_store_create(void);
void depot_config_store_destroy(depot_config_store_t *store);
int depot_config_store_load_from_file(depot_config_store_t *store, const char *filename);
int depot_config_store_save(depot_config_store_t *store);
int depot_config_store_set_manifest_id(depot_config_store_t *store, uint32_t depot_id, uint64_t manifest_id);
bool depot_config_store_get_manifest_id(depot_config_store_t *store, uint32_t depot_id, uint64_t *out_manifest_id);

#ifdef __cplusplus
}
#endif

#endif
