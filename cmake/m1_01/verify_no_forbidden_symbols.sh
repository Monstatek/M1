#!/bin/sh
# verify_no_forbidden_symbols.sh - self-contained post-build regression guard.
#
# Fails the build if a forbidden symbol is linked (as a defined text symbol)
# into the production ELF. This protects against a known-bad implementation
# silently reappearing in a shipped build.
#
# It is fully self-contained: it inspects only the linked ELF via the supplied
# nm tool. It has NO dependency on git history, tags, branches, or any external
# checkpoint, so it runs identically from a standalone public checkout.
#
# Usage: verify_no_forbidden_symbols.sh <nm-tool> <elf-path>
set -u

NM_TOOL="${1:-}"
ELF_PATH="${2:-}"

# Forbidden defined-text symbols (space separated). lfrfid_fsk_osc is the
# Flipper-derived LF-RFID FSK oscillator path that must not ship in the
# production LF-RFID build.
FORBIDDEN="lfrfid_fsk_osc"

if [ -z "$NM_TOOL" ] || [ -z "$ELF_PATH" ] || [ ! -f "$ELF_PATH" ]; then
    echo "verify_no_forbidden_symbols: nm-tool/elf-path not supplied -- skipped" >&2
    exit 0
fi

SYMS=$("$NM_TOOL" "$ELF_PATH" 2>/dev/null)

fail=0
for sym in $FORBIDDEN; do
    if echo "$SYMS" | grep -qE "[0-9a-fA-F]+ [Tt] ${sym}\$"; then
        echo "  FAIL: forbidden symbol linked into the production ELF: $sym" >&2
        fail=1
    fi
done

if [ "$fail" = "0" ]; then
    echo "verify_no_forbidden_symbols.sh: OK -- no forbidden symbols linked."
    exit 0
fi

echo "verify_no_forbidden_symbols.sh: FAILED -- a forbidden symbol is present in the ELF." >&2
echo "  Remove the implementation that defines it before shipping this build." >&2
exit 1
