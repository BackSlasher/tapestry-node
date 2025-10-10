#include "server.h"
#include "esp_http_server.h"
#include "epd.h"
#include "esp_heap_caps.h"
#include "nvs_settings.h"
#include "ota.h"
#include "cJSON.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

static settings_t g_settings;

static const EpdDisplay_t* get_display_from_model(const char* model) {
    if (strcmp(model, "ED060XC3") == 0) {
        return &ED060XC3;
    } else if (strcmp(model, "ED097TC2") == 0) {
        return &ED097TC2;
    } else {
        ESP_LOGE("main", "Unknown screen model: %s", model);
        abort();
    }
}

static esp_err_t http_index(httpd_req_t* req) {
    EpdData data = n_epd_data();
    char response[256];
    snprintf(response, sizeof(response), 
        "{"
        "\"width\":%d,"
        "\"height\":%d,"
        "\"temperature\":%d,"
        "\"screen_model\":\"%s\""
        "}",
        data.width, data.height, data.temperature, g_settings.screen_model);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_clear(httpd_req_t* req) {
    ESP_LOGI(__FUNCTION__, "Clear\n");
    n_epd_clear();
    const char* response = "Cleared\n";
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_draw(httpd_req_t* req) {
    // optional headers: x,y. Default to 0
    // required headers: height, width
    // Content should be a stream of special bytes - we're reading 4 bits at a time.
    int x, y, width, height, clear;
    char header[20];
    memset(header, 0, 20);
    if (httpd_req_get_hdr_value_str(req, "clear", header, 20) == ESP_OK) {
        sscanf(header, "%d", &clear);
    } else {
        clear = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "x", header, 20) == ESP_OK) {
        sscanf(header, "%d", &x);
    } else {
        x = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "y", header, 20) == ESP_OK) {
        sscanf(header, "%d", &y);
    } else {
        y = 0;
    }
    if (httpd_req_get_hdr_value_str(req, "width", header, 20) == ESP_OK) {
        sscanf(header, "%d", &width);
    } else {
        char response[60];
        sprintf(response, "Missing header width");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    if (httpd_req_get_hdr_value_str(req, "height", header, 20) == ESP_OK) {
        sscanf(header, "%d", &height);
    } else {
        char response[60];
        sprintf(response, "Missing header height");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_set_status(req, "400");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // READING STREAM
    int req_size = req->content_len;
    char* content = (char*)heap_caps_malloc(req_size, MALLOC_CAP_SPIRAM);
    if (content == NULL) {
        char msg[50];
        sprintf(msg, "Failed to allocate %d chars\n", req_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_ERR_INVALID_ARG;
    }
    int current_pos = 0;
    int amount_recieved;
    while ((amount_recieved = httpd_req_recv(req, (content + current_pos), req_size)) > 0) {
        ESP_LOGI(__FUNCTION__, "Read %d bytes\n", amount_recieved);
        current_pos += amount_recieved;
    }
    if (amount_recieved < 0) {
        char msg[50];
        heap_caps_free(content);
        ESP_LOGE(msg, "Failed to read byets. Error code %d\n", amount_recieved);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, msg);
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(__FUNCTION__, "Done reading %d bytes out of %d\n", current_pos, req_size);

    if (clear) {
        n_epd_clear();
    }
    n_epd_draw(((uint8_t*)content), x, y, width, height);
    heap_caps_free(content);

    // Done reading
    char response[100];
    sprintf(
        response, "x %d, y %d, width %d, height %d, byte count %d\n", x, y, width, height, req_size
    );
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t http_parameters_get(httpd_req_t* req) {
    cJSON *json = cJSON_CreateObject();
    cJSON *wifi_ssid = cJSON_CreateString((char*)g_settings.wifi_ssid);
    cJSON *wifi_password = cJSON_CreateString("***");  // Don't expose password
    cJSON *screen_model = cJSON_CreateString(g_settings.screen_model);

    cJSON_AddItemToObject(json, "wifi_ssid", wifi_ssid);
    cJSON_AddItemToObject(json, "wifi_password", wifi_password);
    cJSON_AddItemToObject(json, "screen_model", screen_model);

    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t http_parameters_post(httpd_req_t* req) {
    int req_size = req->content_len;
    if (req_size > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    char *content = malloc(req_size + 1);
    if (!content) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, req_size);
    if (ret <= 0) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to receive data");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (!json) {
        free(content);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    settings_t new_settings = g_settings;  // Copy current settings
    bool changed = false;

    cJSON *wifi_ssid = cJSON_GetObjectItem(json, "wifi_ssid");
    if (wifi_ssid && cJSON_IsString(wifi_ssid)) {
        const char *ssid_str = cJSON_GetStringValue(wifi_ssid);
        if (strlen(ssid_str) < MAX_SSID_LEN) {
            memset(new_settings.wifi_ssid, 0, MAX_SSID_LEN);
            strcpy((char*)new_settings.wifi_ssid, ssid_str);
            changed = true;
        }
    }

    cJSON *wifi_password = cJSON_GetObjectItem(json, "wifi_password");
    if (wifi_password && cJSON_IsString(wifi_password)) {
        const char *pass_str = cJSON_GetStringValue(wifi_password);
        if (strlen(pass_str) < MAX_PASSWORD_LEN) {
            memset(new_settings.wifi_password, 0, MAX_PASSWORD_LEN);
            strcpy((char*)new_settings.wifi_password, pass_str);
            changed = true;
        }
    }

    cJSON *screen_model = cJSON_GetObjectItem(json, "screen_model");
    if (screen_model && cJSON_IsString(screen_model)) {
        const char *model_str = cJSON_GetStringValue(screen_model);
        if (strlen(model_str) < MAX_SCREEN_MODEL_LEN) {
            if (strcmp(model_str, "ED060XC3") == 0 || strcmp(model_str, "ED097TC2") == 0) {
                memset(new_settings.screen_model, 0, MAX_SCREEN_MODEL_LEN);
                strcpy(new_settings.screen_model, model_str);
                changed = true;
            }
        }
    }

    if (changed) {
        settings_save(&new_settings);
        g_settings = new_settings;

        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "200");
        httpd_resp_send(req, "{\"status\":\"saved\",\"restart_required\":true}", HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "200");
        httpd_resp_send(req, "{\"status\":\"no_changes\"}", HTTPD_RESP_USE_STRLEN);
    }

    free(content);
    cJSON_Delete(json);
    return ESP_OK;
}

void register_paths(httpd_handle_t server) {
    {
        httpd_uri_t uri
            = { .uri = "/", .method = HTTP_GET, .handler = http_index, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/clear", .method = HTTP_POST, .handler = http_clear, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/draw", .method = HTTP_POST, .handler = http_draw, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/ota", .method = HTTP_POST, .handler = http_ota, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/ota", .method = HTTP_GET, .handler = http_ota_info, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/ota/parameters", .method = HTTP_GET, .handler = http_parameters_get, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
    {
        httpd_uri_t uri
            = { .uri = "/ota/parameters", .method = HTTP_POST, .handler = http_parameters_post, .user_ctx = NULL };
        httpd_register_uri_handler(server, &uri);
    }
}

void app_main(void) {
    static const char* TAG = "MAIN";

    settings_load(&g_settings);

    httpd_handle_t server = get_server(&g_settings);
    if (server != NULL) {
        register_paths(server);

        // Validate OTA update after successful network and HTTP server startup
        const esp_partition_t* running_partition = esp_ota_get_running_partition();
        esp_ota_img_states_t ota_state;

        if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
            if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_LOGI(TAG, "First boot after OTA update - validating firmware...");
                ESP_LOGI(TAG, "✓ WiFi connected successfully");
                ESP_LOGI(TAG, "✓ HTTP server started successfully");

                // Mark the new firmware as valid
                esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "✓ OTA validation successful - rollback cancelled");
                } else {
                    ESP_LOGE(TAG, "✗ Failed to validate OTA update: %s", esp_err_to_name(err));
                }
            } else if (ota_state == ESP_OTA_IMG_NEW) {
                ESP_LOGW(TAG, "Running new firmware - validation pending");
            } else if (ota_state == ESP_OTA_IMG_VALID) {
                ESP_LOGI(TAG, "Running validated firmware");
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to start server - OTA validation skipped");
    }

    const EpdDisplay_t* display = get_display_from_model(g_settings.screen_model);
    n_epd_setup(display);
}
