#!/bin/bash
ESP_VERSION='v5.3.2'
source esp-idf-helper $ESP_VERSION || exit 1

set -euo pipefail

WIFI_SSID="digink"
WIFI_PASSWORD=$(sudo nmcli -s -g 802-11-wireless-security.psk connection show "$WIFI_SSID")
SCREEN_MODEL="$1"

# Validate CSV safety
validate_csv_value() {
    local value="$1"
    local field_name="$2"

    if [[ "$value" == *","* ]]; then
        echo "Error: $field_name contains comma" >&2
        exit 1
    fi

    if [[ "$value" == *"\""* ]]; then
        echo "Error: $field_name contains quote" >&2
        exit 1
    fi

    if [[ "$value" == *$'\n'* ]]; then
        echo "Error: $field_name contains newline" >&2
        exit 1
    fi

    if [[ "$value" == *$'\r'* ]]; then
        echo "Error: $field_name contains carriage return" >&2
        exit 1
    fi
}

validate_csv_value "$WIFI_SSID" "WiFi SSID"
validate_csv_value "$WIFI_PASSWORD" "WiFi password"
validate_csv_value "$SCREEN_MODEL" "Screen model"

NVS_CSV="nvs_config.csv"

cat > "$NVS_CSV" << EOF
key,type,encoding,value
config,namespace,,
wifi_ssid,data,string,$WIFI_SSID
wifi_password,data,string,$WIFI_PASSWORD
screen_model,data,string,$SCREEN_MODEL
EOF

idf.py reconfigure

# Build the project (this will now generate NVS partition automatically)
echo "Building project with NVS generation..."
idf.py build

echo "Flashing application and NVS..."
idf.py flash
