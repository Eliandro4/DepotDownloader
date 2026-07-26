#ifndef DEPOT_DOWNLOADER_CONFIG_H
#define DEPOT_DOWNLOADER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEPOT_DOWNLOADER_INVALID_APP_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_DEPOT_ID UINT32_MAX
#define DEPOT_DOWNLOADER_INVALID_MANIFEST_ID UINT64_MAX
#define DEPOT_DOWNLOADER_DEFAULT_BRANCH "public"

typedef struct {
    uint32_t app_id;
    int cell_id;
    bool download_all_platforms;
    bool download_all_archs;
    bool download_all_languages;
    bool download_manifest_only;
    char *install_directory;

    bool using_file_list;
    uint32_t *depot_ids;
    size_t num_depot_ids;
    size_t depot_ids_capacity;
    uint64_t *manifest_ids;
    size_t num_manifest_ids;
    size_t manifest_ids_capacity;

    char *beta_password;
    char *language;

    bool verify_all;
    int max_downloads;

    bool remember_password;
    uint32_t login_id;

    bool use_qr_code;
    bool skip_app_confirmation;
    bool low_violence;

    char *username;
    char *password;
    char *branch;
    bool debug;

    uint32_t pubfile_id;
    uint64_t ugc_id;
} download_config_t;

download_config_t *download_config_create(void);
void download_config_destroy(download_config_t *config);

#ifdef __cplusplus
}
#endif

#endif
