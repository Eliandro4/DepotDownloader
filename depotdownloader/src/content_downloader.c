#include "../content_downloader.h"
#include "../config.h"
#include "../persistence.h"
#include "../proto_manifest.h"
#include "hash_map.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <steamkit/steamkit.h>

static pthread_mutex_t g_download_mutex = PTHREAD_MUTEX_INITIALIZER;
static hash_map_t *g_active_downloads = NULL;

static uint32_t g_download_index = 0;

download_job_t *download_job_create(uint32_t app_id, uint32_t depot_id, uint64_t manifest_id) {
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

int content_downloader_download_app(uint32_t app_id, const char *branch,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, bool download_manifest_only,
    uint32_t *depot_ids, size_t num_depot_ids, uint64_t *manifest_ids, size_t num_manifest_ids) {

    (void)app_id; (void)branch; (void)install_dir; (void)download_all_platforms;
    (void)download_all_archs; (void)download_all_languages; (void)language;
    (void)low_violence; (void)verify_all; (void)max_downloads;
    (void)download_manifest_only; (void)depot_ids; (void)num_depot_ids;
    (void)manifest_ids; (void)num_manifest_ids;

    printf("[content_downloader] download_app app_id=%u\n", app_id);
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

    printf("[content_downloader] download_pubfile app_id=%u pubfile_id=%llu\n",
           app_id, (unsigned long long)published_file_id);
    return DEPOT_DOWNLOADER_RESULT_OK;
}

int content_downloader_download_ugc(uint32_t app_id, uint64_t ugc_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads) {

    (void)app_id; (void)ugc_id; (void)install_dir;
    (void)download_all_platforms; (void)download_all_archs;
    (void)download_all_languages; (void)language; (void)low_violence;
    (void)verify_all; (void)max_downloads;

    printf("[content_downloader] download_ugc app_id=%u ugc_id=%llu\n",
           app_id, (unsigned long long)ugc_id);
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

