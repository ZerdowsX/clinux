#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build a Debian disk image with Xorg, XFCE and LightDM.
# Needs root and: debootstrap, e2fsprogs (mkfs.ext4).
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
SUITE=${SUITE:-bookworm}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
SIZE=${SIZE:-6G}
USER_NAME=${USER_NAME:-user}
USER_PASS=${USER_PASS:-user}
IMG=$OUT/rootfs.ext4
MNT=$OUT/mnt

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root (sudo $0)" >&2
	exit 1
fi

mkdir -p "$OUT" "$MNT"
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
mkfs.ext4 -q -L rootfs "$IMG"
mount -o loop "$IMG" "$MNT"
trap 'umount -R "$MNT" 2>/dev/null || true' EXIT

debootstrap --include=systemd-sysv,dbus,udev "$SUITE" "$MNT" "$MIRROR"

mount -t proc proc "$MNT/proc"
mount -t sysfs sys "$MNT/sys"
mount --bind /dev "$MNT/dev"

cat > "$MNT/etc/fstab" <<FSTAB
LABEL=rootfs / ext4 defaults 0 1
FSTAB
echo clinux > "$MNT/etc/hostname"

chroot "$MNT" /bin/sh -eu <<CHROOT
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
	xserver-xorg-core xserver-xorg-input-libinput xinit x11-xserver-utils \
	xfce4 xfce4-terminal lightdm lightdm-gtk-greeter \
	dbus-x11 fonts-dejavu sudo locales
apt-get clean

useradd -m -s /bin/bash -G sudo,video,audio,input "$USER_NAME"
echo "$USER_NAME:$USER_PASS" | chpasswd
echo "root:root" | chpasswd

mkdir -p /etc/lightdm/lightdm.conf.d
cat > /etc/lightdm/lightdm.conf.d/50-autologin.conf <<LDM
[Seat:*]
autologin-user=$USER_NAME
autologin-session=xfce
LDM
cat > /etc/systemd/network/80-dhcp.network <<NET
[Match]
Name=en*
[Network]
DHCP=yes
NET
systemctl enable systemd-networkd
systemctl enable lightdm
systemctl set-default graphical.target
CHROOT

echo "Root filesystem: $IMG (login $USER_NAME / $USER_PASS)"
