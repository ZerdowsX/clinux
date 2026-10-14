#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Boot the kernel straight into the Water Candy desktop in QEMU.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
RES=${RES:-1280x800}

KVM=
[ -w /dev/kvm ] && KVM="-enable-kvm -cpu host"

exec qemu-system-x86_64 $KVM \
	-m "${MEM:-1G}" -smp "${CPUS:-2}" \
	-kernel "$OUT/bzImage" \
	-initrd "$OUT/watercandy-initrd.gz" \
	-append "console=ttyS0 quiet loglevel=3 ${APPEND:-}" \
	-device virtio-vga,xres="${RES%x*}",yres="${RES#*x}" \
	-device virtio-keyboard-pci -device virtio-tablet-pci \
	-display "${DISPLAY_BACKEND:-gtk}" \
	-serial mon:stdio \
	${QEMU_EXTRA:-}
