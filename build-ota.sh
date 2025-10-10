#!/bin/bash
ESP_VERSION='v5.3.2'
source esp-idf-helper $ESP_VERSION || exit 1

set -euo pipefail

# rebuild config file
if [ -f sdkconfig ]; then
    rm sdkconfig
fi
idf.py app
