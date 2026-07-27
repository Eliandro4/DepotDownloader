#include <zlib.h>
#include "../persistence.h"
#include "hash_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static string_pair_t *string_map_get(string_map_t *map, const char *key) {
    if (!map || !key) return NULL;
    for (size_t i = 0; i < map->count; ++i) {
        if (strcmp(map->pairs[i].key, key) == 0) {
            return &map->pairs[i];
        }
    }
    return NULL;
}

static int string_map_set(string_map_t *map, const char *key, const char *value) {
    if (!map || !key) return -1;
    string_pair_t *existing = string_map_get(map, key);
    if (existing) {
        free(existing->value);
        existing->value = value ? strdup(value) : NULL;
        return 0;
    }
    if (map->count >= map->capacity) {
        size_t new_cap = map->capacity == 0 ? 16 : map->capacity * 2;
        string_pair_t *new_pairs = (string_pair_t *)realloc(map->pairs, new_cap * sizeof(string_pair_t));
        if (!new_pairs) return -1;
        map->pairs = new_pairs;
        map->capacity = new_cap;
    }
    map->pairs[map->count].key = strdup(key);
    map->pairs[map->count].value = value ? strdup(value) : NULL;
    map->count++;
    return 0;
}

account_settings_store_t *account_settings_store_create(void) {
    account_settings_store_t *store = (account_settings_store_t *)calloc(1, sizeof(account_settings_store_t));
    return store;
}

void account_settings_store_destroy(account_settings_store_t *store) {
    if (!store) return;
    for (size_t i = 0; i < store->content_server_penalty.count; ++i) {
        free(store->content_server_penalty.pairs[i].key);
        free(store->content_server_penalty.pairs[i].value);
    }
    free(store->content_server_penalty.pairs);
    for (size_t i = 0; i < store->login_tokens.count; ++i) {
        free(store->login_tokens.pairs[i].key);
        free(store->login_tokens.pairs[i].value);
    }
    free(store->login_tokens.pairs);
    for (size_t i = 0; i < store->guard_data.count; ++i) {
        free(store->guard_data.pairs[i].key);
        free(store->guard_data.pairs[i].value);
    }
    free(store->guard_data.pairs);
    free(store->filename);
    free(store);
}

int account_settings_store_load_from_file(account_settings_store_t *store, const char *filename) {
    if (!store || !filename) return -1;
    if (store->loaded) return -1;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        store->loaded = true;
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *compressed = (uint8_t *)malloc(size);
    if (!compressed) { fclose(f); return -1; }
    fread(compressed, 1, size, f);
    fclose(f);

    uLongf decompressed_size = size * 4;
    uint8_t *decompressed = (uint8_t *)malloc(decompressed_size);
    if (!decompressed) { free(compressed); return -1; }

    if (uncompress(decompressed, &decompressed_size, compressed, size) != Z_OK) {
        free(compressed);
        free(decompressed);
        store->loaded = true;
        return 0;
    }
    free(compressed);

    FILE *mf = fmemopen(decompressed, decompressed_size, "rb");
    if (!mf) { free(decompressed); store->loaded = true; return -1; }

    char line[1024];
    while (fgets(line, sizeof(line), mf)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = strdup(line);
        char *value = strdup(eq + 1);
        char *nl = strchr(value, '\n');
        if (nl) *nl = '\0';
        nl = strchr(value, '\r');
        if (nl) *nl = '\0';

        if (strncmp(key, "penalty:", 8) == 0) {
            const char *host = key + 8;
            int penalty = atoi(value);
            char *pen_key = strdup(host);
            char *pen_val = (char *)malloc(32);
            snprintf(pen_val, 32, "%d", penalty);
            string_map_set(&store->content_server_penalty, pen_key, pen_val);
            free(pen_key);
            free(pen_val);
        } else if (strncmp(key, "token:", 6) == 0) {
            const char *username = key + 6;
            string_map_set(&store->login_tokens, username, value);
        } else if (strncmp(key, "guard:", 6) == 0) {
            const char *username = key + 6;
            string_map_set(&store->guard_data, username, value);
        }
        free(key);
        free(value);
    }

    fclose(mf);
    free(decompressed);
    store->filename = strdup(filename);
    store->loaded = true;
    return 0;
}

