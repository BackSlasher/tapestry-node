#include "ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_app_format.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "OTA";

// Task to handle OTA finalization and reboot
static void ota_finalize_task(void* param) {
    const esp_partition_t* update_partition = (const esp_partition_t*)param;

    ESP_LOGI(TAG, "Finalizing OTA update...");

    // Set new partition as boot partition (pending validation)
    esp_err_t err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "OTA update staged! New firmware will be validated on next boot.");
    ESP_LOGW(TAG, "If validation fails, device will automatically rollback.");
    ESP_LOGI(TAG, "Rebooting in 2 seconds...");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();
}

esp_err_t http_ota(httpd_req_t* req) {
    ESP_LOGI(TAG, "OTA update request received");

    // Check for force header
    char force_header[32];
    bool force_update = false;
    if (httpd_req_get_hdr_value_str(req, "X-Force-Update", force_header, sizeof(force_header)) == ESP_OK) {
        force_update = (strcasecmp(force_header, "true") == 0 || strcmp(force_header, "1") == 0);
        if (force_update) {
            ESP_LOGW(TAG, "Force update requested, skipping version check");
        }
    }

    // Get content length
    size_t content_length = req->content_len;
    if (content_length == 0) {
        ESP_LOGE(TAG, "No content length provided");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Missing content length", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA firmware size: %zu bytes", content_length);

    // Get current app description for comparison
    const esp_app_desc_t* current_app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Current firmware version: %s", current_app_desc->version);
    ESP_LOGI(TAG, "Current compile time: %s %s", current_app_desc->date, current_app_desc->time);

    // Get update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA update partition found");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "No OTA partition available", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Using OTA partition: %s", update_partition->label);

    // Begin OTA update
    esp_ota_handle_t update_handle = 0;
    esp_err_t err = esp_ota_begin(update_partition, content_length, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to start OTA update", HTTPD_RESP_USE_STRLEN);
        return err;
    }

    // Buffer for receiving data
    char buffer[1024];
    size_t received_bytes = 0;
    bool first_chunk = true;
    esp_app_desc_t new_app_desc;

    // Receive and write firmware data
    while (received_bytes < content_length) {
        int bytes_to_read = (content_length - received_bytes > sizeof(buffer)) ?
                           sizeof(buffer) : (content_length - received_bytes);

        int bytes_received = httpd_req_recv(req, buffer, bytes_to_read);
        if (bytes_received <= 0) {
            ESP_LOGE(TAG, "Error receiving data");
            esp_ota_abort(update_handle);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "Error receiving firmware data", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        }

        // On first chunk, extract and compare app description
        if (first_chunk && bytes_received >= sizeof(esp_app_desc_t)) {
            memcpy(&new_app_desc, buffer + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));

            // Ensure strings are null-terminated for safety
            new_app_desc.version[sizeof(new_app_desc.version) - 1] = '\0';
            new_app_desc.date[sizeof(new_app_desc.date) - 1] = '\0';
            new_app_desc.time[sizeof(new_app_desc.time) - 1] = '\0';

            ESP_LOGI(TAG, "New firmware version: %s", new_app_desc.version);
            ESP_LOGI(TAG, "New compile time: %s %s", new_app_desc.date, new_app_desc.time);

            // Check if it's the same firmware (unless force update)
            if (!force_update &&
                strcmp(current_app_desc->version, new_app_desc.version) == 0 &&
                strcmp(current_app_desc->date, new_app_desc.date) == 0 &&
                strcmp(current_app_desc->time, new_app_desc.time) == 0) {
                ESP_LOGW(TAG, "Same firmware detected, aborting OTA");
                esp_ota_abort(update_handle);
                httpd_resp_set_status(req, "409 Conflict");
                httpd_resp_send(req, "Same firmware already installed", HTTPD_RESP_USE_STRLEN);
                return ESP_ERR_INVALID_ARG;
            }

            first_chunk = false;
        }

        // Write chunk to OTA partition
        err = esp_ota_write(update_handle, buffer, bytes_received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(update_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "Failed to write firmware", HTTPD_RESP_USE_STRLEN);
            return err;
        }

        received_bytes += bytes_received;
        ESP_LOGI(TAG, "OTA progress: %zu/%zu bytes", received_bytes, content_length);
    }

    // Validate received size
    if (received_bytes != content_length) {
        ESP_LOGE(TAG, "Size mismatch: received %zu, expected %zu", received_bytes, content_length);
        esp_ota_abort(update_handle);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, "Firmware size mismatch", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Finalize OTA update
    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "Failed to finalize OTA", HTTPD_RESP_USE_STRLEN);
        return err;
    }

    ESP_LOGI(TAG, "OTA write completed, starting finalization...");

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    const char* response = "{\"status\":\"success\",\"message\":\"OTA complete, rebooting in 2 seconds...\"}";
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    // Create task to finalize OTA and reboot (to avoid mutex issues)
    xTaskCreate(ota_finalize_task, "ota_finalize", 4096, (void*)update_partition, 5, NULL);

    return ESP_OK;
}

esp_err_t http_ota_info(httpd_req_t* req) {
    // Get current app description
    const esp_app_desc_t* app_desc = esp_app_get_description();

    // Get current running partition
    const esp_partition_t* running_partition = esp_ota_get_running_partition();

    // Get next update partition
    const esp_partition_t* update_partition = esp_ota_get_next_update_partition(NULL);

    // Check OTA state
    esp_ota_img_states_t ota_state;
    const char* ota_state_str = "unknown";
    if (esp_ota_get_state_partition(running_partition, &ota_state) == ESP_OK) {
        switch (ota_state) {
            case ESP_OTA_IMG_NEW:
                ota_state_str = "pending_validation";
                break;
            case ESP_OTA_IMG_PENDING_VERIFY:
                ota_state_str = "pending_verification";
                break;
            case ESP_OTA_IMG_VALID:
                ota_state_str = "validated";
                break;
            case ESP_OTA_IMG_INVALID:
                ota_state_str = "invalid";
                break;
            case ESP_OTA_IMG_ABORTED:
                ota_state_str = "aborted";
                break;
            default:
                ota_state_str = "undefined";
                break;
        }
    }

    // Convert SHA256 to hex string
    char sha256_hex[33] = {0}; // 32 hex chars + null terminator
    for (int i = 0; i < 16; i++) {
        sprintf(&sha256_hex[i * 2], "%02x", (unsigned char)app_desc->app_elf_sha256[i]);
    }

    // Build JSON response
    char response[768];
    snprintf(response, sizeof(response),
        "{"
        "\"current_version\":\"%s\","
        "\"compile_date\":\"%s\","
        "\"compile_time\":\"%s\","
        "\"project_name\":\"%s\","
        "\"idf_version\":\"%s\","
        "\"running_partition\":\"%s\","
        "\"next_partition\":\"%s\","
        "\"ota_state\":\"%s\","
        "\"rollback_enabled\":%s,"
        "\"app_elf_sha256\":\"%s\""
        "}",
        app_desc->version,
        app_desc->date,
        app_desc->time,
        app_desc->project_name,
        app_desc->idf_ver,
        running_partition ? running_partition->label : "unknown",
        update_partition ? update_partition->label : "none",
        ota_state_str,
        (ota_state == ESP_OTA_IMG_NEW || ota_state == ESP_OTA_IMG_PENDING_VERIFY) ? "true" : "false",
        sha256_hex
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}