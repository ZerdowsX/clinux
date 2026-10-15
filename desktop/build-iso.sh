#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build a bootable live ISO (BIOS and UEFI) from the kernel and the root
# filesystem made by build-kernel.sh and build-rootfs.sh. Write it to a USB
# stick with dd; the desktop then offers "Install Water Candy" to put the
# system on the computer's disk.
#
# Needs: grub-mkrescue (grub-pc-bin, grub-efi-amd64-bin), xorriso, mtools,
# squashfs-tools, a static busybox, cpio and gzip. Run as root (it mounts
# the root filesystem image).
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
ISO=${ISO:-$OUT/clinux-watercandy.iso}
BUSYBOX=${BUSYBOX:-$(command -v busybox || true)}
WORK=$OUT/iso
MNT=$OUT/iso-mnt

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root (sudo $0)" >&2
	exit 1
fi
for f in "$OUT/bzImage" "$OUT/rootfs.ext4"; do
	[ -f "$f" ] || { echo "Missing $f: run build-kernel.sh and build-rootfs.sh" >&2; exit 1; }
done
if [ -z "$BUSYBOX" ] || ! file -L "$BUSYBOX" | grep -q "statically linked"; then
	echo "Need a statically linked busybox (apt install busybox-static)" >&2
	exit 1
fi

rm -rf "$WORK"
mkdir -p "$WORK/boot/grub" "$WORK/live" "$MNT"
trap 'umount "$MNT" 2>/dev/null || true' EXIT

echo "Compressing the root filesystem (this takes a while)..."
mount -o loop,ro "$OUT/rootfs.ext4" "$MNT"
mksquashfs "$MNT" "$WORK/live/filesystem.squashfs" -comp zstd -noappend \
	-e lost+found -quiet
umount "$MNT"

# A tiny initramfs that finds the medium and starts the system from it.
IRD=$OUT/iso-initrd
rm -rf "$IRD"
mkdir -p "$IRD/bin" "$IRD/proc" "$IRD/sys" "$IRD/dev" "$IRD/live" "$IRD/newroot"
cp "$BUSYBOX" "$IRD/bin/busybox"
cat > "$IRD/init" <<'INIT'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /live/medium /live/squash /live/rw

echo "Water Candy: looking for the live system..."
found=
for try in $(seq 1 40); do
	for d in /dev/sr* /dev/sd* /dev/vd* /dev/nvme*n* /dev/mmcblk*; do
		[ -b "$d" ] || continue
		mount -o ro "$d" /live/medium 2>/dev/null || continue
		if [ -f /live/medium/live/filesystem.squashfs ]; then
			found=$d
			break 2
		fi
		umount /live/medium
	done
	sleep 1
done
if [ -z "$found" ]; then
	echo "The live system was not found. Dropping to a shell."
	exec sh
fi
echo "Found it on $found."

mount -t squashfs -o loop,ro /live/medium/live/filesystem.squashfs /live/squash
mount -t tmpfs -o size=75% tmpfs /live/rw
mkdir -p /live/rw/upper /live/rw/work
mount -t overlay overlay \
	-o lowerdir=/live/squash,upperdir=/live/rw/upper,workdir=/live/rw/work /newroot

mkdir -p /newroot/live/medium /newroot/live/squash /newroot/live/rw
mount --move /live/medium /newroot/live/medium
mount --move /live/squash /newroot/live/squash
mount --move /live/rw /newroot/live/rw
touch /newroot/etc/clinux-live
echo "overlay / overlay rw 0 0" > /newroot/etc/fstab

umount /proc /sys
mount --move /dev /newroot/dev
exec switch_root /newroot /sbin/init
INIT
chmod +x "$IRD/init"
(cd "$IRD" && find . | cpio -o -H newc --quiet | gzip -9) > "$WORK/boot/initrd.img"
cp "$OUT/bzImage" "$WORK/boot/vmlinuz"

cat > "$WORK/boot/grub/grub.cfg" <<'EOF'
set timeout=5
set default=0
insmod all_video

menuentry "Water Candy (live)" {
	linux /boot/vmlinuz quiet
	initrd /boot/initrd.img
}
menuentry "Water Candy (live, safe graphics)" {
	linux /boot/vmlinuz nomodeset
	initrd /boot/initrd.img
}
EOF

grub-mkrescue -o "$ISO" "$WORK" -- -volid WATERCANDY 2>/dev/null
echo "ISO: $ISO"
echo "Write it to a USB stick (ALL DATA ON IT IS ERASED):"
echo "  sudo dd if=$ISO of=/dev/sdX bs=4M status=progress oflag=sync"
