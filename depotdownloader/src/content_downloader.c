#include "../content_downloader.h"
#include "persistence.h"
#include "proto_manifest.h"
#include "hash_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

static sk_steam3_session_t *g_session = NULL;
static download_config_t *g_config = NULL;

bool content_downloader_init(void) {
    if (g_session) return true;
    g_session = steam3_session_create(&(steam3_logon_details_t){0});
    return g_session != NULL;
}

void content_downloader_shutdown(void) {
    if (g_session) {
        steam3_session_destroy(g_session);
        g_session = NULL;
    }
    if (g_config) {
        download_config_destroy(g_config);
        g_config = NULL;
    }
}

int content_downloader_download_app(uint32_t app_id, const char *branch,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, bool download_manifest_only,
    uint32_t *depot_ids, size_t num_depot_ids, uint64_t *manifest_ids, size_t num_manifest_ids) {
    (void)app_id;
    (void)branch;
    (void)install_dir;
    (void)download_all_platforms;
    (void)download_all_archs;
    (void)download_all_languages;
    (void)language;
    (void)low_violence;
    (void)verify_all;
    (void)max_downloads;
    (void)download_manifest_only;
    (void)depot_ids;
    (void)num_depot_ids;
    (void)manifest_ids;
    (void)num_manifest_ids;
    printf("Downloading app %u (C port - work in progress)\n", app_id);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_pubfile(uint32_t app_id, uint64_t published_file_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads) {
    (void)app_id;
    (void)published_file_id;
    (void)install_dir;
    (void)download_all_platforms;
    (void)download_all_archs;
    (void)download_all_languages;
    (void)language;
    (void)low_violence;
    (void)verify_all;
    (void)max_downloads;
    printf("Downloading pubfile %llu for app %u (C port - work in progress)\n",
           (unsigned long long)published_file_id, app_id);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_ugc(uint32_t app_id, uint64_t ugc_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads) {
    (void)app_id;
    (void)ugc_id;
    (void)install_dir;
    (void)download_all_platforms;
    (void)download_all_archs;
    (void)download_all_languages;
    (void)language;
    (void)low_violence;
    (void)verify_all;
    (void)max_downloads;
    printf("Downloading UGC %llu for app %u (C port - work in progress)\n",
           (unsigned long long)ugc_id, app_id);
    return DEPOT_DOWNLOADER_RESULT_OK;
}
