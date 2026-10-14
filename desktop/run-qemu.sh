#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Boot the kernel and root filesystem in QEMU with a graphical window.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}

KVM=
[ -w /dev/kvm ] && KVM="-enable-kvm -cpu host"

exec qemu-system-x86_64 $KVM \
	-m "${MEM:-2G}" -smp "${CPUS:-2}" \
	-kernel "$OUT/bzImage" \
	-append "root=/dev/vda rw console=tty0 console=ttyS0" \
	-drive file="$OUT/rootfs.ext4",format=raw,if=virtio \
	-device virtio-vga -display "${DISPLAY_BACKEND:-gtk}" \
	-device virtio-keyboard-pci -device virtio-tablet-pci \
	-nic user,model=virtio-net-pci \
	-serial mon:stdio
