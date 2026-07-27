#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <pthread.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

#include "../content_downloader.h"
#include "../config.h"
#include "../persistence.h"
#include "../proto_manifest.h"
#include "../steam3_session.h"
#include "hash_map.h"
#include <steamkit/steamkit.h>
#include <steamkit/types/key_value.h>
#include <steamkit/cdn/cdn_client.h>
#include <steamkit/types/depot_manifest.h>

static pthread_mutex_t g_download_mutex = PTHREAD_MUTEX_INITIALIZER;
static hash_map_t *g_active_downloads = NULL;
static uint32_t g_download_index = 0;
static bool g_download_aborted = false;

static void ensure_directory_exists(const char *path) {
    if (!path || !*path) return;
    char *buf = strdup(path);
    if (!buf) return;
    for (char *p = buf + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
#ifdef _WIN32
            _mkdir(buf);
#else
            mkdir(buf, 0755);
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    _mkdir(buf);
#else
    mkdir(buf, 0755);
#endif
    free(buf);
}

static sk_key_value_t* find_child_named(sk_key_value_t* kv, const char* name) {
    if (!kv || !name) return NULL;
    size_t count = sk_key_value_child_count(kv);
    for (size_t i = 0; i < count; ++i) {
        sk_key_value_t* child = sk_key_value_child(kv, i);
        const char* child_name = sk_key_value_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
    }
    return NULL;
}

static uint64_t find_manifest_id_for_branch(sk_key_value_t* depot_kv, const char* branch) {
    if (!depot_kv) return 0;
    sk_key_value_t* manifests = find_child_named(depot_kv, "manifests");
    if (!manifests) return 0;

    sk_key_value_t* branch_kv = find_child_named(manifests, branch);
    if (branch_kv && sk_key_value_type(branch_kv) == SK_KV_TYPE_STRING) {
        const char* manifest_str = sk_key_value_string(branch_kv);
        if (manifest_str) {
            return strtoull(manifest_str, NULL, 10);
        }
    }
    return 0;
}

static int32_t find_depot_platform(sk_key_value_t* depot_kv) {
    if (!depot_kv) return -1;
    sk_key_value_t* config = find_child_named(depot_kv, "config");
    if (!config) return -1;
    sk_key_value_t* os = find_child_named(config, "os");
    if (!os) return -1;
    const char* os_str = sk_key_value_string(os);
    if (!os_str) return -1;
    if (strcmp(os_str, "windows") == 0) return 0;
    if (strcmp(os_str, "linux") == 0) return 1;
    if (strcmp(os_str, "macos") == 0) return 2;
    return -1;
}

static int32_t find_depot_arch(sk_key_value_t* depot_kv) {
    if (!depot_kv) return -1;
    sk_key_value_t* config = find_child_named(depot_kv, "config");
    if (!config) return -1;
    sk_key_value_t* arch = find_child_named(config, "arch");
    if (!arch) return -1;
    const char* arch_str = sk_key_value_string(arch);
    if (!arch_str) return -1;
    if (strcmp(arch_str, "32") == 0) return 32;
    if (strcmp(arch_str, "64") == 0) return 64;
    return -1;
}

download_job_t* download_job_create(uint32_t app_id, uint32_t depot_id, uint64_t manifest_id) {
    download_job_t *job = (download_job_t *)calloc(1, sizeof(download_job_t));
    if (!job) return NULL;
    job->app_id = app_id;
    job->depot_id = depot_id;
    job->manifest_id = manifest_id;
    job->cell_id = 0;
    job->priority = 0;
    return job;
}

void download_job_destroy(download_job_t *job) {
    if (!job) return;
    free(job);
}

bool content_downloader_init(void) {
    g_active_downloads = hash_map_create(16);
    return g_active_downloads != NULL;
}

void content_downloader_shutdown(void) {
    if (!g_active_downloads) return;
    hash_map_clear(g_active_downloads, free);
    hash_map_destroy(g_active_downloads, free);
    g_active_downloads = NULL;
}

static int content_downloader_download_depot_files(sk_steam3_session_t* session,
    uint32_t app_id, uint32_t depot_id, sk_depot_manifest_t* manifest,
    const char* install_dir, const uint8_t* depot_key, size_t key_len,
    bool verify_all, bool download_manifest_only, uint32_t max_downloads) {
    (void)max_downloads;

    if (!manifest || !install_dir || !depot_key || key_len != 32) return -1;

    char depot_path[1024];
    snprintf(depot_path, sizeof(depot_path), "%s/depot_%u", install_dir, depot_id);
    ensure_directory_exists(depot_path);

    char staging_path[1024];
    snprintf(staging_path, sizeof(staging_path), "%s/staging", depot_path);
    ensure_directory_exists(staging_path);

    size_t total_bytes = 0;
    size_t downloaded_bytes = 0;
    int num_files_processed = 0;
    int num_chunks_downloaded = 0;

    for (uint32_t i = 0; i < manifest->num_files; ++i) {
        total_bytes += manifest->files[i].total_size;
    }

    printf("[content_downloader] Depot %u: %u files, %zu total bytes\n", depot_id, manifest->num_files, total_bytes);

    if (download_manifest_only) {
        printf("[content_downloader] Manifest-only mode, skipping file downloads\n");
        return 0;
    }

    for (uint32_t i = 0; i < manifest->num_files; ++i) {
        if (g_download_aborted) {
            printf("[content_downloader] Download aborted\n");
            return DEPOT_DOWNLOADER_RESULT_CANCELLED;
        }

        sk_depot_file_t* file = &manifest->files[i];
        num_files_processed++;

        if (!file->filename || strlen(file->filename) == 0) continue;

        char file_path[2048];
        snprintf(file_path, sizeof(file_path), "%s/%s", depot_path, file->filename);

        if (file->total_size == 0) {
            ensure_directory_exists(file_path);
            continue;
        }

        char* last_slash = strrchr(file_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            ensure_directory_exists(file_path);
            *last_slash = '/';
        }

        FILE* f = fopen(file_path, "wb");
        if (!f) {
            printf("[content_downloader] Failed to create file: %s\n", file_path);
            continue;
        }

        size_t file_downloaded = 0;
        for (uint32_t c = 0; c < file->num_chunks; ++c) {
            if (g_download_aborted) {
                fclose(f);
                return DEPOT_DOWNLOADER_RESULT_CANCELLED;
            }

            uint8_t* chunk_buf = (uint8_t*)malloc(file->chunks[c].uncompressed_length > 0 ? file->chunks[c].uncompressed_length : 65536);
            if (!chunk_buf) {
                fclose(f);
                return DEPOT_DOWNLOADER_RESULT_ERROR;
            }

            int written = steam3_session_download_depot_chunk(
                session, depot_id,
                &file->chunks[c],
                depot_key, key_len,
                chunk_buf, (size_t)(file->chunks[c].uncompressed_length > 0 ? file->chunks[c].uncompressed_length : 65536));

            if (written > 0) {
                fwrite(chunk_buf, 1, (size_t)written, f);
                file_downloaded += (size_t)written;
                downloaded_bytes += (size_t)written;
                num_chunks_downloaded++;
            }

            free(chunk_buf);
        }

        fclose(f);

        int percent = (int)(total_bytes > 0 ? (downloaded_bytes * 100) / total_bytes : 0);
        printf("[content_downloader] Progress: %d%% (%d/%d files, %d chunks)\r",
               percent, num_files_processed, (int)manifest->num_files, num_chunks_downloaded);
        fflush(stdout);
    }

    printf("\n[content_downloader] Depot %u download complete: %d files, %d chunks\n",
           depot_id, num_files_processed, num_chunks_downloaded);
    return 0;
}

static int content_downloader_download_depot(sk_steam3_session_t* session,
    uint32_t app_id, uint32_t depot_id, uint64_t manifest_id,
    const char* branch, const char* install_dir,
    bool verify_all, bool download_manifest_only, uint32_t max_downloads) {
    uint8_t depot_key[32];
    if (steam3_session_get_depot_key(session, depot_id, app_id, depot_key, 32) != 0) {
        printf("[content_downloader] Failed to get depot key for depot=%u\n", depot_id);
        return -1;
    }

    uint64_t request_code = 0;
    if (manifest_id == DEPOT_DOWNLOADER_INVALID_MANIFEST_ID) {
        printf("[content_downloader] Manifest ID not specified for depot=%u, need to resolve from app info\n", depot_id);
        return -1;
    }

    int req_result = steam3_session_request_manifest(session, depot_id, (uint32_t)app_id, manifest_id, branch);
    if (req_result != 0) {
        printf("[content_downloader] Failed to get manifest request code for depot=%u\n", depot_id);
        return -1;
    }
    request_code = steam3_session_get_manifest_request_code(session);

    void* manifest_data = steam3_session_download_manifest(
        session, depot_id, manifest_id, request_code, depot_key, 32);
    if (!manifest_data) {
        printf("[content_downloader] Failed to download manifest for depot=%u\n", depot_id);
        return -1;
    }

    sk_depot_manifest_t* sk_manifest = (sk_depot_manifest_t*)manifest_data;

    char manifest_filename[512];
    snprintf(manifest_filename, sizeof(manifest_filename), "%s/depot_%u_%llu.bin", install_dir ? install_dir : ".", depot_id, (unsigned long long)sk_manifest->manifest_gid);
    ensure_directory_exists(install_dir ? install_dir : ".");
    sk_depot_manifest_save_to_file(sk_manifest, manifest_filename);

    int result = content_downloader_download_depot_files(
        session, app_id, depot_id, sk_manifest,
        install_dir ? install_dir : ".", depot_key, 32,
        verify_all, download_manifest_only, max_downloads);

    sk_depot_manifest_destroy(sk_manifest);
    return result;
}

int content_downloader_download_app(uint32_t app_id, const char *branch,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, bool download_manifest_only,
    uint32_t *depot_ids, size_t num_depot_ids, uint64_t *manifest_ids, size_t num_manifest_ids) {

    (void)download_all_platforms;
    (void)download_all_archs;
    (void)download_all_languages;
    (void)language;
    (void)low_violence;

    printf("[content_downloader] DownloadApp: app=%u branch=%s dir=%s\n", app_id, branch ? branch : "public", install_dir ? install_dir : ".");

    sk_steam3_session_t* session = steam3_session_create(NULL, NULL, app_id, 0);
    if (!session) {
        printf("[content_downloader] Failed to create steam session\n");
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    int result = steam3_session_connect(session);
    if (result != 0) {
        printf("[content_downloader] Failed to connect to Steam\n");
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    result = steam3_session_log_on(session);
    if (result != 0) {
        printf("[content_downloader] Failed to log on to Steam\n");
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    result = steam3_session_request_app_info(session, app_id);
    if (result != 0) {
        printf("[content_downloader] Failed to request app info\n");
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    bool got_product_info = false;
    for (int retry = 0; retry < 20; ++retry) {
        result = steam3_session_wait_for_callback(session, 5000);
        if (result > 0) {
            got_product_info = true;
            break;
        }
    }

    if (!got_product_info) {
        printf("[content_downloader] Timeout waiting for PICS product info\n");
    }

    const char* effective_branch = branch ? branch : "public";

    size_t num_depots = num_depot_ids > 0 ? num_depot_ids : 1;
    for (size_t i = 0; i < num_depots; ++i) {
        uint32_t depot_id = (i < num_depot_ids) ? depot_ids[i] : 0;
        uint64_t manifest_id = DEPOT_DOWNLOADER_INVALID_MANIFEST_ID;

        if (depot_id == 0) {
            printf("[content_downloader] No depot specified\n");
            continue;
        }

        if (num_manifest_ids > 0 && i < num_manifest_ids) {
            manifest_id = manifest_ids[i];
        }

        printf("[content_downloader] Downloading depot=%u manifest=%llu\n",
               depot_id, (unsigned long long)manifest_id);

        int dep_result = content_downloader_download_depot(
            session, app_id, depot_id, manifest_id,
            effective_branch, install_dir ? install_dir : ".",
            verify_all, download_manifest_only, max_downloads);

        if (dep_result != 0) {
            printf("[content_downloader] Depot %u download failed with result=%d\n", depot_id, dep_result);
        }
    }

    steam3_session_destroy(session);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_pubfile(uint32_t app_id, uint64_t published_file_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads) {

    (void)app_id; (void)published_file_id; (void)install_dir;
    (void)download_all_platforms; (void)download_all_archs;
    (void)download_all_languages; (void)language; (void)low_violence;
    (void)verify_all; (void)max_downloads;

    printf("[content_downloader] download_pubfile app_id=%u pubfile_id=%llu - not yet implemented\n",
           app_id, (unsigned long long)published_file_id);
    return DEPOT_DOWNLOADER_RESULT_ERROR;
}

int content_downloader_download_ugc(uint32_t app_id, uint64_t ugc_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads) {

    (void)app_id; (void)ugc_id; (void)install_dir;
    (void)download_all_platforms; (void)download_all_archs;
    (void)download_all_languages; (void)language; (void)low_violence;
    (void)verify_all; (void)max_downloads;

    printf("[content_downloader] download_ugc app_id=%u ugc_id=%llu - not yet implemented\n",
           app_id, (unsigned long long)ugc_id);
    return DEPOT_DOWNLOADER_RESULT_ERROR;
}

int content_downloader_start_job(download_job_t *job) {
    if (!job) return -1;
    pthread_mutex_lock(&g_download_mutex);
    char key[64];
    snprintf(key, sizeof(key), "job_%u", g_download_index++);
    hash_map_set(g_active_downloads, key, job);
    pthread_mutex_unlock(&g_download_mutex);
    printf("[content_downloader] started job for depot=%u manifest=%llu\n",
           job->depot_id, (unsigned long long)job->manifest_id);
    return 0;
}

int content_downloader_cancel_job(download_job_t *job) {
    if (!job) return -1;
    printf("[content_downloader] cancelled job for depot=%u\n", job->depot_id);
    return 0;
}
