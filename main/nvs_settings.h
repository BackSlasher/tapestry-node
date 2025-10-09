#ifndef NVS_SETTINGS_H
#define NVS_SETTINGS_H

#include <stdint.h>

#define MAX_SSID_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_SCREEN_MODEL_LEN 16

typedef struct {
    uint8_t wifi_ssid[MAX_SSID_LEN];
    uint8_t wifi_password[MAX_PASSWORD_LEN];
    char screen_model[MAX_SCREEN_MODEL_LEN];
} settings_t;

void settings_load(settings_t *settings);
void settings_save(const settings_t *settings);

#endif /* NVS_SETTINGS_H */