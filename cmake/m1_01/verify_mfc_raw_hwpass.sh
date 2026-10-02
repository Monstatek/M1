#!/bin/sh
# See COPYING.txt for license details.
#
# verify_mfc_raw_hwpass.sh - post-build guard for the production MFC raw
# emulation path.
#
# Run automatically (POST_BUILD, see cmake/m1_01/CMakeLists.txt) ONLY when
# M1_MFC_RAW_EMULATION and M1_MFC_DMA_AT are both ON. Fails the build if the
# canonical RAWOWN listener/dispatcher or the production DMA {At} transmitter
# did not actually make it into the linked ELF -- symbol/map evidence, not
# log-string evidence, so a future refactor that silently drops or renames
# the real entry points is caught at build time instead of on hardware.
#
# Usage: verify_mfc_raw_hwpass.sh <nm-tool> <elf-path>

set -e

NM="$1"
ELF="$2"

if [ -z "$NM" ] || [ -z "$ELF" ]; then
    echo "verify_mfc_raw_hwpass.sh: usage: $0 <nm-tool> <elf-path>" >&2
    exit 2
fi

if [ ! -f "$ELF" ]; then
    echo "verify_mfc_raw_hwpass.sh: ELF not found: $ELF" >&2
    exit 2
fi

# Canonical RAWOWN listener/dispatcher entry points (m1_mfc_raw_listener.c,
# m1_mfc_raw_session_hw.c) plus the production DMA {At} transmitter
# (m1_mfc_dma.c). Every one of these must be a defined ('T'/'t') symbol in
# the final link -- a missing one means the guarded #if the flags control
# didn't actually reach the linker for that piece.
# Retargeted to the AUTHORITATIVE Flow-A RAWOWN/Crypto1/DMA/session backend
# (m1_mfc_raw_hw_run -> m1_mfc_session -> ce_mfc/crypto1, TX via
# m1_mfc_dma_tx_frame). The former list required listener-frame Flow-B symbols
# (m1_mfc_raw_service_frame, m1_mfc_pauth_dispatched, m1_mfc_dma_tx_at) that
# were the duplicate second backend deleted in the single-implementation
# refactor -- requiring them here would force the dead backend back into the
# link. See cmake/m1_01/mfc_authoritative_manifest.txt (verify_mfc_arch.sh
# additionally forbids those Flow-B symbols from reappearing).
REQUIRED_SYMS="
m1_mfc_raw_begin
m1_mfc_raw_hw_run
m1_mfc_raw_hw_session_end
m1_mfc_session_take_auth
m1_mfc_session_on_read
ce_mfc_auth1
crypto1_word
m1_mfc_dma_init
m1_mfc_dma_tx_frame
"

SYMTAB=$("$NM" "$ELF" 2>/dev/null)
missing=""

for sym in $REQUIRED_SYMS; do
    if ! echo "$SYMTAB" | grep -qE "[0-9a-fA-F]+ [Tt] ${sym}\$"; then
        missing="$missing $sym"
    fi
done

if [ -n "$missing" ]; then
    echo "verify_mfc_raw_hwpass.sh: FAILED -- production MFC raw emulation" >&2
    echo "build is missing required symbols in $ELF:" >&2
    for sym in $missing; do
        echo "  - $sym" >&2
    done
    echo "M1_MFC_RAW_EMULATION/M1_MFC_DMA_AT were requested but the linked" >&2
    echo "binary does not actually contain the canonical RAWOWN listener" >&2
    echo "and/or DMA {At} transmitter. Do not ship this binary." >&2
    exit 1
fi

echo "verify_mfc_raw_hwpass.sh: OK -- all $(echo "$REQUIRED_SYMS" | wc -w | tr -d ' ') canonical MFC raw-emulation symbols present in $(basename "$ELF")."
exit 0
