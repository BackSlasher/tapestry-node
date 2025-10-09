set(
    app_sources "epd.c" "server.c" "main.c" "nvs_settings.c" "ota.c"
)

idf_component_register(
        SRCS ${app_sources}
        REQUIRES epdiy esp_wifi nvs_flash esp_http_server esp_netif app_update json
)
