#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"

#include "network.h"
#include "network_mngr.h"
#include "storage.h"
#include "types.h"
#include "port_cfg.h"

#define STORAGE_NAMESPACE   "nvs"
static const char *TAG = "storage";

bool is_espressif_target(const char *cfg_file) {
    const char *prefix = "esp32";
    size_t prefix_length = strlen(prefix);

    if (strlen(cfg_file) < prefix_length) return false;
    if (strncmp(cfg_file, prefix, prefix_length) == 0) return true;

    return false;
}

esp_err_t storage_init_filesystem(void) {
    ESP_LOGI(TAG, "Mounting FAT filesystem");
    // To mount device we need name of device partition, define base_path
    // and allow format partition in case if it is new one and was not formatted before
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 5,
        .format_if_mount_failed = true
    };

    static wl_handle_t wl_handle;
    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl("/data", NULL, &mount_config, &wl_handle);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find FATFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize FATFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    uint64_t total = 0, used = 0;
    ret = esp_vfs_fat_info("/data", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get FATFS partition information (%s)", esp_err_to_name(ret));
        return ret;
    } else {
        ESP_LOGI(TAG, "Partition size: total: %" PRId64 ", used: %" PRId64, total, used);
    }
    return ESP_OK;
}

esp_err_t storage_init_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return err;
    }
    // --- Network Params ---
    if (!storage_is_key_exist(WIFI_SSID_KEY)) {
        const char *wifi_ssid = CONFIG_ESP_WIFI_SSID;
        nvs_set_blob(handle, WIFI_SSID_KEY, wifi_ssid, strlen(wifi_ssid));
    }

    if (!storage_is_key_exist(WIFI_PASS_KEY)) {
        const char *wifi_pass = CONFIG_ESP_WIFI_PASSWORD;
        nvs_set_blob(handle, WIFI_PASS_KEY, wifi_pass, strlen(wifi_pass));
    }

    // --- Port Configurations ---
    if (!storage_is_key_exist(PORT_A_CFG_KEY)) {
        const char *pa_cfg_val = "0"; // PA_LOGICANALYZER is 0
        nvs_set_blob(handle, PORT_A_CFG_KEY, pa_cfg_val, strlen(pa_cfg_val) + 1);
    }
    
    if (!storage_is_key_exist(PORT_B_CFG_KEY)) {
        const char *pb_cfg_val = "0"; // PB_LOGICANALYZER is 0
        nvs_set_blob(handle, PORT_B_CFG_KEY, pb_cfg_val, strlen(pb_cfg_val) + 1);
    }

    if (!storage_is_key_exist(PORT_C_CFG_KEY)) {
        const char *pc_cfg_val = "0"; // PC_LOGICANALYZER is 0
        nvs_set_blob(handle, PORT_C_CFG_KEY, pc_cfg_val, strlen(pc_cfg_val) + 1);
    }

    if (!storage_is_key_exist(PORT_D_CFG_KEY)) {
        const char *pd_cfg_val = "0"; // PD_LOGICANALYZER is 0
        nvs_set_blob(handle, PORT_D_CFG_KEY, pd_cfg_val, strlen(pd_cfg_val) + 1);
    }

    bool is_sta_configured = false;
    storage_get_bool_key("STA_MODE_CONFIGURED", &is_sta_configured);
    
    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t storage_write(const char *key, const char *value, size_t len) {
    if (!key) {
        ESP_LOGE(TAG, "Key is NULL!");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;

    esp_err_t esp_err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs namespace! (%s)", esp_err_to_name(esp_err));
        return esp_err;
    }

    esp_err = nvs_set_blob(my_handle, key, value, len);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob! (%s)-(%s)", key, esp_err_to_name(esp_err));
        nvs_close(my_handle);
        return esp_err;
    }

    esp_err = nvs_commit(my_handle);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed save changes! (%s)", esp_err_to_name(esp_err));
    }
    nvs_close(my_handle);

    return esp_err;
}

