#ifndef HASH_MAP_H
#define HASH_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hash_map_entry {
    char *key;
    void *value;
    struct hash_map_entry *next;
} hash_map_entry_t;

typedef struct {
    hash_map_entry_t **buckets;
    size_t bucket_count;
    size_t size;
} hash_map_t;

hash_map_t *hash_map_create(size_t bucket_count);
void hash_map_destroy(hash_map_t *map, void (*free_value)(void *));
void *hash_map_get(hash_map_t *map, const char *key);
int hash_map_set(hash_map_t *map, const char *key, void *value);
int hash_map_remove(hash_map_t *map, const char *key);
void hash_map_clear(hash_map_t *map, void (*free_value)(void *));

#ifdef __cplusplus
}
#endif

#endif
