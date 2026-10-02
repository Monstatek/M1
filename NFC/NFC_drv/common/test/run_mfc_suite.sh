#!/bin/sh
# run_mfc_suite.sh -- build + run the MIFARE Classic host characterization
# suite against the REAL production sources (crypto1.c, ce_mfc.c,
# m1_mfc_session.c, mfc_key_source.c, mfc_dict_phase.c, m1_mfc_emu_image.c,
# mfc_harvest.c) plus the RAWOWN lifecycle/logic and persona-routing mirrors.
# These lock the AUTHORITATIVE Flow-A RAWOWN/Crypto1/session backend behavior:
# activation -> MFC AUTH -> sector reads. UID/ATQA/SAK-only presentation is
# NOT a pass here.
#
# Run from the repository root:  sh NFC/NFC_drv/common/test/run_mfc_suite.sh
# Requires a host C compiler; -fsanitize=address,undefined used where the
# upstream per-test recipe uses it.
#
# Exit 0 iff every suite passes with 0 failures.
set -u
ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
cd "$ROOT" || exit 2
CC="${CC:-cc}"
SAN="-std=c11 -Wall -Wextra -fsanitize=address,undefined"
C="NFC/NFC_drv/common"; T="$C/test"
TMP="$(mktemp -d)"; fail=0

run() { # name  flags  "src list"
  name="$1"; flags="$2"; src="$3"; bin="$TMP/$name"
  # shellcheck disable=SC2086
  if ! $CC $flags $src -o "$bin" >"$TMP/$name.log" 2>&1; then
    echo "  BUILD-FAIL $name"; grep -i error: "$TMP/$name.log" | head -2 | sed 's/^/    /'; fail=1; return
  fi
  out="$("$bin" 2>&1)"; rc=$?
  line="$(printf '%s\n' "$out" | grep -iE 'passed|PASS:|failed|FAIL:' | tail -1)"
  if [ $rc -eq 0 ] && ! printf '%s' "$line" | grep -qiE 'FAIL: [1-9]|[1-9][0-9]* failed'; then
    echo "  ok   $name :: $line"
  else
    echo "  FAIL $name :: ${line:-rc=$rc}"; fail=1
  fi
}

echo "== authoritative RAWOWN / Crypto1 / session backend =="
run ce_mfc_test           "$SAN -I $C" "$C/crypto1.c $C/ce_mfc.c $T/ce_mfc_test.c"
run crypto1_recover_test  "$SAN -I $C" "$C/crypto1.c $C/crypto1_recover.c $T/crypto1_recover_test.c"
run mfc_session_test      "$SAN -I $C" "$C/crypto1.c $C/ce_mfc.c $C/m1_mfc_session.c $T/mfc_session_test.c"
run mfc_rawown_lifecycle_test "$SAN -I $C" "$T/mfc_rawown_lifecycle_test.c"
run mfc_raw_hw_logic_test "$SAN -I $C" "$C/crypto1.c $C/ce_mfc.c $C/m1_mfc_session.c $T/mfc_raw_hw_logic_test.c"
run ce_dev_type_test      "$SAN -I $C" "$C/crypto1.c $C/ce_mfc.c $T/ce_dev_type_test.c"
run mfc_resolver_test     "$SAN -I $C" "$C/crypto1.c $C/ce_mfc.c $C/m1_mfc_session.c $T/mfc_resolver_test.c"

echo "== emulation image builder (real production m1_mfc_emu_image.c) =="
cp "$C/m1_mfc_emu_image.c" "$C/m1_mfc_emu_image.h" "$T/stub/" 2>/dev/null
run mfc_emu_image_test "$SAN -I $T/stub" "$T/stub/m1_mfc_emu_image.c $T/stub/nfc_ctx_stub.c $T/mfc_emu_image_test.c"
rm -f "$T/stub/m1_mfc_emu_image.c" "$T/stub/m1_mfc_emu_image.h"

echo "== dictionary / key source / harvest =="
run mfc_harvest_test "-std=c11 -Wall -Wextra -O2 -I $C" "$C/mfc_harvest.c $T/mfc_harvest_test.c"
( cd "$T" && $CC $SAN ../nfc_dict_line.c ../mfc_key_source.c mfc_key_source_test.c -I.. -o "$TMP/mks" >/dev/null 2>&1 \
    && "$TMP/mks" >"$TMP/mks.out" 2>&1 && grep -qiE '[1-9][0-9]* passed, 0 failed' "$TMP/mks.out" ) \
  && echo "  ok   mfc_key_source_test" || { echo "  FAIL mfc_key_source_test"; fail=1; }
( cd "$T" && $CC $SAN ../nfc_dict_line.c ../mfc_key_source.c ../mfc_result.c ../mfc_dict_resume.c ../mfc_dict_phase.c mfc_dict_phase_test.c -I.. -o "$TMP/mdp" >/dev/null 2>&1 \
    && "$TMP/mdp" >"$TMP/mdp.out" 2>&1 && grep -qiE '[1-9][0-9]* passed, 0 failed' "$TMP/mdp.out" ) \
  && echo "  ok   mfc_dict_phase_test" || { echo "  FAIL mfc_dict_phase_test"; fail=1; }

echo "== routing (persona / role) =="
run nfc_persona_routing_test "$SAN" "NFC/NFC_drv/legacy/test/nfc_persona_routing_test.c"

rm -rf "$TMP"
if [ "$fail" = "0" ]; then echo "MFC SUITE: ALL PASS"; exit 0; else echo "MFC SUITE: FAILURES"; exit 1; fi
