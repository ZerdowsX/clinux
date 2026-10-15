#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build an x86_64 kernel and its modules for the desktop system.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
KOUT=$OUT/kernel

mkdir -p "$KOUT"
make -C "$SRC" O="$KOUT" x86_64_defconfig kvm_guest.config x11_desktop.config
make -C "$SRC" O="$KOUT" -j"$(nproc)" bzImage modules

# Modules (Wi-Fi drivers) go into a staging tree that build-rootfs.sh copies.
rm -rf "$OUT/modroot"
make -C "$SRC" O="$KOUT" INSTALL_MOD_PATH="$OUT/modroot" INSTALL_MOD_STRIP=1 \
	modules_install
cp "$KOUT/arch/x86/boot/bzImage" "$OUT/bzImage"
echo "Kernel: $OUT/bzImage, modules: $OUT/modroot"
