#!/bin/sh
# Build the native Sub-GHz Record RAW preview harness and render previews.
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
U8G2="$ROOT/Drivers/u8g2_csrc"
BIN="$ROOT/scratch_preview"
OUT="$ROOT/out/recordraw_preview"

mkdir -p "$BIN" "$OUT"
cd "$ROOT"

# Compile the whole U8g2 library (u8g2_d_setup.c references every device
# driver, so we let the linker pull what it needs). mui*.c are excluded - they
# are the menu framework and are not used by the harness.
SRCS=""
for s in "$U8G2"/u8g2_*.c "$U8G2"/u8x8_*.c "$U8G2"/u8log*.c; do
  case "$s" in
    *mui*) continue ;;
  esac
  SRCS="$SRCS $s"
done
SRCS="$SRCS $ROOT/m1_csrc/m1_display_data.c"
SRCS="$SRCS $ROOT/m1_csrc/m1_subghz_recordraw_draw.c"
SRCS="$SRCS $ROOT/scripts/preview/recordraw_preview.c"

echo "Compiling preview harness..."
clang -O1 -std=c11 -Wall -Wextra -Wno-unused-parameter \
      -I"$U8G2" -I"$ROOT/m1_csrc" \
      $SRCS -o "$BIN/recordraw_preview"

echo "Rendering previews (cwd -> $OUT)..."
cd "$OUT"
mkdir -p out
"$BIN/recordraw_preview"
# Move flat outputs up and convert BMP -> PNG for viewing.
mv -f out/*.pbm out/*.bmp "$OUT"/ 2>/dev/null || true
rmdir out 2>/dev/null || true
if command -v sips >/dev/null 2>&1; then
  for b in "$OUT"/*.bmp; do
    [ -f "$b" ] || continue
    sips -s format png "$b" --out "${b%.bmp}.png" >/dev/null 2>&1 || true
  done
fi
echo "Done. Artifacts in $OUT"
ls -1 "$OUT"
