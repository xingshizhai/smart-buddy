#!/usr/bin/env bash
set -euo pipefail

IDF_EXPORT="$HOME/.espressif/release-v6.0/esp-idf/export.sh"
if [[ ! -f "$IDF_EXPORT" ]]; then
  echo "ESP-IDF export script not found: $IDF_EXPORT" >&2
  exit 1
fi

source "$IDF_EXPORT"
exec idf.py menuconfig
