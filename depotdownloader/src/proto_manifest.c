#include "../proto_manifest.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>
#include "steamkit/steamkit.h"

int proto_manifest_save_to_file(proto_manifest_t *manifest, const char *filename, const char *depot_key, size_t key_len) {
    if (!manifest || !filename) return -1;
    (void)depot_key;
    (void)key_len;
    FILE *f = fopen(filename, "wb");
    if (!f) return -1;
    fprintf(f, "DepotManifest\n");
    fprintf(f, "DepotID=%u\n", manifest->depot_id);
    fprintf(f, "ManifestGID=%llu\n", (unsigned long long)manifest->manifest_gid);
    fprintf(f, "CreationTime=%llu\n", (unsigned long long)manifest->creation_time);
    fprintf(f, "TotalUncompressedSize=%llu\n", (unsigned long long)manifest->total_uncompressed_size);
    fprintf(f, "TotalCompressedSize=%llu\n", (unsigned long long)manifest->total_compressed_size);
    fprintf(f, "FilenamesEncrypted=%d\n", manifest->filenames_encrypted ? 1 : 0);
    for (size_t i = 0; i < manifest->num_files; ++i) {
        proto_manifest_file_t *file = &manifest->files[i];
        fprintf(f, "File:\n");
        fprintf(f, "  Name=%s\n", file->filename ? file->filename : "");
        fprintf(f, "  TotalSize=%llu\n", (unsigned long long)file->total_size);
        fprintf(f, "  Flags=%u\n", file->flags);
    }
    fclose(f);
    return 0;
}

proto_manifest_t *proto_manifest_load_from_file(const char *filename) {
    if (!filename) return NULL;
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    proto_manifest_t *manifest = (proto_manifest_t *)calloc(1, sizeof(proto_manifest_t));
    if (!manifest) { fclose(f); return NULL; }

    manifest->files = NULL;
    manifest->num_files = 0;

    char line[4096];
    proto_manifest_file_t *current_file = NULL;
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        if (strncmp(line, "DepotID=", 8) == 0) {
            manifest->depot_id = (uint32_t)strtoul(line + 8, NULL, 10);
        } else if (strncmp(line, "ManifestGID=", 12) == 0) {
            manifest->manifest_gid = strtoull(line + 12, NULL, 10);
        } else if (strncmp(line, "CreationTime=", 13) == 0) {
            manifest->creation_time = strtoull(line + 13, NULL, 10);
        } else if (strncmp(line, "TotalUncompressedSize=", 22) == 0) {
            manifest->total_uncompressed_size = strtoull(line + 22, NULL, 10);
        } else if (strncmp(line, "TotalCompressedSize=", 20) == 0) {
            manifest->total_compressed_size = strtoull(line + 20, NULL, 10);
        } else if (strncmp(line, "FilenamesEncrypted=", 19) == 0) {
            manifest->filenames_encrypted = atoi(line + 19) != 0;
        } else if (strcmp(line, "File:") == 0) {
            if (current_file && manifest->num_files > 0) {
                manifest->files = (proto_manifest_file_t *)realloc(manifest->files, manifest->num_files * sizeof(proto_manifest_file_t));
                manifest->files[manifest->num_files - 1] = *current_file;
                free(current_file->filename);
                free(current_file->link_target);
                free(current_file->filename_hash);
                free(current_file->file_hash);
                free(current_file);
            }
            current_file = (proto_manifest_file_t *)calloc(1, sizeof(proto_manifest_file_t));
            manifest->num_files++;
        } else if (current_file) {
            if (strncmp(line, "  Name=", 7) == 0) {
                current_file->filename = strdup(line + 7);
            } else if (strncmp(line, "  TotalSize=", 12) == 0) {
                current_file->total_size = strtoull(line + 12, NULL, 10);
            } else if (strncmp(line, "  Flags=", 8) == 0) {
                current_file->flags = (uint32_t)strtoul(line + 8, NULL, 10);
            }
        }
    }

    if (current_file && manifest->num_files > 0) {
        manifest->files[manifest->num_files - 1] = *current_file;
        free(current_file->filename);
        free(current_file->link_target);
        free(current_file->filename_hash);
        free(current_file->file_hash);
        free(current_file);
    }

    fclose(f);
    return manifest;
}

proto_manifest_t *proto_manifest_convert_from_legacy(const char *bin_filename, uint32_t depot_id) {
    (void)bin_filename;
    (void)depot_id;
    return NULL;
}

void proto_manifest_destroy(proto_manifest_t *manifest) {
    if (!manifest) return;
    for (size_t i = 0; i < manifest->num_files; ++i) {
        proto_manifest_file_t *file = &manifest->files[i];
        free(file->filename);
        free(file->link_target);
        free(file->filename_hash);
        free(file->file_hash);
    }
    free(manifest->files);
    free(manifest);
}