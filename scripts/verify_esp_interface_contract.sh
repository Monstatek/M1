#!/bin/sh
# verify_esp_interface_contract.sh
#
# Verifies the canonical STM32<->ESP32 build configuration established for the
# HARDENING-002 reproducibility gate:
#
#   1. The STM32 firmware no longer depends on ESP-IDF's generated, git-ignored
#      sdkconfig.h: a tracked interface contract + a tracked minimal sdkconfig
#      stand-in replace it, and CMake no longer puts Esp_spi_at/build/include on
#      the include path.
#   2. A clean build succeeds with NO Esp_spi_at/build/ directory present
#      (i.e. as in a fresh clone).            [requires --build]
#   3. Two independent clean --release builds of the current commit are
#      byte-identical (deterministic via SOURCE_DATE_EPOCH).   [requires --build]
#
# Structural checks (1) always run and are fast. The build checks (2,3) run only
# with --build and need the pinned arm-none-eabi toolchain on PATH.
#
# Usage:  scripts/verify_esp_interface_contract.sh [--build]

set -e
cd "$(dirname "$0")/.."
REPO="$(pwd)"
FAIL=0
ok()   { echo "  ok   : $1"; }
bad()  { echo "  FAIL : $1"; FAIL=1; }

echo "== structural checks =="

# tracked files exist
[ -f m1_csrc/m1_esp_interface_contract.h ] && ok "interface contract header present" \
    || bad "m1_csrc/m1_esp_interface_contract.h missing"
[ -f m1_csrc/esp_stub/sdkconfig.h ] && ok "tracked sdkconfig stand-in present" \
    || bad "m1_csrc/esp_stub/sdkconfig.h missing"

# the interface contract carries the canonical values
for pair in "M1_ESP_SPI_MODE 1" "M1_ESP_SPI_TX_Q_SIZE 20" \
            "M1_ESP_SPI_RX_Q_SIZE 20" "M1_ESP_MMU_PAGE_SIZE 0x10000"; do
    name=${pair% *}; val=${pair#* }
    if grep -Eq "^#define[[:space:]]+$name[[:space:]]+$val" m1_csrc/m1_esp_interface_contract.h; then
        ok "contract defines $name = $val"
    else
        bad "contract missing/incorrect $name (expected $val)"
    fi
done

# m1_esp_hosted_config.h includes the contract, NOT sdkconfig.h
if grep -q '#include "m1_esp_interface_contract.h"' m1_csrc/m1_esp_hosted_config.h; then
    ok "m1_esp_hosted_config.h includes the interface contract"
else
    bad "m1_esp_hosted_config.h does not include the interface contract"
fi
if grep -Eq '^[[:space:]]*#include[[:space:]]+"sdkconfig.h"' m1_csrc/m1_esp_hosted_config.h; then
    bad "m1_esp_hosted_config.h still includes sdkconfig.h directly"
else
    ok "m1_esp_hosted_config.h no longer includes sdkconfig.h directly"
fi

# the stand-in forwards the contract (is not a full generated config)
if grep -q '#include "m1_esp_interface_contract.h"' m1_csrc/esp_stub/sdkconfig.h; then
    ok "sdkconfig stand-in forwards the interface contract"
else
    bad "sdkconfig stand-in does not forward the interface contract"
fi

# CMake no longer has an ACTIVE (non-comment) reference to the generated ESP
# build include dir. Comment lines (starting with '#') are ignored so the
# explanatory note that names the old path does not trip this check.
if grep -v '^[[:space:]]*#' cmake/m1_01/CMakeLists.txt | grep -q 'Esp_spi_at/build/include'; then
    bad "CMake still has an active reference to Esp_spi_at/build/include"
else
    ok "CMake include path has no active Esp_spi_at/build/include reference"
fi
if grep -q 'm1_csrc/esp_stub' cmake/m1_01/CMakeLists.txt; then
    ok "CMake include path uses the tracked m1_csrc/esp_stub"
else
    bad "CMake include path does not use m1_csrc/esp_stub"
fi

if [ "$1" != "--build" ]; then
    echo ""
    [ "$FAIL" = "0" ] && echo "STRUCTURAL CHECKS PASSED (run with --build for the reproducibility proof)" \
                      || echo "STRUCTURAL CHECKS FAILED"
    exit $FAIL
fi

echo ""
echo "== build checks =="

# (2) clean build with NO Esp_spi_at/build/ present
STASH=""
if [ -d Esp_spi_at/build ]; then
    STASH="$(mktemp -d)/esp_build"
    mv Esp_spi_at/build "$STASH"
fi
restore() { [ -n "$STASH" ] && [ -d "$STASH" ] && mv "$STASH" Esp_spi_at/build; }
trap restore EXIT

rm -rf build artifacts
if sh build.sh --release > /tmp/vic_build1.log 2>&1; then
    ok "clean --release build succeeds with NO Esp_spi_at/build/ directory"
else
    bad "clean build FAILED without Esp_spi_at/build/ (see /tmp/vic_build1.log)"
    tail -5 /tmp/vic_build1.log
    exit 1
fi
H1=$(md5 -q artifacts/*_wCRC.bin 2>/dev/null || md5sum artifacts/*_wCRC.bin | cut -d' ' -f1)
E1=$(md5 -q artifacts/*.elf 2>/dev/null || md5sum artifacts/*.elf | cut -d' ' -f1)

# (3) second independent clean build, still no Esp_spi_at/build/, must match
rm -rf build artifacts
if sh build.sh --release > /tmp/vic_build2.log 2>&1; then
    ok "second clean --release build succeeds"
else
    bad "second clean build FAILED"
    exit 1
fi
H2=$(md5 -q artifacts/*_wCRC.bin 2>/dev/null || md5sum artifacts/*_wCRC.bin | cut -d' ' -f1)
E2=$(md5 -q artifacts/*.elf 2>/dev/null || md5sum artifacts/*.elf | cut -d' ' -f1)

[ "$H1" = "$H2" ] && ok "two independent _wCRC.bin builds byte-identical ($H1)" \
                  || bad "_wCRC.bin differs between builds ($H1 vs $H2)"
[ "$E1" = "$E2" ] && ok "two independent .elf builds byte-identical ($E1)" \
                  || bad ".elf differs between builds ($E1 vs $E2)"

echo ""
[ "$FAIL" = "0" ] && echo "ALL CHECKS PASSED" || echo "CHECKS FAILED"
exit $FAIL
