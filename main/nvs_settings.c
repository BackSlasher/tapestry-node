#include "nvs_settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#define SETTINGS_NAMESPACE "config"

static const char* TAG = "NVS_SETTINGS";

void settings_load(settings_t *settings) {
    ESP_ERROR_CHECK(nvs_flash_init());

    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &nvs_handle));

    size_t required_size;

    required_size = MAX_SSID_LEN;
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_ssid", (char*)settings->wifi_ssid, &required_size));

    required_size = MAX_PASSWORD_LEN;
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "wifi_password", (char*)settings->wifi_password, &required_size));

    required_size = MAX_SCREEN_MODEL_LEN;
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, "screen_model", settings->screen_model, &required_size));

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Settings loaded: SSID=%s, Screen=%s", (char*)settings->wifi_ssid, settings->screen_model);
}

void settings_save(const settings_t *settings) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &nvs_handle));

    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "wifi_ssid", (char*)settings->wifi_ssid));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "wifi_password", (char*)settings->wifi_password));
    ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "screen_model", settings->screen_model));

    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "Settings saved: SSID=%s, Screen=%s", (char*)settings->wifi_ssid, settings->screen_model);
}
