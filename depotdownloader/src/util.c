#ifdef SK_ENABLE_OPENSSL
#include <openssl/sha.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#endif
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <pwd.h>
#endif

const char *util_get_steam_os(void) {
#ifdef __linux__
    return "linux";
#elif defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#elif defined(__FreeBSD__)
    return "linux";
#else
    return "unknown";
#endif
}

const char *util_get_steam_arch(void) {
    return sizeof(void *) == 8 ? "64" : "32";
}

char *util_read_password(void) {
#ifdef _WIN32
    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (c == '\r') continue;
        if (len + 1 >= cap) {
            cap = cap == 0 ? 64 : cap * 2;
            buf = (char *)realloc(buf, cap);
            if (!buf) return NULL;
        }
        buf[len++] = (char)c;
    }
    if (len + 1 >= cap) {
        cap = cap == 0 ? 64 : cap * 2;
        buf = (char *)realloc(buf, cap);
        if (!buf) return NULL;
    }
    buf[len] = '\0';
    return buf;
#else
    struct termios old, cur;
    tcgetattr(STDIN_FILENO, &old);
    cur = old;
    cur.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &cur);

    char *buf = NULL;
    size_t cap = 0;
    size_t len = 0;
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
        if (c == '\r') continue;
        if (len + 1 >= cap) {
            cap = cap == 0 ? 64 : cap * 2;
            buf = (char *)realloc(buf, cap);
            if (!buf) { tcsetattr(STDIN_FILENO, TCSANOW, &old); return NULL; }
        }
        buf[len++] = (char)c;
    }
    if (len + 1 >= cap) {
        cap = cap == 0 ? 64 : cap * 2;
        buf = (char *)realloc(buf, cap);
        if (!buf) { tcsetattr(STDIN_FILENO, TCSANOW, &old); return NULL; }
    }
    buf[len] = '\0';
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    return buf;
#endif
}

int util_adler_hash(const uint8_t *data, size_t len, uint32_t *out_hash) {
    if (!data || !out_hash) return -1;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    *out_hash = a | (b << 16);
    return 0;
}

int util_file_sha_hash(const char *filename, uint8_t *out_hash, size_t hash_len) {
    if (!filename || !out_hash || hash_len < 20) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;

#ifdef SK_ENABLE_OPENSSL
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        SHA1_Update(&ctx, buf, n);
    }
    SHA1_Final(out_hash, &ctx);
#else
    fclose(f);
    return -1;
#endif

    fclose(f);
    return 0;
}

int util_decode_hex_string(const char *hex, uint8_t **out_bytes, size_t *out_len) {
    if (!hex || !out_bytes || !out_len) return -1;
    size_t len = strlen(hex);
    if (len % 2 != 0) return -1;
    *out_len = len / 2;
    uint8_t *buf = (uint8_t *)malloc(*out_len);
    if (!buf) return -1;
    for (size_t i = 0; i < *out_len; ++i) {
        char byte[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        buf[i] = (uint8_t)strtol(byte, NULL, 16);
    }
    *out_bytes = buf;
    return 0;
}

char *util_symmetric_decrypt_ecb(const uint8_t *input, size_t input_len, const uint8_t *key, size_t key_len) {
#ifdef SK_ENABLE_OPENSSL
    if (!input || !key || key_len != 32) return NULL;
    AES_KEY aes_key;
    if (AES_set_decrypt_key(key, 256, &aes_key) != 0) return NULL;
    size_t out_len = input_len;
    uint8_t *out = (uint8_t *)malloc(out_len);
    if (!out) return NULL;
    for (size_t i = 0; i < input_len; i += 16) {
        AES_ecb_encrypt(input + i, out + i, &aes_key, AES_DECRYPT);
    }
    return (char *)out;
#else
    (void)input;
    (void)input_len;
    (void)key;
    (void)key_len;
    return NULL;
#endif
}

int util_set_executable(const char *path, bool value) {
    if (!path) return -1;
#ifdef _WIN32
    return 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    mode_t mode = st.st_mode;
    mode_t exec_mask = S_IXUSR | S_IXGRP | S_IXOTH;
    if (value) {
        if ((mode & exec_mask) == exec_mask) return 0;
        return chmod(path, mode | exec_mask);
    } else {
        if ((mode & exec_mask) == 0) return 0;
        return chmod(path, mode & ~exec_mask);
    }
#endif
}

int util_verify_console_launch(void) {
#ifdef _WIN32
    return 0;
#else
    return 0;
#endif
}

void util_progress(int state, int percent) {
    (void)state;
    (void)percent;
}