int account_settings_store_save(account_settings_store_t *store) {
    if (!store || !store->loaded || !store->filename) return -1;

    size_t buf_size = 0;
    for (size_t i = 0; i < store->content_server_penalty.count; ++i) {
        buf_size += 64 + strlen(store->content_server_penalty.pairs[i].key) + strlen(store->content_server_penalty.pairs[i].value);
    }
    for (size_t i = 0; i < store->login_tokens.count; ++i) {
        buf_size += 64 + strlen(store->login_tokens.pairs[i].key) + strlen(store->login_tokens.pairs[i].value);
    }
    for (size_t i = 0; i < store->guard_data.count; ++i) {
        buf_size += 64 + strlen(store->guard_data.pairs[i].key) + strlen(store->guard_data.pairs[i].value);
    }
    if (buf_size == 0) buf_size = 1;

    char* buf = (char*)malloc(buf_size + 1);
    if (!buf) return -1;
    size_t used = 0;
    for (size_t i = 0; i < store->content_server_penalty.count; ++i) {
        int n = snprintf(buf + used, buf_size - used, "penalty:%s=%s\n",
            store->content_server_penalty.pairs[i].key,
            store->content_server_penalty.pairs[i].value);
        if (n > 0) used += (size_t)n;
    }
    for (size_t i = 0; i < store->login_tokens.count; ++i) {
        int n = snprintf(buf + used, buf_size - used, "token:%s=%s\n",
            store->login_tokens.pairs[i].key,
            store->login_tokens.pairs[i].value);
        if (n > 0) used += (size_t)n;
    }
    for (size_t i = 0; i < store->guard_data.count; ++i) {
        int n = snprintf(buf + used, buf_size - used, "guard:%s=%s\n",
            store->guard_data.pairs[i].key,
            store->guard_data.pairs[i].value);
        if (n > 0) used += (size_t)n;
    }

    uLongf compressed_size = compressBound((uLong)used);
    uint8_t* compressed = (uint8_t*)malloc(compressed_size);
    if (!compressed) { free(buf); return -1; }

    if (compress2(compressed, &compressed_size, (const Bytef*)buf, (uLong)used, 9) != Z_OK) {
        free(compressed);
        free(buf);
        return -1;
    }

    FILE *f = fopen(store->filename, "wb");
    if (!f) { free(compressed); free(buf); return -1; }
    fwrite(compressed, 1, compressed_size, f);
    fclose(f);

    free(compressed);
    free(buf);
    return 0;
}

void account_settings_store_set_login_token(account_settings_store_t *store, const char *username, const char *token) {
    if (!store || !username || !token) return;
    string_map_set(&store->login_tokens, username, token);
}

const char *account_settings_store_get_login_token(account_settings_store_t *store, const char *username) {
    if (!store || !username) return NULL;
    string_pair_t *pair = string_map_get(&store->login_tokens, username);
    return pair ? pair->value : NULL;
}

void account_settings_store_set_guard_data(account_settings_store_t *store, const char *username, const char *guard_data) {
    if (!store || !username) return;
    string_map_set(&store->guard_data, username, guard_data);
}

const char *account_settings_store_get_guard_data(account_settings_store_t *store, const char *username) {
    if (!store || !username) return NULL;
    string_pair_t *pair = string_map_get(&store->guard_data, username);
    return pair ? pair->value : NULL;
}

int account_settings_store_penalty_add(account_settings_store_t *store, const char *host, int penalty) {
    if (!store || !host) return -1;
    char key[64];
    snprintf(key, sizeof(key), "%s", host);
    char val[32];
    snprintf(val, sizeof(val), "%d", penalty);
    return string_map_set(&store->content_server_penalty, key, val);
}

int account_settings_store_penalty_get(account_settings_store_t *store, const char *host, int *out_penalty) {
    if (!store || !host || !out_penalty) return -1;
    string_pair_t *pair = string_map_get(&store->content_server_penalty, host);
    if (!pair || !pair->value) return -1;
    *out_penalty = atoi(pair->value);
    return 0;
}

depot_config_store_t *depot_config_store_create(void) {
    depot_config_store_t *store = (depot_config_store_t *)calloc(1, sizeof(depot_config_store_t));
    return store;
}

void depot_config_store_destroy(depot_config_store_t *store) {
    if (!store) return;
    free(store->depot_ids);
    free(store->manifest_ids);
    free(store->filename);
    free(store);
}

