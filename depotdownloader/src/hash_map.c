#include "hash_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t hash_str(const char *s) {
    uint32_t h = 5381;
    while (*s) {
        h = h * 33 + (uint8_t)*s;
        s++;
    }
    return h;
}

hash_map_t *hash_map_create(size_t bucket_count) {
    if (bucket_count == 0) bucket_count = 64;
    hash_map_t *map = (hash_map_t *)calloc(1, sizeof(hash_map_t));
    if (!map) return NULL;
    map->buckets = (hash_map_entry_t **)calloc(bucket_count, sizeof(hash_map_entry_t *));
    if (!map->buckets) { free(map); return NULL; }
    map->bucket_count = bucket_count;
    return map;
}

void hash_map_destroy(hash_map_t *map, void (*free_value)(void *)) {
    if (!map) return;
    for (size_t i = 0; i < map->bucket_count; ++i) {
        hash_map_entry_t *entry = map->buckets[i];
        while (entry) {
            hash_map_entry_t *next = entry->next;
            free(entry->key);
            if (free_value && entry->value) free_value(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(map->buckets);
    free(map);
}

static hash_map_entry_t *hash_map_find(hash_map_t *map, const char *key, size_t *bucket_idx) {
    uint32_t h = hash_str(key) % map->bucket_count;
    if (bucket_idx) *bucket_idx = h;
    hash_map_entry_t *entry = map->buckets[h];
    while (entry) {
        if (strcmp(entry->key, key) == 0) return entry;
        entry = entry->next;
    }
    return NULL;
}

void *hash_map_get(hash_map_t *map, const char *key) {
    if (!map || !key) return NULL;
    hash_map_entry_t *entry = hash_map_find(map, key, NULL);
    return entry ? entry->value : NULL;
}

int hash_map_set(hash_map_t *map, const char *key, void *value) {
    if (!map || !key) return -1;
    size_t bucket_idx;
    hash_map_entry_t *entry = hash_map_find(map, key, &bucket_idx);
    if (entry) {
        entry->value = value;
        return 0;
    }
    entry = (hash_map_entry_t *)calloc(1, sizeof(hash_map_entry_t));
    if (!entry) return -1;
    entry->key = strdup(key);
    entry->value = value;
    entry->next = map->buckets[bucket_idx];
    map->buckets[bucket_idx] = entry;
    map->size++;
    return 0;
}

int hash_map_remove(hash_map_t *map, const char *key) {
    if (!map || !key) return -1;
    size_t bucket_idx;
    hash_map_entry_t *entry = hash_map_find(map, key, &bucket_idx);
    if (!entry) return -1;
    hash_map_entry_t *prev = NULL;
    hash_map_entry_t *curr = map->buckets[bucket_idx];
    while (curr && curr != entry) {
        prev = curr;
        curr = curr->next;
    }
    if (prev) prev->next = entry->next;
    else map->buckets[bucket_idx] = entry->next;
    free(entry->key);
    free(entry);
    map->size--;
    return 0;
}

void hash_map_clear(hash_map_t *map, void (*free_value)(void *)) {
    if (!map) return;
    for (size_t i = 0; i < map->bucket_count; ++i) {
        hash_map_entry_t *entry = map->buckets[i];
        while (entry) {
            hash_map_entry_t *next = entry->next;
            free(entry->key);
            if (free_value && entry->value) free_value(entry->value);
            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}