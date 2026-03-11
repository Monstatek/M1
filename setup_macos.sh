#!/bin/bash
set -e

echo "--- Preparing MacOS Build Environment ---"

DEPENDS=(cmake ninja git arm-none-eabi-gcc gcc-arm-embedded srecord)

if command -v brew >/dev/null 2>&1; then
    echo "--- Installing dependencies from brew ---"
    brew install ${DEPENDS[@]}
fi

echo "--- Dependency Setup complete. Rerun make  ---"
