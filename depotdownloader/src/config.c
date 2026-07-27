#include "../config.h"
#include <stdlib.h>
#include <string.h>

download_config_t *download_config_create(void) {
    download_config_t *config = (download_config_t *)calloc(1, sizeof(download_config_t));
    if (!config) return NULL;
    config->app_id = DEPOT_DOWNLOADER_INVALID_APP_ID;
    config->cell_id = 0;
    config->max_downloads = 8;
    config->login_id = 0x534B32;
    config->low_violence = false;
    config->verify_all = false;
    config->remember_password = false;
    config->use_qr_code = false;
    config->skip_app_confirmation = false;
    config->download_all_platforms = false;
    config->download_all_archs = false;
    config->download_all_languages = false;
    config->download_manifest_only = false;
    config->debug = false;
    config->pubfile_id = 0;
    config->ugc_id = 0;
    config->depot_ids_capacity = 16;
    config->depot_ids = (uint32_t *)malloc(config->depot_ids_capacity * sizeof(uint32_t));
    config->manifest_ids_capacity = 16;
    config->manifest_ids = (uint64_t *)malloc(config->manifest_ids_capacity * sizeof(uint64_t));
    return config;
}

void download_config_destroy(download_config_t *config) {
    if (!config) return;
    free(config->install_directory);
    free(config->username);
    free(config->password);
    free(config->branch);
    free(config->beta_password);
    free(config->language);
    free(config->filelist);
    free(config->access_token);
    free(config->depot_ids);
    free(config->manifest_ids);
    free(config);
}
