#ifndef DEPOT_DOWNLOADER_PROTO_MANIFEST_H
#define DEPOT_DOWNLOADER_PROTO_MANIFEST_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <steamkit/types/depot_manifest.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *filename;
    char *link_target;
    uint64_t total_size;
    uint32_t flags;
    uint8_t *filename_hash;
    size_t filename_hash_len;
    uint8_t *file_hash;
    size_t file_hash_len;
    uint64_t offset;
    uint32_t chunk_id[5];
    uint32_t checksum;
    uint32_t compressed_length;
    uint32_t uncompressed_length;
    sk_depot_chunk_t *chunks;
    size_t num_chunks;
} proto_manifest_file_t;

typedef struct {
    proto_manifest_file_t *files;
    size_t num_files;
    uint32_t depot_id;
    uint64_t manifest_gid;
    uint64_t creation_time;
    bool filenames_encrypted;
    uint32_t encrypted_crc;
    uint64_t total_uncompressed_size;
    uint64_t total_compressed_size;
} proto_manifest_t;

int proto_manifest_save_to_file(proto_manifest_t *manifest, const char *filename, const char *depot_key, size_t key_len);
proto_manifest_t *proto_manifest_load_from_file(const char *filename);
proto_manifest_t *proto_manifest_convert_from_legacy(const char *bin_filename, uint32_t depot_id);
void proto_manifest_destroy(proto_manifest_t *manifest);

#ifdef __cplusplus
}
#endif

#endif
