#include "server.h"
#include "nvs_settings.h"

static const char* TAG = "SERVER";
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;

httpd_handle_t get_server(const settings_t *settings);


static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 2000) {
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi disconnected, retrying in 30 seconds... (attempt %d)", s_retry_num);
        } else {
            ESP_LOGW(TAG, "WiFi disconnected, retrying in 30 seconds... (attempt many)");
        }
        vTaskDelay(30000 / portTICK_PERIOD_MS);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(const settings_t *settings) {
    s_wifi_event_group = xEventGroupCreate();

    // Initialize the ESP-NETIF
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default event loop
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize the Wi-Fi driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Set custom hostname based on MAC address
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "tapestry-%02x%02x%02x",
             mac[3], mac[4], mac[5]);
    ESP_ERROR_CHECK(esp_netif_set_hostname(sta_netif, hostname));
    ESP_LOGI(TAG, "Set hostname to: %s", hostname);

    // Register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip
    ));

    // Set Wi-Fi mode to station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Configure Wi-Fi connection using NVS settings
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // Now using WPA2 for better security
            .pmf_cfg = {
                .capable = true,   // WPA2 supports PMF
                .required = false
            },
        },
    };

    // Copy SSID and password from settings struct
    memcpy(wifi_config.sta.ssid, settings->wifi_ssid, sizeof(wifi_config.sta.ssid));
    memcpy(wifi_config.sta.password, settings->wifi_password, sizeof(wifi_config.sta.password));
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    ESP_LOGI(TAG, "Initializing WiFi with SSID: %s", (char*)settings->wifi_ssid);

    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

httpd_handle_t start_webserver(void) {
    ESP_LOGI(TAG, "Starting HTTP server...");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.stack_size = 8192; // Increase stack size for OTA operations

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return NULL;
    }
    return server;
}

httpd_handle_t get_server(const settings_t *settings) {
    wifi_init_sta(settings);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        return start_webserver();
    } else {
        ESP_LOGE(TAG, "Unexpected error waiting for WiFi connection");
        return NULL;
    }
}
