#!/bin/sh

set -eu

PLATFORM="linux/amd64"
IMAGE="${M1_BUILD_IMAGE:-monstatek-m1-build:arm-gnu-14.2-rel1-amd64}"
BASE_DIGEST="sha256:63a496b5d3b99214b39f5ed70eb71a61e590a77979c79cbee4faf991f8c0783e"
ARM_GNU_SHA256="62a63b981fe391a9cbad7ef51b17e49aeaa3e7b0d029b36ca1e9c3b2a9b78823"

ROOT="$(git rev-parse --show-toplevel)"
SOURCE_COMMIT="$(git -C "$ROOT" rev-parse HEAD)"
SOURCE_SHORT="$(git -C "$ROOT" rev-parse --short=12 HEAD)"
SOURCE_DATE_EPOCH="$(git -C "$ROOT" show -s --format=%ct "$SOURCE_COMMIT")"

DOCKERFILE_PATH="${ROOT}/docker/firmware/Dockerfile"
BUILD_SCRIPT_PATH="${ROOT}/scripts/reproducible/container-build.sh"

DOCKERFILE_SHA256="$(
    shasum -a 256 "$DOCKERFILE_PATH" |
        awk '{print $1}'
)"

BUILD_SCRIPT_SHA256="$(
    shasum -a 256 "$BUILD_SCRIPT_PATH" |
        awk '{print $1}'
)"

TEMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/m1-container-build.XXXXXX")"
SOURCE_DIR="${TEMP_ROOT}/source"
OUTPUT_DIR="${ROOT}/artifacts/container-linux-amd64"

cleanup()
{
    rm -rf "$TEMP_ROOT"
}

trap cleanup EXIT HUP INT TERM

mkdir -p "$SOURCE_DIR"

echo "=== CONTAINER BUILD INPUT ==="
echo "source_commit=$SOURCE_COMMIT"
echo "source_date_epoch=$SOURCE_DATE_EPOCH"
echo "platform=$PLATFORM"
echo "image=$IMAGE"
echo "base_digest=$BASE_DIGEST"
echo "arm_gnu_sha256=$ARM_GNU_SHA256"

echo
echo "=== EXPORT COMMITTED SOURCE ==="

git -C "$ROOT" archive --format=tar HEAD |
    tar -xf - -C "$SOURCE_DIR"

git -C "$SOURCE_DIR" init --quiet

echo "source_export=PASS"

echo
echo "=== BUILD TOOLCHAIN IMAGE ==="

docker build \
    --platform "$PLATFORM" \
    --provenance=false \
    --build-arg "VCS_REF=$SOURCE_COMMIT" \
    --tag "$IMAGE" \
    --file "$ROOT/docker/firmware/Dockerfile" \
    "$ROOT/docker/firmware"

echo "toolchain_image_build=PASS"

IMAGE_ID="$(
    docker image inspect "$IMAGE" --format '{{.Id}}'
)"

echo "image_id=$IMAGE_ID"

echo
echo "=== CONTAINER TOOL VERSIONS ==="

docker run \
    --rm \
    --platform "$PLATFORM" \
    --network none \
    "$IMAGE" \
    sh -c '
        arm-none-eabi-gcc --version | head -n 1
        arm-none-eabi-ld --version | head -n 1
        cmake --version | head -n 1
        printf "ninja "
        ninja --version
        srec_cat -VERSion | head -n 1
    '

echo
echo "=== CONTAINER FIRMWARE BUILD ==="

docker run \
    --rm \
    --platform "$PLATFORM" \
    --network none \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp/m1-home \
    --env "SOURCE_DATE_EPOCH=$SOURCE_DATE_EPOCH" \
    --volume "$SOURCE_DIR:/workspace" \
    --workdir /workspace \
    "$IMAGE" \
    sh -c '
        mkdir -p "$HOME"
        make clean
        make
    '

echo "container_firmware_build=PASS"

echo
echo "=== COLLECT CONTAINER ARTIFACTS ==="

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"

cp "$SOURCE_DIR"/artifacts/*.bin "$OUTPUT_DIR"/
cp "$SOURCE_DIR"/artifacts/*.elf "$OUTPUT_DIR"/
cp "$SOURCE_DIR"/artifacts/*.hex "$OUTPUT_DIR"/

{
    echo "source_commit=$SOURCE_COMMIT"
    echo "source_short=$SOURCE_SHORT"
    echo "source_date_epoch=$SOURCE_DATE_EPOCH"
    echo "platform=$PLATFORM"
    echo "image=$IMAGE"
    echo "image_id=$IMAGE_ID"
    echo "base_digest=$BASE_DIGEST"
    echo "arm_gnu_sha256=$ARM_GNU_SHA256"
    echo "dockerfile_sha256=$DOCKERFILE_SHA256"
    echo "build_wrapper_sha256=$BUILD_SCRIPT_SHA256"
    echo "build_type=Release"
} > "$OUTPUT_DIR/build-metadata.txt"

(
    cd "$OUTPUT_DIR"
    shasum -a 256 \
        MonstaTek_M1_v0800.bin \
        MonstaTek_M1_v0800_wCRC.bin \
        MonstaTek_M1_v0800.elf \
        MonstaTek_M1_v0800.hex \
        > SHA256SUMS
)

echo "artifact_collection=PASS"

echo
echo "=== CONTAINER ARTIFACT MANIFEST ==="

cat "$OUTPUT_DIR/SHA256SUMS"

echo
echo "=== CONTAINER ARTIFACT SIZES ==="

for file in \
    "$OUTPUT_DIR/MonstaTek_M1_v0800.bin" \
    "$OUTPUT_DIR/MonstaTek_M1_v0800_wCRC.bin" \
    "$OUTPUT_DIR/MonstaTek_M1_v0800.elf" \
    "$OUTPUT_DIR/MonstaTek_M1_v0800.hex"
do
    printf '%s size=%s\n' \
        "$(basename "$file")" \
        "$(stat -f '%z' "$file")"
done

echo
echo "output_directory=$OUTPUT_DIR"
echo "container_build=PASS"
