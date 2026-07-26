#ifndef DEPOT_DOWNLOADER_CONTENT_DOWNLOADER_H
#define DEPOT_DOWNLOADER_CONTENT_DOWNLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "steamkit/steamkit.h"

#define DEPOT_DOWNLOADER_INVALID_APP_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_DEPOT_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_MANIFEST_ID UINT64_MAX
#define DEPOT_DOWNLOADER_DEFAULT_BRANCH "public"

typedef enum {
    DEPOT_DOWNLOADER_RESULT_OK = 0,
    DEPOT_DOWNLOADER_RESULT_ERROR = 1,
    DEPOT_DOWNLOADER_RESULT_CANCELLED = 2,
    DEPOT_DOWNLOADER_RESULT_NOT_FOUND = 3,
} depot_downloader_result_t;

int content_downloader_download_app(uint32_t app_id, const char *branch,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads, bool download_manifest_only,
    uint32_t *depot_ids, size_t num_depot_ids, uint64_t *manifest_ids, size_t num_manifest_ids);

int content_downloader_download_pubfile(uint32_t app_id, uint64_t published_file_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads);

int content_downloader_download_ugc(uint32_t app_id, uint64_t ugc_id,
    const char *install_dir, bool download_all_platforms, bool download_all_archs,
    bool download_all_languages, const char *language, bool low_violence,
    bool verify_all, uint32_t max_downloads);

bool content_downloader_init(void);
void content_downloader_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
