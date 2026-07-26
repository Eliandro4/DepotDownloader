#ifndef DEPOT_DOWNLOADER_UTIL_H
#define DEPOT_DOWNLOADER_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *util_get_steam_os(void);
const char *util_get_steam_arch(void);
char *util_read_password(void);
int util_adler_hash(const uint8_t *data, size_t len, uint32_t *out_hash);
int util_file_sha_hash(const char *filename, uint8_t *out_hash, size_t hash_len);
int util_decode_hex_string(const char *hex, uint8_t **out_bytes, size_t *out_len);
char *util_symmetric_decrypt_ecb(const uint8_t *input, size_t input_len, const uint8_t *key, size_t key_len);
int util_set_executable(const char *path, bool value);
int util_verify_console_launch(void);
void util_progress(int state, int percent);

#ifdef __cplusplus
}
#endif

#endif
