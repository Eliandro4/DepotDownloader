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
#include "util.h"
#include "hash_map.h"
#include <steamkit/steamkit.h>
#include <steamkit/types/key_value.h>
#include <steamkit/cdn/cdn_client.h>
#include <steamkit/types/depot_manifest.h>

static pthread_mutex_t g_download_mutex = PTHREAD_MUTEX_INITIALIZER;
static hash_map_t *g_active_downloads = NULL;
static uint32_t g_download_index = 0;
static bool g_download_aborted = false;

static hash_map_t* load_filelist(const char* path) {
    if (!path || !path[0]) return NULL;
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    hash_map_t* map = hash_map_create(64);
    if (!map) { fclose(f); return NULL; }
    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';
        if (line[0] == '\0') continue;
        char* dup = strdup(line);
        if (!dup) continue;
        hash_map_set(map, dup, dup);
    }
    fclose(f);
    return map;
}

static bool file_in_filelist(hash_map_t* filelist, const char* filename) {
    if (!filelist || !filename) return true;
    return hash_map_get(filelist, filename) != NULL;
}

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

static uint64_t resolve_manifest_id_for_depot(sk_key_value_t* app_info, uint32_t depot_id, const char* branch) {
    if (!app_info) return 0;
    char depot_str[32];
    snprintf(depot_str, sizeof(depot_str), "%u", depot_id);

    sk_key_value_t* appinfo = find_child_named(app_info, "appinfo");
    if (!appinfo) appinfo = app_info;

    sk_key_value_t* depots = find_child_named(appinfo, "depots");
    if (!depots) return 0;

    sk_key_value_t* depot_kv = find_child_named(depots, depot_str);
    if (!depot_kv) return 0;

    sk_key_value_t* manifests = find_child_named(depot_kv, "manifests");
    if (!manifests) return 0;

    sk_key_value_t* branch_kv = find_child_named(manifests, branch ? branch : "public");
    if (!branch_kv) return 0;

    const char* manifest_str = sk_key_value_string(branch_kv);
    if (!manifest_str) return 0;

    return strtoull(manifest_str, NULL, 10);
}

static bool depot_matches_options(sk_key_value_t* app_info, uint32_t depot_id,
    bool download_all_platforms, bool download_all_archs,
    const char* language, bool low_violence) {
    if (download_all_platforms && download_all_archs) return true;

    char depot_str[32];
    snprintf(depot_str, sizeof(depot_str), "%u", depot_id);

    sk_key_value_t* appinfo = find_child_named(app_info, "appinfo");
    if (!appinfo) appinfo = app_info;

    sk_key_value_t* depots = find_child_named(appinfo, "depots");
    if (!depots) return true;

    sk_key_value_t* depot_kv = find_child_named(depots, depot_str);
    if (!depot_kv) return true;

    sk_key_value_t* config = find_child_named(depot_kv, "config");
    if (!config) return true;

    if (!download_all_platforms) {
        sk_key_value_t* os = find_child_named(config, "os");
        const char* os_str = os ? sk_key_value_string(os) : NULL;
        const char* current_os = util_get_steam_os();
        if (!os_str || !current_os || strcmp(os_str, current_os) != 0) {
            return false;
        }
    }

    if (!download_all_archs) {
        sk_key_value_t* arch = find_child_named(config, "arch");
        const char* arch_str = arch ? sk_key_value_string(arch) : NULL;
        const char* current_arch = util_get_steam_arch();
        if (!arch_str || !current_arch || strcmp(arch_str, current_arch) != 0) {
            return false;
        }
    }

    if (language && language[0]) {
        sk_key_value_t* lang = find_child_named(config, "language");
        const char* lang_str = lang ? sk_key_value_string(lang) : NULL;
        if (!lang_str || strcmp(lang_str, language) != 0) {
            return false;
        }
    }

    if (low_violence) {
        sk_key_value_t* flags = find_child_named(depot_kv, "flags");
        if (flags) {
            const char* flags_str = sk_key_value_string(flags);
            if (flags_str && strstr(flags_str, "lowviolence")) {
                return false;
            }
        }
    }

    return true;
}

