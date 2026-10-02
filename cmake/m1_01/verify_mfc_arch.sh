#!/bin/sh
# verify_mfc_arch.sh -- POST_BUILD structural guard enforcing the ONE
# authoritative MIFARE Classic implementation. Consumes the maintained
# inventory cmake/m1_01/mfc_authoritative_manifest.txt and checks, together:
#
#   REQUIRED_SYM    -> must be a defined text symbol in the linked ELF
#   FORBIDDEN_SYM   -> must NOT be a defined text symbol (obsolete generic-MFC
#                      or the deleted duplicate Flow-B crypto backend)
#   REQUIRED_SRC    -> invariant that must remain present in production source
#   FORBIDDEN_SRC   -> extended-regex that must NOT match any production
#                      .c/.h outside */test/* (a match, even in a comment,
#                      fails -- this also forbids legacy documentation)
#   FORBIDDEN_CMAKE -> extended-regex that must NOT match active (non-#)
#                      cmake configuration (no option selecting a legacy backend)
#
# A single grep is deliberately NOT sufficient: this combines source,
# configuration, and compiled-symbol/map evidence. Build fails on any breach.
#
# Usage: verify_mfc_arch.sh <nm-tool> <elf-path> <src-root>
set -u
NM="$1"; ELF="$2"; ROOT="$3"
MAN="$ROOT/cmake/m1_01/mfc_authoritative_manifest.txt"
[ -x "$NM" ] || command -v "$NM" >/dev/null 2>&1 || { echo "verify_mfc_arch: nm '$NM' not found" >&2; exit 2; }
[ -f "$ELF" ] || { echo "verify_mfc_arch: ELF '$ELF' not found" >&2; exit 2; }
[ -f "$MAN" ] || { echo "verify_mfc_arch: manifest '$MAN' not found" >&2; exit 2; }

SYMS=$("$NM" "$ELF" 2>/dev/null)
# production source files (exclude tests and the u8g2/vendor font blobs)
SRC=$(find "$ROOT/NFC" "$ROOT/m1_csrc" -name '*.c' -o -name '*.h' 2>/dev/null | grep -v '/test/')
CMK="$ROOT/cmake/m1_01/CMakeLists.txt $ROOT/CMakeLists.txt"
fail=0

while read -r kind arg; do
    case "$kind" in
      REQUIRED_SYM)
        echo "$SYMS" | grep -qE "[0-9a-fA-F]+ [Tt] ${arg}\$" || { echo "  FAIL REQUIRED_SYM missing: $arg" >&2; fail=1; } ;;
      FORBIDDEN_SYM)
        if echo "$SYMS" | grep -qE "[0-9a-fA-F]+ [Tt] ${arg}\$"; then echo "  FAIL FORBIDDEN_SYM present: $arg" >&2; fail=1; fi ;;
      REQUIRED_SRC)
        hit=$(grep -rEl "$arg" $SRC 2>/dev/null)
        if [ -z "$hit" ]; then echo "  FAIL REQUIRED_SRC missing: $arg" >&2; fail=1; fi ;;
      FORBIDDEN_SRC)
        hit=$(grep -rEl "$arg" $SRC 2>/dev/null)
        if [ -n "$hit" ]; then echo "  FAIL FORBIDDEN_SRC matched [$arg] in:" >&2; echo "$hit" | sed 's/^/      /' >&2; fail=1; fi ;;
      FORBIDDEN_CMAKE)
        # active (non-comment) cmake lines only
        if grep -hvE '^[[:space:]]*#' $CMK 2>/dev/null | grep -qE "$arg"; then echo "  FAIL FORBIDDEN_CMAKE matched: $arg" >&2; fail=1; fi ;;
      ''|\#*) : ;;
    esac
done < "$MAN"

# There must be exactly one UI-side post of the dedicated event. More than one
# recreates divergent live/saved routes; zero removes the working backend.
MFC_POSTS=$(grep -hE 'm1_app_send_q_message\([^,]+,[[:space:]]*Q_EVENT_NFC_MFC_EMULATE\)|\.q_evt_type[[:space:]]*=[[:space:]]*Q_EVENT_NFC_MFC_EMULATE[[:space:]]*;' "$ROOT/m1_csrc/m1_nfc.c" 2>/dev/null | wc -l | tr -d ' ')
if [ "$MFC_POSTS" != "1" ]; then
    echo "  FAIL dedicated MFC event post count: expected 1, got $MFC_POSTS" >&2
    fail=1
fi

if [ "$fail" = "0" ]; then
    echo "verify_mfc_arch.sh: OK -- single authoritative MFC implementation enforced (manifest clean)."
    exit 0
fi
echo "verify_mfc_arch.sh: FAILED -- an obsolete/duplicate MFC route or symbol reappeared (see above)." >&2
exit 1
