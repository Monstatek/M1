#!/bin/sh
set -e

echo "Updating submodules..."
git submodule update --init --recursive

BUILD_DIR="build"
OUTPUT_DIR="artifacts"
mkdir -p $BUILD_DIR $OUTPUT_DIR

echo "Configuring CMake..."
cmake -G Ninja -B $BUILD_DIR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo "Compiling..."
cmake --build $BUILD_DIR --parallel $(nproc)

echo "Collecting artifacts to ./$OUTPUT_DIR"
cp $BUILD_DIR/*.bin $BUILD_DIR/*.elf $BUILD_DIR/*.hex $OUTPUT_DIR/ 2>/dev/null || true

ELF_FILE=$(ls $OUTPUT_DIR/*.elf 2>/dev/null | head -1)

if [ -z "$ELF_FILE" ]; then
    echo "Error: No ELF file found in $OUTPUT_DIR"
    exit 1
fi

ARCH=$(arm-none-eabi-objdump -f "$ELF_FILE" 2>/dev/null | grep "architecture" | head -1)
if echo "$ARCH" | grep -q "armv8-m\|armv7-m"; then
    ELF_NAME=$(basename "$ELF_FILE")
    echo "Build verification: Valid ARM firmware ($ELF_NAME)"
    arm-none-eabi-size "$ELF_FILE"
else
    echo "Error: Unexpected architecture in ELF"
    exit 1
fi

echo "Success! Firmware is in $(pwd)/$OUTPUT_DIR"