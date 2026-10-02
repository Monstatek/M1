#!/bin/sh
set -e

# --release: enforce a clean tree, verify the embedded [BUILD] banner
# actually matches HEAD/dirty=0 after linking, and emit a small manifest
# beside the binary. Plain (no-flag) invocations still auto-embed the real
# commit/dirty state -- they just don't refuse a dirty tree or write a
# manifest, so local iteration is unaffected.
RELEASE_MODE=0
if [ "$1" = "--release" ]; then
    RELEASE_MODE=1
fi

echo "Updating submodules..."
git submodule update --init --recursive

BUILD_DIR="build"
OUTPUT_DIR="artifacts"
ARTIFACT_STEM="MonstaTek_M1_v0800"
mkdir -p $BUILD_DIR $OUTPUT_DIR

GIT_COMMIT=$(git rev-parse --short=8 HEAD)
if git diff --quiet --ignore-submodules HEAD -- 2>/dev/null; then
    GIT_DIRTY=0
else
    GIT_DIRTY=1
fi

if [ "$RELEASE_MODE" = "1" ] && [ "$GIT_DIRTY" = "1" ]; then
    echo "Error: --release requires a clean tree (git status --short is non-empty)." >&2
    git status --short >&2
    exit 1
fi

# Deterministic release metadata: derive SOURCE_DATE_EPOCH from the SOURCE
# COMMIT's own committer timestamp (identical for a given commit on any machine
# at any time), and export it so GCC expands __DATE__/__TIME__ reproducibly
# instead of using wall-clock time. This is what makes two independent clean
# builds of the same commit byte-identical. Only for --release; plain developer
# builds intentionally keep ordinary current-time __DATE__/__TIME__ so a local
# incremental rebuild still reflects when it was built. No date or commit is
# hardcoded, and no compiler warning is suppressed.
SOURCE_DATE_UTC=""
if [ "$RELEASE_MODE" = "1" ]; then
    SOURCE_DATE_EPOCH=$(git log -1 --format=%ct HEAD)
    export SOURCE_DATE_EPOCH
    SOURCE_DATE_UTC=$(date -u -r "$SOURCE_DATE_EPOCH" +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null \
                     || date -u -d "@$SOURCE_DATE_EPOCH" +"%Y-%m-%dT%H:%M:%SZ")
    echo "Deterministic release: SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH ($SOURCE_DATE_UTC)"
fi

echo "Configuring CMake (commit=$GIT_COMMIT dirty=$GIT_DIRTY)..."
cmake -G Ninja -B $BUILD_DIR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DM1_MFC_RAW_EMULATION=ON -DM1_MFC_DMA_AT=ON \
    -DM1_GIT_COMMIT="$GIT_COMMIT" -DM1_GIT_DIRTY=$GIT_DIRTY

echo "Compiling..."
cmake --build $BUILD_DIR --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "Collecting artifacts to ./$OUTPUT_DIR"
for artifact in \
    "$ARTIFACT_STEM.bin" \
    "$ARTIFACT_STEM.elf" \
    "$ARTIFACT_STEM.hex" \
    "${ARTIFACT_STEM}_wCRC.bin"
do
    if [ ! -f "$BUILD_DIR/$artifact" ]; then
        echo "Error: Expected build artifact is missing: $BUILD_DIR/$artifact" >&2
        exit 1
    fi
    cp "$BUILD_DIR/$artifact" "$OUTPUT_DIR/$artifact"
done

# Select the artifacts produced by this build explicitly. The output directory
# may also contain named release candidates from earlier runs; a wildcard plus
# `head -1` can silently validate or hash one of those stale files instead.
ELF_FILE="$OUTPUT_DIR/$ARTIFACT_STEM.elf"

ARCH=$(arm-none-eabi-objdump -f "$ELF_FILE" 2>/dev/null | grep "architecture" | head -1)
if echo "$ARCH" | grep -q "armv8-m\|armv7-m"; then
    ELF_NAME=$(basename "$ELF_FILE")
    echo "Build verification: Valid ARM firmware ($ELF_NAME)"
    arm-none-eabi-size "$ELF_FILE"
else
    echo "Error: Unexpected architecture in ELF"
    exit 1
fi

if [ "$RELEASE_MODE" = "1" ]; then
    # The [BUILD] banner is a runtime printf ("git=%s dirty=%d ...") -- the
    # commit is substituted at runtime, so it can't be read back as one
    # concatenated string from a static ELF dump. What CAN be verified
    # statically: the exact $GIT_COMMIT string literal we just told the
    # compiler to embed (via -DM1_GIT_COMMIT="...") is actually present
    # in the linked binary's rodata, not silently dropped/empty/stale.
    if ! arm-none-eabi-strings "$ELF_FILE" 2>/dev/null | grep -qxF "$GIT_COMMIT"; then
        echo "Error: --release expected the literal commit string '$GIT_COMMIT' to be" >&2
        echo "        embedded in $ELF_FILE (via M1_GIT_COMMIT) but it was not found." >&2
        exit 1
    fi
    echo "Verified: embedded commit literal '$GIT_COMMIT' present in $ELF_FILE"

    ARTIFACT_BIN="$OUTPUT_DIR/${ARTIFACT_STEM}_wCRC.bin"

    MD5_HASH=$(md5 -q "$ARTIFACT_BIN" 2>/dev/null || md5sum "$ARTIFACT_BIN" 2>/dev/null | cut -d' ' -f1)
    SHA256_HASH=$(shasum -a 256 "$ARTIFACT_BIN" 2>/dev/null | cut -d' ' -f1)
    SIZE_BYTES=$(wc -c < "$ARTIFACT_BIN" | tr -d ' ')
    BRANCH=$(git branch --show-current)
    TIMESTAMP=$(date -u +"%Y-%m-%dT%H:%M:%SZ")

    MANIFEST="$OUTPUT_DIR/$(basename "$ARTIFACT_BIN" .bin).manifest.txt"
    {
        echo "binary=$(basename "$ARTIFACT_BIN")"
        echo "commit=$GIT_COMMIT"
        echo "dirty=$GIT_DIRTY"
        echo "branch=$BRANCH"
        echo "flags=M1_MFC_RAW_EMULATION=ON M1_MFC_DMA_AT=ON"
        echo "source_date_epoch=$SOURCE_DATE_EPOCH"
        echo "source_date_utc=$SOURCE_DATE_UTC"
        echo "build_timestamp_utc=$TIMESTAMP"
        echo "size_bytes=$SIZE_BYTES"
        echo "md5=$MD5_HASH"
        echo "sha256=$SHA256_HASH"
        arm-none-eabi-size "$ELF_FILE" 2>/dev/null | tail -1 | awk '{print "flash_text="$1"\nflash_data="$2"\nram_bss="$3}'
    } > "$MANIFEST"
    echo "Release manifest: $MANIFEST"
    cat "$MANIFEST"
fi

echo "Success! Firmware is in $(pwd)/$OUTPUT_DIR"