static int enumerate_depots_from_app_info(sk_key_value_t* app_info, uint32_t** out_depots, size_t* out_count) {
    if (!app_info || !out_depots || !out_count) return -1;

    sk_key_value_t* appinfo = find_child_named(app_info, "appinfo");
    if (!appinfo) appinfo = app_info;

    sk_key_value_t* depots = find_child_named(appinfo, "depots");
    if (!depots) return -1;

    size_t count = sk_key_value_child_count(depots);
    if (count == 0) return -1;

    uint32_t* list = (uint32_t*)malloc(count * sizeof(uint32_t));
    if (!list) return -1;

    size_t num = 0;
    for (size_t i = 0; i < count; ++i) {
        sk_key_value_t* child = sk_key_value_child(depots, i);
        const char* name = sk_key_value_name(child);
        if (name && name[0] >= '0' && name[0] <= '9') {
            list[num++] = (uint32_t)strtoul(name, NULL, 10);
        }
    }

    if (num == 0) {
        free(list);
        return -1;
    }

    *out_depots = list;
    *out_count = num;
    return 0;
}

static int32_t resolve_depot_platform(sk_key_value_t* app_info, uint32_t depot_id) {
    if (!app_info) return -1;
    char depot_str[32];
    snprintf(depot_str, sizeof(depot_str), "%u", depot_id);

    sk_key_value_t* appinfo = find_child_named(app_info, "appinfo");
    if (!appinfo) appinfo = app_info;

    sk_key_value_t* depots = find_child_named(appinfo, "depots");
    if (!depots) return -1;

    sk_key_value_t* depot_kv = find_child_named(depots, depot_str);
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

static int32_t resolve_depot_arch(sk_key_value_t* app_info, uint32_t depot_id) {
    if (!app_info) return -1;
    char depot_str[32];
    snprintf(depot_str, sizeof(depot_str), "%u", depot_id);

    sk_key_value_t* appinfo = find_child_named(app_info, "appinfo");
    if (!appinfo) appinfo = app_info;

    sk_key_value_t* depots = find_child_named(appinfo, "depots");
    if (!depots) return -1;

    sk_key_value_t* depot_kv = find_child_named(depots, depot_str);
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
    bool verify_all, bool download_manifest_only, uint32_t max_downloads, hash_map_t* filelist) {
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
        if (file->flags & SK_DEPOT_FILE_FLAG_DIRECTORY) continue;

        if (!file_in_filelist(filelist, file->filename)) {
            continue;
        }

        char file_path[2048];
        snprintf(file_path, sizeof(file_path), "%s/%s", depot_path, file->filename);

        if (file->total_size == 0) {
            FILE* empty = fopen(file_path, "wb");
            if (empty) fclose(empty);
            continue;
        }

        char* last_slash = strrchr(file_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            ensure_directory_exists(file_path);
            *last_slash = '/';
        }

        FILE* f = fopen(file_path, "rb+");
        if (!f) {
            f = fopen(file_path, "wb");
            if (!f) {
                printf("[content_downloader] Failed to create file: %s\n", file_path);
                continue;
            }
        }

        if (file->total_size > 0) {
#ifdef _WIN32
            _chsize_s(_fileno(f), file->total_size);
#else
            ftruncate(fileno(f), file->total_size);
#endif
        }

        fclose(f);

        f = fopen(file_path, "rb+");
        if (!f) {
            printf("[content_downloader] Failed to open file: %s\n", file_path);
            continue;
        }

        size_t file_downloaded = 0;
        for (uint32_t c = 0; c < file->num_chunks; ++c) {
            if (g_download_aborted) {
                fclose(f);
                return DEPOT_DOWNLOADER_RESULT_CANCELLED;
            }

            uint32_t chunk_size = file->chunks[c].uncompressed_length > 0 ? file->chunks[c].uncompressed_length : 65536;
            uint8_t* chunk_buf = (uint8_t*)malloc(chunk_size);
            if (!chunk_buf) {
                fclose(f);
                return DEPOT_DOWNLOADER_RESULT_ERROR;
            }

            int written = -1;
            for (int retry = 0; retry < 3; ++retry) {
                written = steam3_session_download_depot_chunk(
                    session, depot_id,
                    &file->chunks[c],
                    depot_key, key_len,
                    chunk_buf, chunk_size);

                if (written > 0) break;
                printf("[content_downloader] Chunk %u/%u retry %d for depot=%u\n",
                       c + 1, file->num_chunks, retry + 1, depot_id);
            }

            if (written > 0) {
                bool checksum_ok = true;
                if (written == (int)chunk_size) {
                    uint8_t computed_sha[20];
                    if (util_buffer_sha_hash(chunk_buf, (size_t)written, computed_sha, 20) == 0) {
                        if (memcmp(computed_sha, file->chunks[c].checksum, 20) != 0) {
                            checksum_ok = false;
                            printf("[content_downloader] Chunk checksum mismatch for chunk %u/%u in depot=%u\n",
                                   c + 1, file->num_chunks, depot_id);
                        }
                    }
                }
                if (checksum_ok) {
                    fseek(f, file->chunks[c].offset, SEEK_SET);
                    fwrite(chunk_buf, 1, (size_t)written, f);
                    fflush(f);
                }
                file_downloaded += (size_t)written;
                downloaded_bytes += (size_t)written;
                num_chunks_downloaded++;
            } else {
                printf("[content_downloader] Failed to download chunk %u/%u for depot=%u\n",
                       c + 1, file->num_chunks, depot_id);
            }

            free(chunk_buf);
        }

        fclose(f);

        if (file->total_size > 0) {
            struct stat st;
            if (stat(file_path, &st) == 0 && (uint64_t)st.st_size != file->total_size) {
                printf("[content_downloader] Warning: file size mismatch for %s (expected %llu, got %lld)\n",
                       file_path, (unsigned long long)file->total_size, (long long)st.st_size);
            }
        }

        if (file->flags & SK_DEPOT_FILE_FLAG_EXECUTABLE) {
            util_set_executable(file_path, true);
        }

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
    bool verify_all, bool download_manifest_only, uint32_t max_downloads, hash_map_t* filelist) {
    uint8_t depot_key[32];
    if (steam3_session_get_depot_key(session, depot_id, app_id, depot_key, 32) != 0) {
        printf("[content_downloader] Failed to get depot key for depot=%u\n", depot_id);
        return -1;
    }

    uint64_t request_code = 0;
    if (manifest_id == DEPOT_DOWNLOADER_INVALID_MANIFEST_ID) {
        printf("[content_downloader] Manifest ID not specified for depot=%u\n", depot_id);
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
        verify_all, download_manifest_only, max_downloads, filelist);

    if (result == 0) {
        char config_path[1024];
        snprintf(config_path, sizeof(config_path), "%s/depotdownloader.config", install_dir ? install_dir : ".");
        depot_config_store_t* depot_store = depot_config_store_create();
        if (depot_store) {
            depot_config_store_load_from_file(depot_store, config_path);
            depot_config_store_set_manifest_id(depot_store, depot_id, manifest_id);
            depot_config_store_save(depot_store);
            depot_config_store_destroy(depot_store);
            printf("[content_downloader] Saved depot manifest state for depot=%u\n", depot_id);
        }
    }

    sk_depot_manifest_destroy(sk_manifest);
    return result;
}

int content_downloader_download_app(uint32_t app_id, const char *branch,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, bool download_manifest_only,
    bool using_file_list, const char *filelist,
    const char *access_token,
    uint32_t *depot_ids, size_t num_depot_ids, uint64_t *manifest_ids, size_t num_manifest_ids) {

    (void)download_all_platforms;
    (void)download_all_archs;
    (void)download_all_languages;
    (void)language;
    (void)low_violence;

    printf("[content_downloader] DownloadApp: app=%u branch=%s dir=%s\n", app_id, branch ? branch : "public", install_dir ? install_dir : ".");

    hash_map_t* filelist_map = NULL;
    if (using_file_list && filelist) {
        filelist_map = load_filelist(filelist);
        if (filelist_map) {
            printf("[content_downloader] Loaded filelist from %s\n", filelist);
        } else {
            printf("[content_downloader] Warning: failed to load filelist from %s\n", filelist);
        }
    }

    sk_steam3_session_t* session = steam3_session_create(NULL, NULL, app_id, 0);
    if (!session) {
        printf("[content_downloader] Failed to create steam session\n");
        hash_map_destroy(filelist_map, free);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    if (access_token) {
        steam3_session_set_access_token(session, access_token);
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

    sk_key_value_t* app_info = steam3_session_get_pics_app_info(session, app_id, 120000);
    if (!app_info) {
        printf("[content_downloader] Timeout waiting for PICS product info\n");
    }

    const char* effective_branch = branch ? branch : "public";

    uint32_t* depots_to_download = NULL;
    size_t num_depots_to_download = 0;

    if (num_depot_ids > 0) {
        depots_to_download = depot_ids;
        num_depots_to_download = num_depot_ids;
    } else if (app_info) {
        if (enumerate_depots_from_app_info(app_info, &depots_to_download, &num_depots_to_download) == 0) {
            printf("[content_downloader] Enumerated %zu depots from app info\n", num_depots_to_download);
        }
    }

    if (!depots_to_download || num_depots_to_download == 0) {
        printf("[content_downloader] No depots to download\n");
        if (depots_to_download && num_depot_ids == 0) free(depots_to_download);
        if (app_info) sk_key_value_destroy(app_info);
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_OK;
    }

    for (size_t i = 0; i < num_depots_to_download; ++i) {
        uint32_t depot_id = depots_to_download[i];

        if (!depot_matches_options(app_info, depot_id, download_all_platforms, download_all_archs, language, low_violence)) {
            printf("[content_downloader] Skipping depot=%u (filtered by platform/arch/language)\n", depot_id);
            continue;
        }

        uint64_t manifest_id = DEPOT_DOWNLOADER_INVALID_MANIFEST_ID;
        if (num_manifest_ids > 0 && i < num_manifest_ids) {
            manifest_id = manifest_ids[i];
        } else if (app_info) {
            manifest_id = resolve_manifest_id_for_depot(app_info, depot_id, effective_branch);
        }

        if (manifest_id == DEPOT_DOWNLOADER_INVALID_MANIFEST_ID) {
            printf("[content_downloader] No manifest ID for depot=%u, skipping\n", depot_id);
            continue;
        }

        printf("[content_downloader] Downloading depot=%u manifest=%llu\n",
               depot_id, (unsigned long long)manifest_id);

        int dep_result = content_downloader_download_depot(
            session, app_id, depot_id, manifest_id,
            effective_branch, install_dir ? install_dir : ".",
            verify_all, download_manifest_only, max_downloads, filelist_map);

        if (dep_result != 0) {
            printf("[content_downloader] Depot %u download failed with result=%d\n", depot_id, dep_result);
        }
    }

    if (depots_to_download && num_depot_ids == 0) free(depots_to_download);
    if (app_info) sk_key_value_destroy(app_info);
    if (filelist_map) hash_map_destroy(filelist_map, free);
    steam3_session_destroy(session);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_pubfile(uint32_t app_id, uint64_t published_file_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, const char *access_token) {

    printf("[content_downloader] download_pubfile app_id=%u pubfile_id=%llu\n",
           app_id, (unsigned long long)published_file_id);

    sk_steam3_session_t* session = steam3_session_create(NULL, NULL, app_id, 0);
    if (!session) {
        printf("[content_downloader] Failed to create steam session\n");
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    if (access_token) {
        steam3_session_set_access_token(session, access_token);
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

    sk_published_file_details_t* details = sk_steam_published_file_get_details(steam3_session_get_published_file(session), published_file_id);
    if (!details || details->result != 1) {
        printf("[content_downloader] Failed to get published file details for %llu\n",
               (unsigned long long)published_file_id);
        sk_published_file_details_destroy(details);
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    printf("[content_downloader] PublishedFile: title=%s url=%s size=%llu\n",
           details->title ? details->title : "(null)",
           details->file_url ? details->file_url : "(null)",
           (unsigned long long)details->file_size);

    if (details->file_url) {
        printf("[content_downloader] Downloading from URL: %s\n", details->file_url);
        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s/%s", install_dir ? install_dir : ".", details->title ? details->title : "pubfile");
        if (util_download_file(details->file_url, out_path) == 0) {
            printf("[content_downloader] Downloaded to %s\n", out_path);
        } else {
            printf("[content_downloader] Failed to download from URL\n");
        }
    } else {
        printf("[content_downloader] No file URL, need depot/chunk download\n");
    }

    sk_published_file_details_destroy(details);
    steam3_session_destroy(session);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_ugc(uint32_t app_id, uint64_t ugc_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, const char *access_token) {

    printf("[content_downloader] download_ugc app_id=%u ugc_id=%llu\n",
           app_id, (unsigned long long)ugc_id);

    sk_steam3_session_t* session = steam3_session_create(NULL, NULL, app_id, 0);
    if (!session) {
        printf("[content_downloader] Failed to create steam session\n");
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    if (access_token) {
        steam3_session_set_access_token(session, access_token);
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

    sk_ugc_details_callback_t* ugc_cb = sk_steam_cloud_request_ugc_details(steam3_session_get_cloud(session), ugc_id);
    if (!ugc_cb) {
        printf("[content_downloader] Failed to request UGC details for %llu\n",
               (unsigned long long)ugc_id);
        steam3_session_destroy(session);
        return DEPOT_DOWNLOADER_RESULT_ERROR;
    }

    printf("[content_downloader] UGC: file=%s url=%s size=%llu\n",
           ugc_cb->file_name ? ugc_cb->file_name : "(null)",
           ugc_cb->url ? ugc_cb->url : "(null)",
           (unsigned long long)ugc_cb->file_size);

    sk_ugc_details_callback_destroy(ugc_cb);
    steam3_session_destroy(session);
    return DEPOT_DOWNLOADER_RESULT_OK;
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
