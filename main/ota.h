#pragma once

#include "esp_http_server.h"

esp_err_t http_ota(httpd_req_t* req);
esp_err_t http_ota_info(httpd_req_t* req);