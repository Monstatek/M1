#!/bin/sh
# Build and run the Record RAW graph-direction regression test (A-F).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
U8G2="$ROOT/Drivers/u8g2_csrc"
BIN="$ROOT/scratch_preview"
OUT="$ROOT/out/dir_test"
mkdir -p "$BIN" "$OUT/out"
cd "$ROOT"

SRCS=""
for s in "$U8G2"/u8g2_*.c "$U8G2"/u8x8_*.c "$U8G2"/u8log*.c; do
  case "$s" in *mui*) continue ;; esac
  SRCS="$SRCS $s"
done
SRCS="$SRCS $ROOT/m1_csrc/m1_display_data.c $ROOT/m1_csrc/m1_subghz_recordraw_draw.c $ROOT/scripts/preview/recordraw_dir_test.c"

clang -O1 -std=c11 -Wall -Wextra -Wno-unused-parameter -I"$U8G2" -I"$ROOT/m1_csrc" \
      $SRCS -o "$BIN/dir_test"

cd "$OUT"
"$BIN/dir_test"; rc=$?
mv -f out/*.bmp "$OUT"/ 2>/dev/null || true
rmdir out 2>/dev/null || true
if command -v sips >/dev/null 2>&1; then
  for b in "$OUT"/*.bmp; do [ -f "$b" ] || continue; sips -s format png "$b" --out "${b%.bmp}.png" >/dev/null 2>&1 || true; done
fi
exit $rc
