#!/bin/bash
ESP_VERSION='v5.3.2'
source esp-idf-helper $ESP_VERSION || exit 1 2>/dev/null

set -euo pipefail

# Build using the OTA-only target (no NVS generation needed for OTA)
exec idf.py build-ota