int depot_config_store_load_from_file(depot_config_store_t *store, const char *filename) {
    if (!store || !filename) return -1;
    if (store->loaded) return -1;

    FILE *f = fopen(filename, "rb");
    if (!f) {
        store->loaded = true;
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *compressed = (uint8_t *)malloc(size);
    if (!compressed) { fclose(f); return -1; }
    fread(compressed, 1, size, f);
    fclose(f);

    uLongf decompressed_size = size * 4;
    uint8_t *decompressed = (uint8_t *)malloc(decompressed_size);
    if (!decompressed) { free(compressed); return -1; }

    if (uncompress(decompressed, &decompressed_size, compressed, size) != Z_OK) {
        free(compressed);
        free(decompressed);
        store->loaded = true;
        return 0;
    }
    free(compressed);

    FILE *mf = fmemopen(decompressed, decompressed_size, "rb");
    if (!mf) { free(decompressed); store->loaded = true; return -1; }

    char line[1024];
    while (fgets(line, sizeof(line), mf)) {
        char *sep = strchr(line, ':');
        if (!sep) continue;
        *sep = '\0';
        uint32_t depot_id = (uint32_t)strtoul(line, NULL, 10);
        uint64_t manifest_id = strtoull(sep + 1, NULL, 10);

        if (store->count >= store->capacity) {
            size_t new_cap = store->capacity == 0 ? 16 : store->capacity * 2;
            store->depot_ids = (uint32_t *)realloc(store->depot_ids, new_cap * sizeof(uint32_t));
            store->manifest_ids = (uint64_t *)realloc(store->manifest_ids, new_cap * sizeof(uint64_t));
            store->capacity = new_cap;
        }
        store->depot_ids[store->count] = depot_id;
        store->manifest_ids[store->count] = manifest_id;
        store->count++;
    }

    fclose(mf);
    free(decompressed);
    store->filename = strdup(filename);
    store->loaded = true;
    return 0;
}

int depot_config_store_save(depot_config_store_t *store) {
    if (!store || !store->loaded || !store->filename) return -1;

    size_t buf_size = store->count * 48;
    if (buf_size == 0) buf_size = 1;

    char* buf = (char*)malloc(buf_size + 1);
    if (!buf) return -1;
    size_t used = 0;
    for (size_t i = 0; i < store->count; ++i) {
        int n = snprintf(buf + used, buf_size - used, "%u:%llu\n",
            store->depot_ids[i], (unsigned long long)store->manifest_ids[i]);
        if (n > 0) used += (size_t)n;
    }

    uLongf compressed_size = compressBound((uLong)used);
    uint8_t* compressed = (uint8_t*)malloc(compressed_size);
    if (!compressed) { free(buf); return -1; }

    if (compress2(compressed, &compressed_size, (const Bytef*)buf, (uLong)used, 9) != Z_OK) {
        free(compressed);
        free(buf);
        return -1;
    }

    FILE *f = fopen(store->filename, "wb");
    if (!f) { free(compressed); free(buf); return -1; }
    fwrite(compressed, 1, compressed_size, f);
    fclose(f);

    free(compressed);
    free(buf);
    return 0;
}

int depot_config_store_set_manifest_id(depot_config_store_t *store, uint32_t depot_id, uint64_t manifest_id) {
    if (!store) return -1;
    for (size_t i = 0; i < store->count; ++i) {
        if (store->depot_ids[i] == depot_id) {
            store->manifest_ids[i] = manifest_id;
            return 0;
        }
    }
    if (store->count >= store->capacity) {
        size_t new_cap = store->capacity == 0 ? 16 : store->capacity * 2;
        store->depot_ids = (uint32_t *)realloc(store->depot_ids, new_cap * sizeof(uint32_t));
        store->manifest_ids = (uint64_t *)realloc(store->manifest_ids, new_cap * sizeof(uint64_t));
        store->capacity = new_cap;
    }
    store->depot_ids[store->count] = depot_id;
    store->manifest_ids[store->count] = manifest_id;
    store->count++;
    return 0;
}

bool depot_config_store_get_manifest_id(depot_config_store_t *store, uint32_t depot_id, uint64_t *out_manifest_id) {
    if (!store || !out_manifest_id) return false;
    for (size_t i = 0; i < store->count; ++i) {
        if (store->depot_ids[i] == depot_id) {
            *out_manifest_id = store->manifest_ids[i];
            return true;
        }
    }
    return false;
}