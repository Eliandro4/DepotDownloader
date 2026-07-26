#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "steamkit/steamkit.h"
#include "../config.h"
#include "../persistence.h"
#include "../content_downloader.h"
#include "util.h"

static download_config_t *g_config = NULL;

static void print_usage(const char *prog) {
    printf("Usage: %s -app <id> [-depot <id> [-manifest <id>]]\n", prog);
    printf("       [-username <user> [-password <pass>]]\n");
    printf("       [-branch <branchname>] [-branchpassword <pass>]\n");
    printf("       [-all-platforms] [-all-archs] [-all-languages]\n");
    printf("       [-language <lang>] [-lowviolence]\n");
    printf("       [-ugc <#>] [-pubfile <#>]\n");
    printf("       [-dir <installdir>] [-filelist <file.txt>]\n");
    printf("       [-validate] [-manifest-only]\n");
    printf("       [-cellid <#>] [-max-downloads <#>]\n");
    printf("       [-use-lancache] [-loginid <#>]\n");
    printf("       [-remember-password] [-qr] [-no-mobile]\n");
    printf("       [-debug] [-V|--version]\n");
}

static int add_depot_id(download_config_t *config, uint32_t depot_id) {
    if (config->num_depot_ids >= config->depot_ids_capacity) {
        config->depot_ids_capacity *= 2;
        uint32_t *new_arr = (uint32_t *)realloc(config->depot_ids, config->depot_ids_capacity * sizeof(uint32_t));
        if (!new_arr) return -1;
        config->depot_ids = new_arr;
    }
    config->depot_ids[config->num_depot_ids++] = depot_id;
    return 0;
}

static int add_manifest_id(download_config_t *config, uint64_t manifest_id) {
    if (config->num_manifest_ids >= config->manifest_ids_capacity) {
        config->manifest_ids_capacity *= 2;
        uint64_t *new_arr = (uint64_t *)realloc(config->manifest_ids, config->manifest_ids_capacity * sizeof(uint64_t));
        if (!new_arr) return -1;
        config->manifest_ids = new_arr;
    }
    config->manifest_ids[config->num_manifest_ids++] = manifest_id;
    return 0;
}

static bool parse_args(int argc, char **argv, download_config_t *config) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-app") == 0 && i + 1 < argc) {
            config->app_id = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-depot") == 0 && i + 1 < argc) {
            uint32_t depot = (uint32_t)strtoul(argv[++i], NULL, 10);
            add_depot_id(config, depot);
        } else if (strcmp(argv[i], "-manifest") == 0 && i + 1 < argc) {
            uint64_t manifest = strtoull(argv[++i], NULL, 10);
            add_manifest_id(config, manifest);
        } else if (strcmp(argv[i], "-username") == 0 && i + 1 < argc) {
            config->username = argv[++i];
        } else if (strcmp(argv[i], "-password") == 0 && i + 1 < argc) {
            config->password = argv[++i];
        } else if (strcmp(argv[i], "-branch") == 0 && i + 1 < argc) {
            config->branch = argv[++i];
        } else if (strcmp(argv[i], "-branchpassword") == 0 && i + 1 < argc) {
            config->beta_password = argv[++i];
        } else if (strcmp(argv[i], "-all-platforms") == 0) {
            config->download_all_platforms = true;
        } else if (strcmp(argv[i], "-all-archs") == 0) {
            config->download_all_archs = true;
        } else if (strcmp(argv[i], "-all-languages") == 0) {
            config->download_all_languages = true;
        } else if (strcmp(argv[i], "-language") == 0 && i + 1 < argc) {
            config->language = argv[++i];
        } else if (strcmp(argv[i], "-lowviolence") == 0) {
            config->low_violence = true;
        } else if (strcmp(argv[i], "-ugc") == 0 && i + 1 < argc) {
            config->ugc_id = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-pubfile") == 0 && i + 1 < argc) {
            config->pubfile_id = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-dir") == 0 && i + 1 < argc) {
            config->install_directory = argv[++i];
        } else if (strcmp(argv[i], "-debug") == 0) {
            config->debug = true;
        } else if (strcmp(argv[i], "-remember-password") == 0) {
            config->remember_password = true;
        } else if (strcmp(argv[i], "-qr") == 0) {
            config->use_qr_code = true;
        } else if (strcmp(argv[i], "-no-mobile") == 0) {
            config->skip_app_confirmation = true;
        } else if (strcmp(argv[i], "-cellid") == 0 && i + 1 < argc) {
            config->cell_id = (int)strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-max-downloads") == 0 && i + 1 < argc) {
            config->max_downloads = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-loginid") == 0 && i + 1 < argc) {
            config->login_id = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-validate") == 0) {
            config->verify_all = true;
        } else if (strcmp(argv[i], "-manifest-only") == 0) {
            config->download_manifest_only = true;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("Depot Downloader (C port) - Development Build\n");
            exit(0);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }
    return config->app_id != DEPOT_DOWNLOADER_INVALID_APP_ID;
}

int main(int argc, char **argv) {
    download_config_t *config = download_config_create();
    if (!config) {
        fprintf(stderr, "Failed to create config\n");
        return 1;
    }

    if (argc < 2) {
        print_usage(argv[0]);
        download_config_destroy(config);
        return 0;
    }

    if (!parse_args(argc, argv, config)) {
        fprintf(stderr, "Error: -app not specified\n");
        download_config_destroy(config);
        return 1;
    }

    if (!content_downloader_init()) {
        fprintf(stderr, "Failed to initialize downloader\n");
        download_config_destroy(config);
        return 1;
    }

    printf("Depot Downloader (C port) - App %u\n", config->app_id);

    int result = DEPOT_DOWNLOADER_RESULT_OK;

    if (config->pubfile_id != 0) {
        result = content_downloader_download_pubfile(
            config->app_id, config->pubfile_id,
            config->install_directory,
            config->download_all_platforms, config->download_all_archs,
            config->download_all_languages, config->language,
            config->low_violence, config->verify_all, config->max_downloads);
    } else if (config->ugc_id != 0) {
        result = content_downloader_download_ugc(
            config->app_id, config->ugc_id,
            config->install_directory,
            config->download_all_platforms, config->download_all_archs,
            config->download_all_languages, config->language,
            config->low_violence, config->verify_all, config->max_downloads);
    } else {
        result = content_downloader_download_app(
            config->app_id, config->branch,
            config->install_directory,
            config->download_all_platforms, config->download_all_archs,
            config->download_all_languages, config->language,
            config->low_violence, config->verify_all, config->max_downloads,
            config->download_manifest_only,
            config->depot_ids, config->num_depot_ids,
            config->manifest_ids, config->num_manifest_ids);
    }

    content_downloader_shutdown();
    download_config_destroy(config);
    return result == DEPOT_DOWNLOADER_RESULT_OK ? 0 : 1;
}
