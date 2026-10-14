#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build an x86_64 kernel with everything Xorg + XFCE need.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
KOUT=$OUT/kernel

mkdir -p "$KOUT"
make -C "$SRC" O="$KOUT" x86_64_defconfig kvm_guest.config x11_desktop.config
make -C "$SRC" O="$KOUT" -j"$(nproc)" bzImage

cp "$KOUT/arch/x86/boot/bzImage" "$OUT/bzImage"
echo "Kernel: $OUT/bzImage"
