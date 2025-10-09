#!/bin/bash
ESP_VERSION='v5.3.2'
source esp-idf-helper $ESP_VERSION || exit 1 2>/dev/null

set -euo pipefail

# Build only the app (no NVS generation needed for OTA)
exec idf.py app
