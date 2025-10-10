#!/bin/bash
ESP_VERSION='v5.3.2'
source esp-idf-helper $ESP_VERSION || exit 1

set -euo pipefail
idf.py app