esp_err_t storage_read(const char *key, char *value, size_t len) {
    if (!key) {
        ESP_LOGE(TAG, "Key is NULL!");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;

    esp_err_t esp_err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &my_handle);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs namespace! (%s)", esp_err_to_name(esp_err));
        return esp_err;
    }

    size_t n_bytes = len;

    esp_err = nvs_get_blob(my_handle, key, value, &n_bytes);
    if (esp_err != ESP_OK) {
        if (esp_err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Key (%s) not found during read", key);
        } else {
            ESP_LOGE(TAG, "Failed to get blob (%s)-(%s)", key, esp_err_to_name(esp_err));
        }
    }

    nvs_close(my_handle);

    if (esp_err == ESP_OK && n_bytes != len) {
        ESP_LOGE(TAG, "Expected len (%d) actual len (%d)", len, n_bytes);
        return ESP_FAIL;
    }

    return esp_err;
}

esp_err_t storage_erase_key(const char *key)
{
    if (!key) {
        ESP_LOGE(TAG, "Key is NULL!");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t my_handle;

    esp_err_t esp_err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open nvs namespace! (%s)", esp_err_to_name(esp_err));
        return esp_err;
    }

    esp_err = nvs_erase_key(my_handle, key);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase (%s)-(%s)", key, esp_err_to_name(esp_err));
    }

    nvs_commit(my_handle);
    nvs_close(my_handle);

    return esp_err;
}

size_t storage_get_value_length(const char *key)
{
    if (!key) {
        ESP_LOGE(TAG, "Key is NULL");
        return 0;
    }

    nvs_handle_t nvs;

    esp_err_t esp_err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs);
    if (esp_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open namespace err:(%s)", esp_err_to_name(esp_err));
        return 0;
    }

    size_t length = 0;

    esp_err = nvs_get_blob(nvs, key, NULL, &length);
    if (esp_err != ESP_OK) {
        if (esp_err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGD(TAG, "Key (%s) not found in NVS", key);
        } else {
            ESP_LOGE(TAG, "Failed to get blob (%s)-(%s)", key, esp_err_to_name(esp_err));
        }
    }

    nvs_close(nvs);

    return length;
}

bool storage_is_key_exist(const char *key)
{
    return storage_get_value_length(key) > 0;
}

esp_err_t storage_erase_all(void)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }

    ESP_LOGI(TAG, "Namespace '%s' was %s erased", STORAGE_NAMESPACE, (err == ESP_OK) ? "" : "not");
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t storage_alloc_and_read(const char *key, char **value)
{
    if (!key) {
        return ESP_ERR_INVALID_ARG;
    }

    /* First read length of the config string. */
    size_t len = storage_get_value_length(key);
    if (len == 0) {
        return ESP_FAIL;
    }

    char *ptr = (char *)calloc(1, len + 1);
    if (!ptr) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_FAIL;
    }

    esp_err_t esp_err = storage_read(key, ptr, len);
    if (esp_err != ESP_OK) {
        free(ptr);
        return esp_err;
    }

    ESP_LOGI(TAG, "Read val:%s len:%zu", ptr, len);

    *value = ptr;

    return ESP_OK;
}

