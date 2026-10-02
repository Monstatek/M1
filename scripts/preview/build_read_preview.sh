#!/bin/sh
# Build the native NFC/RFID read-screen preview harness and render previews.
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
U8G2="$ROOT/Drivers/u8g2_csrc"
BIN="$ROOT/scratch_preview"
OUT="$ROOT/out/read_icon_preview"

mkdir -p "$BIN" "$OUT"
cd "$ROOT"

SRCS=""
for s in "$U8G2"/u8g2_*.c "$U8G2"/u8x8_*.c "$U8G2"/u8log*.c; do
  case "$s" in *mui*) continue ;; esac
  SRCS="$SRCS $s"
done
SRCS="$SRCS $ROOT/m1_csrc/m1_read_icon.c $ROOT/scripts/preview/read_icon_preview.c"

echo "Compiling read-screen preview harness..."
clang -O1 -std=c11 -Wall -Wextra -Wno-unused-parameter \
      -I"$U8G2" -I"$ROOT/m1_csrc" \
      $SRCS -o "$BIN/read_icon_preview"

echo "Rendering..."
cd "$OUT"; mkdir -p out
"$BIN/read_icon_preview"
mv -f out/*.pbm out/*.bmp "$OUT"/ 2>/dev/null || true
rmdir out 2>/dev/null || true
if command -v sips >/dev/null 2>&1; then
  for b in "$OUT"/*.bmp; do [ -f "$b" ] || continue; sips -s format png "$b" --out "${b%.bmp}.png" >/dev/null 2>&1 || true; done
fi
echo "Done. Artifacts in $OUT"; ls -1 "$OUT"
