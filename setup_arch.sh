#!/bin/bash
set -e

echo "--- Preparing Arch Linux Build Environment ---"

DEPENDS=(cmake ninja git arm-none-eabi-gcc arm-none-eabi-newlib)

sudo pacman -S --needed --noconfirm "${DEPENDS[@]}"

if ! command -v srec_cat >/dev/null 2>&1; then
    echo "--- Installing srecord from AUR ---"
    if command -v yay >/dev/null 2>&1; then
        yay -S --noconfirm srecord
    elif command -v paru >/dev/null 2>&1; then
        paru -S --noconfirm srecord
    else
        cd /tmp
        git clone https://aur.archlinux.org/srecord.git
        cd srecord
        makepkg -si --noconfirm
        cd -
    fi
fi

echo "--- Dependency Setup complete. Rerun make  ---"