esp_err_t storage_update_target_struct(void)
{
    /* first, remove old records */
    if (g_app_params.target_list) {
        for (size_t i = 0; i < g_app_params.target_count; ++i) {
            free((void *)g_app_params.target_list[i]);
        }
        free(g_app_params.target_list);
        g_app_params.target_list = NULL;
    }

    DIR *d = opendir(CFG_FILE_PATH);
    if (!d) {
        ESP_LOGE(TAG, "Could not open the directory");
        return ESP_FAIL;
    }

#define TARGET_LIST_INITIAL_SIZE  20
#define TARGET_LIST_MAX_SIZE      256

    struct dirent *dir;
    size_t target_list_size = TARGET_LIST_INITIAL_SIZE;

    g_app_params.target_list = calloc(target_list_size, sizeof(*g_app_params.target_list));
    if (!g_app_params.target_list) {
        ESP_LOGE(TAG, "Could not allocate memory for the target list (%zu)", target_list_size);
        return ESP_FAIL;
    }

    int target_count = 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_type == DT_DIR) {
            continue;
        }
        g_app_params.target_list[target_count++] = strdup(dir->d_name);
        g_app_params.target_count = target_count;
        if (target_count >= (int)target_list_size) {
            if (target_list_size >= TARGET_LIST_MAX_SIZE) {
                ESP_LOGW(TAG, "Target list reached max size (%d), truncating", TARGET_LIST_MAX_SIZE);
                break;
            }
            target_list_size *= 2;
            if (target_list_size > TARGET_LIST_MAX_SIZE) {
                target_list_size = TARGET_LIST_MAX_SIZE;
            }
            const char **new_target_list = realloc(g_app_params.target_list, sizeof(*g_app_params.target_list) * target_list_size);
            if (!new_target_list) {
                ESP_LOGE(TAG, "Could not allocate memory for the target list (%zu)", target_list_size);
                return ESP_FAIL;
            }
            g_app_params.target_list = new_target_list;
        }
    }

    closedir(d);

    for (size_t i = 0; i < g_app_params.target_count; ++i) {
        if (!strcmp(g_app_params.target_list[i], g_app_params.config_file)) {
            g_app_params.selected_target_index = i;
            break;
        }
    }


    return ESP_OK;
}

/* Statically allocated; g_app_params.rtos_list points into this array. */
static const char *s_rtos_names[] = {
    "FreeRTOS", "nuttx", "Zephyr", "hwthread",
    "ThreadX", "eCos", "linux", "chibios", "Chromium-EC",
    "embKernel", "mqx", "uCOS-III", "rtkernel", "none"
};

esp_err_t storage_update_rtos_struct(void) {
    g_app_params.rtos_list = s_rtos_names;
    g_app_params.rtos_count = sizeof(s_rtos_names) / sizeof(*s_rtos_names);

    for (size_t i = 0; i < g_app_params.rtos_count; ++i) {
        if (!strcmp(g_app_params.rtos_list[i], g_app_params.rtos_type)) {
            g_app_params.selected_rtos_index = i;
            break;
        }
    }


    return ESP_OK;
}

void storage_set_bool_key(const char *key, bool value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle for setting bool: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "Setting NVS bool key '%s' to %s", key, value ? "true" : "false");
    esp_err_t set_err = nvs_set_u8(nvs_handle, key, (uint8_t)value);
    if (set_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set bool key '%s': %s", key, esp_err_to_name(set_err));
    }
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void storage_get_bool_key(const char *key, bool *value) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle for getting bool: %s", esp_err_to_name(err));
        *value = false; // Default to false if NVS cannot be opened
        return;
    }
    uint8_t temp_val = 0; // Default to 0 (false)
    esp_err_t get_err = nvs_get_u8(nvs_handle, key, &temp_val);
    if (get_err == ESP_OK) {
        *value = (bool)temp_val;
        ESP_LOGI(TAG, "Read NVS bool key '%s': %s", key, *value ? "true" : "false");
    } else if (get_err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS bool key '%s' not found, defaulting to false", key);
        *value = false;
    } else {
        ESP_LOGE(TAG, "Failed to get bool key '%s': %s", key, esp_err_to_name(get_err));
        *value = false; // Default to false on other errors
    }
    nvs_close(nvs_handle);
}
esp_err_t storage_open_session(storage_handle_t *handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        *handle = (storage_handle_t)nvs_handle;
    }
    return err;
}

esp_err_t storage_write_session(storage_handle_t handle, const char *key, const char *value, size_t len) {
    if (!key) return ESP_ERR_INVALID_ARG;
    nvs_handle_t nvs_handle = (nvs_handle_t)handle;

    esp_err_t err = nvs_set_blob(nvs_handle, key, value, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob! (%s)-(%s)", key, esp_err_to_name(err));
    }
    return err;
}

void storage_close_session(storage_handle_t handle) {
    nvs_handle_t nvs_handle = (nvs_handle_t)handle;
    esp_err_t err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed save changes! (%s)", esp_err_to_name(err));
    }
    nvs_close(nvs_handle);
}
