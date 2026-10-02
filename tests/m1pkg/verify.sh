#!/bin/sh
# See COPYING.txt for license details.
#
# Cross-check every committed .m1pkg fixture against the REAL firmware validator
# (m1cp_pkg_validate, H573 target) and compare to the expected result recorded in
# MANIFEST.json. Also re-verifies each fixture's SHA-256. Host validation only:
# this does NOT exercise FatFs, SD ownership, USB-MSC, or physical-device
# behaviour.
#
#   sh tests/m1pkg/verify.sh
#
# Exit 0 if all fixtures match expected result AND hash; non-zero otherwise.
set -e
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
fx="$here/fixtures"
manifest="$fx/MANIFEST.json"

# Build the validator harness (compiles the real m1_manager_fw.c).
make -C "$repo/tools/m1cp_host_test" validate_file >/dev/null
vf="$repo/tools/m1cp_host_test/validate_file"

pass=0; fail=0
names=$(node -e "require('$manifest').fixtures.forEach(f=>console.log(f.name))")
for name in $names; do
  exp=$(node -e "console.log(require('$manifest').fixtures.find(f=>f.name==='$name').expect)")
  shaExp=$(node -e "console.log(require('$manifest').fixtures.find(f=>f.name==='$name').sha256)")
  act=$("$vf" "$fx/$name" | awk '{print $2}')
  shaAct=$(shasum -a 256 "$fx/$name" | awk '{print $1}')
  if [ "$exp" = "$act" ] && [ "$shaExp" = "$shaAct" ]; then
    pass=$((pass+1)); printf '  ok   %-24s %s\n' "$name" "$act"
  else
    fail=$((fail+1)); printf '  FAIL %-24s expect=%s actual=%s hash=%s\n' "$name" "$exp" "$act" \
      "$( [ "$shaExp" = "$shaAct" ] && echo ok || echo MISMATCH )"
  fi
done
echo ""
echo "fixture cross-check: $pass matched, $fail mismatched"
[ "$fail" -eq 0 ]
