#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef SK_ENABLE_OPENSSL
#include <openssl/evp.h>
#include <openssl/aes.h>
#endif

#include <steamkit/types/depot_manifest.h>
#include "../proto_manifest.h"

static int aes_cbc_encrypt(const uint8_t* input, size_t input_len, const uint8_t* key, size_t key_len, uint8_t** out, size_t* out_len) {
#ifdef SK_ENABLE_OPENSSL
    if (!input || !key || key_len != 32 || !out || !out_len) return -1;
    uint8_t iv[16];
    memcpy(iv, key, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    size_t cap = input_len + AES_BLOCK_SIZE;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0;
    if (1 != EVP_EncryptUpdate(ctx, buf, &len, input, (int)input_len)) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    int total = len;

    if (1 != EVP_EncryptFinal_ex(ctx, buf + len, &len)) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total += len;

    EVP_CIPHER_CTX_free(ctx);
    *out = buf;
    *out_len = (size_t)total;
    return 0;
#else
    (void)input; (void)input_len; (void)key; (void)key_len; (void)out; (void)out_len;
    return -1;
#endif
}

static int aes_cbc_decrypt(const uint8_t* input, size_t input_len, const uint8_t* key, size_t key_len, uint8_t** out, size_t* out_len) {
#ifdef SK_ENABLE_OPENSSL
    if (!input || !key || key_len != 32 || !out || !out_len) return -1;
    uint8_t iv[16];
    memcpy(iv, key, 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    size_t cap = input_len + AES_BLOCK_SIZE;
    uint8_t* buf = (uint8_t*)malloc(cap);
    if (!buf) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0;
    if (1 != EVP_DecryptUpdate(ctx, buf, &len, input, (int)input_len)) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    int total = len;

    if (1 != EVP_DecryptFinal_ex(ctx, buf + len, &len)) {
        free(buf);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    total += len;

    EVP_CIPHER_CTX_free(ctx);
    *out = buf;
    *out_len = (size_t)total;
    return 0;
#else
    (void)input; (void)input_len; (void)key; (void)key_len; (void)out; (void)out_len;
    return -1;
#endif
}

static void encrypt_filenames(proto_manifest_t* manifest, const uint8_t* depot_key, size_t key_len) {
    if (!manifest || !depot_key || key_len != 32) return;
    for (size_t i = 0; i < manifest->num_files; ++i) {
        proto_manifest_file_t* file = &manifest->files[i];
        if (!file->filename) continue;
        size_t flen = strlen(file->filename) + 1;
        uint8_t* enc = NULL;
        size_t enclen = 0;
        if (aes_cbc_encrypt((const uint8_t*)file->filename, flen, depot_key, key_len, &enc, &enclen) == 0) {
            free(file->filename);
            file->filename = (char*)enc;
        }
    }
    manifest->filenames_encrypted = true;
}

static void decrypt_filenames(proto_manifest_t* manifest, const uint8_t* depot_key, size_t key_len) {
    if (!manifest || !depot_key || key_len != 32) return;
    for (size_t i = 0; i < manifest->num_files; ++i) {
        proto_manifest_file_t* file = &manifest->files[i];
        if (!file->filename) continue;
        size_t flen = strlen(file->filename);
        uint8_t* dec = NULL;
        size_t declen = 0;
        if (aes_cbc_decrypt((const uint8_t*)file->filename, flen, depot_key, key_len, &dec, &declen) == 0) {
            free(file->filename);
            file->filename = (char*)dec;
        }
    }
    manifest->filenames_encrypted = false;
}

int proto_manifest_save_to_file(proto_manifest_t *manifest, const char *filename, const char *depot_key, size_t key_len) {
    if (!manifest || !filename) return -1;

    proto_manifest_t tmp;
    memcpy(&tmp, manifest, sizeof(tmp));
    tmp.files = NULL;

    if (depot_key && key_len == 32) {
        tmp.files = (proto_manifest_file_t*)calloc(manifest->num_files, sizeof(proto_manifest_file_t));
        if (!tmp.files) return -1;
        memcpy(tmp.files, manifest->files, manifest->num_files * sizeof(proto_manifest_file_t));
        encrypt_filenames(&tmp, (const uint8_t*)depot_key, key_len);
    }

    int result = 0;
    FILE *f = fopen(filename, "wb");
    if (!f) { free(tmp.files); return -1; }

    fprintf(f, "DepotManifest\n");
    fprintf(f, "DepotID=%u\n", manifest->depot_id);
    fprintf(f, "ManifestGID=%llu\n", (unsigned long long)manifest->manifest_gid);
    fprintf(f, "CreationTime=%llu\n", (unsigned long long)manifest->creation_time);
    fprintf(f, "TotalUncompressedSize=%llu\n", (unsigned long long)manifest->total_uncompressed_size);
    fprintf(f, "TotalCompressedSize=%llu\n", (unsigned long long)manifest->total_compressed_size);
    fprintf(f, "FilenamesEncrypted=%d\n", tmp.filenames_encrypted ? 1 : 0);
    fprintf(f, "EncryptedCRC=%u\n", manifest->encrypted_crc);

    for (size_t i = 0; i < tmp.num_files; ++i) {
        proto_manifest_file_t *file = &tmp.files[i];
        fprintf(f, "File:\n");
        fprintf(f, "  Name=%s\n", file->filename ? file->filename : "");
        fprintf(f, "  TotalSize=%llu\n", (unsigned long long)file->total_size);
        fprintf(f, "  Flags=%u\n", file->flags);
        fprintf(f, "  NumChunks=%zu\n", file->num_chunks);
    }

    fclose(f);
    free(tmp.files);
    return result;
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
        } else if (strncmp(line, "EncryptedCRC=", 12) == 0) {
            manifest->encrypted_crc = (uint32_t)strtoul(line + 12, NULL, 10);
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
            } else if (strncmp(line, "  NumChunks=", 12) == 0) {
                current_file->num_chunks = (size_t)strtoul(line + 12, NULL, 10);
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
    if (!bin_filename) return NULL;

    sk_depot_manifest_t* sk_manifest = sk_depot_manifest_load_from_file(bin_filename);
    if (!sk_manifest) return NULL;

    proto_manifest_t* manifest = (proto_manifest_t*)calloc(1, sizeof(proto_manifest_t));
    if (!manifest) {
        sk_depot_manifest_destroy(sk_manifest);
        return NULL;
    }

    manifest->depot_id = depot_id;
    manifest->manifest_gid = sk_manifest->manifest_gid;
    manifest->creation_time = sk_manifest->creation_time;
    manifest->filenames_encrypted = sk_manifest->filenames_encrypted;
    manifest->encrypted_crc = sk_manifest->encrypted_crc;
    manifest->total_uncompressed_size = sk_manifest->total_uncompressed_size;
    manifest->total_compressed_size = sk_manifest->total_compressed_size;
    manifest->num_files = sk_manifest->num_files;

    manifest->files = (proto_manifest_file_t*)calloc(manifest->num_files, sizeof(proto_manifest_file_t));
    if (!manifest->files) {
        sk_depot_manifest_destroy(sk_manifest);
        free(manifest);
        return NULL;
    }

    for (uint32_t i = 0; i < sk_manifest->num_files; ++i) {
        const sk_depot_file_t* src = &sk_manifest->files[i];
        proto_manifest_file_t* dst = &manifest->files[i];

        dst->filename = src->filename ? strdup(src->filename) : NULL;
        dst->link_target = src->link_target ? strdup(src->link_target) : NULL;
        dst->total_size = src->total_size;
        dst->flags = (uint32_t)src->flags;
        dst->compressed_length = 0;
        dst->uncompressed_length = 0;
        dst->offset = 0;
        dst->checksum = 0;
        dst->chunk_id[0] = dst->chunk_id[1] = dst->chunk_id[2] = dst->chunk_id[3] = dst->chunk_id[4] = 0;
        dst->num_chunks = src->num_chunks;
        dst->chunks = NULL;
        dst->filename_hash = src->filename_hash ? (uint8_t*)malloc(src->filename_hash_len) : NULL;
        dst->filename_hash_len = src->filename_hash_len;
        if (dst->filename_hash && src->filename_hash) {
            memcpy(dst->filename_hash, src->filename_hash, src->filename_hash_len);
        }
        dst->file_hash = src->file_hash ? (uint8_t*)malloc(src->file_hash_len) : NULL;
        dst->file_hash_len = src->file_hash_len;
        if (dst->file_hash && src->file_hash) {
            memcpy(dst->file_hash, src->file_hash, src->file_hash_len);
        }
    }

    sk_depot_manifest_destroy(sk_manifest);
    return manifest;
}

void proto_manifest_encrypt_filenames(proto_manifest_t* manifest, const char* depot_key, size_t key_len) {
    if (!manifest || !depot_key || key_len != 32) return;
    encrypt_filenames(manifest, (const uint8_t*)depot_key, key_len);
}

void proto_manifest_decrypt_filenames(proto_manifest_t* manifest, const char* depot_key, size_t key_len) {
    if (!manifest || !depot_key || key_len != 32) return;
    decrypt_filenames(manifest, (const uint8_t*)depot_key, key_len);
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
