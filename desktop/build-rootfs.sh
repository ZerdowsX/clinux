#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build a Debian (or Ubuntu) disk image for this kernel: APT, networking
# through NetworkManager, Firefox, and the Water Candy desktop (or XFCE).
#
# Needs root and: debootstrap, e2fsprogs (mkfs.ext4), and for Debian on an
# Ubuntu host the debian-archive-keyring package.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
DISTRO=${DISTRO:-debian}		# debian | ubuntu
DESKTOP=${DESKTOP:-watercandy}		# watercandy | xfce
FIREFOX=${FIREFOX:-1}			# install Firefox with APT
FIRMWARE=${FIRMWARE:-1}			# Wi-Fi and other device firmware
LOCALE=${LOCALE:-ru_RU.UTF-8}
TIMEZONE=${TIMEZONE:-Europe/Moscow}
SIZE=${SIZE:-8G}
USER_NAME=${USER_NAME:-user}
USER_PASS=${USER_PASS:-user}
EXTRA_PACKAGES=${EXTRA_PACKAGES:-}
IMG=$OUT/rootfs.ext4
MNT=$OUT/mnt

case "$DISTRO" in
debian)
	SUITE=${SUITE:-trixie}
	MIRROR=${MIRROR:-http://deb.debian.org/debian}
	COMPONENTS=main
	;;
ubuntu)
	SUITE=${SUITE:-noble}
	MIRROR=${MIRROR:-http://archive.ubuntu.com/ubuntu}
	COMPONENTS=main,universe
	;;
*)
	echo "DISTRO must be debian or ubuntu" >&2
	exit 1
	;;
esac

if [ "$(id -u)" -ne 0 ]; then
	echo "Run as root (sudo $0)" >&2
	exit 1
fi

cleanup() {
	for m in dev/pts dev sys proc; do
		umount "$MNT/$m" 2>/dev/null || true
	done
	umount "$MNT" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$OUT" "$MNT"
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
mkfs.ext4 -q -L rootfs "$IMG"
mount -o loop "$IMG" "$MNT"

KEYRING=
if [ "$DISTRO" = debian ] && [ -f /usr/share/keyrings/debian-archive-keyring.gpg ]; then
	KEYRING=--keyring=/usr/share/keyrings/debian-archive-keyring.gpg
fi
# shellcheck disable=SC2086
debootstrap $KEYRING --components="$COMPONENTS" \
	--include=systemd-sysv,dbus,udev,ca-certificates,apt-transport-https \
	"$SUITE" "$MNT" "$MIRROR"

mount -t proc proc "$MNT/proc"
mount -t sysfs sys "$MNT/sys"
mount --bind /dev "$MNT/dev"
mount -t devpts devpts "$MNT/dev/pts"
# Network for the chroot while building; NetworkManager owns it at runtime.
rm -f "$MNT/etc/resolv.conf"
cp -L /etc/resolv.conf "$MNT/etc/resolv.conf"

if [ "$DISTRO" = debian ]; then
	cat > "$MNT/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main contrib non-free non-free-firmware
deb $MIRROR $SUITE-updates main contrib non-free non-free-firmware
deb http://security.debian.org/debian-security $SUITE-security main contrib non-free non-free-firmware
EOF
else
	cat > "$MNT/etc/apt/sources.list" <<EOF
deb $MIRROR $SUITE main restricted universe multiverse
deb $MIRROR $SUITE-updates main restricted universe multiverse
deb $MIRROR $SUITE-security main restricted universe multiverse
EOF
fi

cat > "$MNT/etc/fstab" <<EOF
LABEL=rootfs / ext4 defaults 0 1
EOF
echo clinux > "$MNT/etc/hostname"
cat > "$MNT/etc/hosts" <<EOF
127.0.0.1 localhost
127.0.1.1 clinux
::1 localhost ip6-localhost ip6-loopback
EOF
echo "LANG=$LOCALE" > "$MNT/etc/default/locale"

if [ "$DESKTOP" = watercandy ]; then
	DESKTOP_PKGS="xvfb x11-xkb-utils xauth libx11-6 libxtst6 libxfixes3 \
		fonts-dejavu-core fonts-freefont-ttf fonts-liberation \
		pipewire pipewire-pulse wireplumber alsa-utils dbus-user-session"
	BUILD_PKGS="build-essential pkg-config libx11-dev libxtst-dev \
		libxfixes-dev libpam0g-dev"
else
	DESKTOP_PKGS="xserver-xorg-core xserver-xorg-input-libinput xinit \
		x11-xserver-utils xfce4 xfce4-terminal lightdm lightdm-gtk-greeter \
		dbus-x11 fonts-dejavu-core"
	BUILD_PKGS=
fi

chroot "$MNT" /bin/sh -eu <<CHROOT
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
	network-manager wpasupplicant iw wireless-regdb \
	grub-efi-amd64-bin grub-pc-bin grub-common dosfstools fdisk \
	iproute2 iputils-ping wget curl gnupg tzdata kmod \
	sudo less nano bash-completion locales \
	$DESKTOP_PKGS $BUILD_PKGS $EXTRA_PACKAGES

sed -i -e 's/^# *\(ru_RU.UTF-8\)/\1/' -e 's/^# *\(en_US.UTF-8\)/\1/' \
	/etc/locale.gen 2>/dev/null || true
grep -q '^ru_RU.UTF-8' /etc/locale.gen 2>/dev/null || echo 'ru_RU.UTF-8 UTF-8' >> /etc/locale.gen
grep -q '^en_US.UTF-8' /etc/locale.gen 2>/dev/null || echo 'en_US.UTF-8 UTF-8' >> /etc/locale.gen
# Ubuntu takes the locales as arguments, Debian reads /etc/locale.gen.
if [ -d /var/lib/locales/supported.d ]; then
	locale-gen ru_RU.UTF-8 en_US.UTF-8
else
	locale-gen
fi
ln -sf /usr/share/zoneinfo/$TIMEZONE /etc/localtime
echo $TIMEZONE > /etc/timezone

useradd -m -s /bin/bash -G sudo,video,audio,input "$USER_NAME"
echo "$USER_NAME:$USER_PASS" | chpasswd
echo "root:root" | chpasswd

# NetworkManager manages wired links with DHCP. Ubuntu's package ships a
# config that leaves them to netplan, so override it.
mkdir -p /etc/NetworkManager/conf.d
: > /etc/NetworkManager/conf.d/10-globally-managed-devices.conf
cat > /etc/NetworkManager/conf.d/90-clinux.conf <<NM
[main]
plugins=keyfile
[ifupdown]
managed=true
NM
systemctl enable NetworkManager

# Keep the user's systemd instance running so PipeWire (sound) and the
# session D-Bus are there for the programs Water Candy starts.
mkdir -p /var/lib/systemd/linger
touch /var/lib/systemd/linger/$USER_NAME

# The Water Candy login window authenticates through PAM.
cat > /etc/pam.d/watercandy <<PAM
@include common-auth
@include common-account
PAM
CHROOT

if [ "$FIRMWARE" = 1 ]; then
	if [ "$DISTRO" = debian ]; then
		FW="firmware-iwlwifi firmware-realtek firmware-atheros \
			firmware-brcm80211 firmware-mediatek firmware-misc-nonfree"
	else
		FW="linux-firmware"
	fi
	# shellcheck disable=SC2086
	chroot "$MNT" env DEBIAN_FRONTEND=noninteractive \
		apt-get install -y --no-install-recommends $FW ||
		echo "WARNING: some firmware packages could not be installed" >&2
fi

# The kernel and its modules (Wi-Fi drivers) from build-kernel.sh.
if [ -d "$OUT/modroot/lib/modules" ]; then
	mkdir -p "$MNT/lib/modules"
	cp -a "$OUT/modroot/lib/modules/." "$MNT/lib/modules/"
fi
if [ -f "$OUT/bzImage" ]; then
	mkdir -p "$MNT/boot"
	cp "$OUT/bzImage" "$MNT/boot/vmlinuz-clinux"
fi

if [ "$FIREFOX" = 1 ]; then
	# Firefox from Mozilla's own APT repository (a real .deb, no snap).
	if chroot "$MNT" /bin/sh -eu <<'CHROOT'
export DEBIAN_FRONTEND=noninteractive
install -d -m 0755 /etc/apt/keyrings
wget -q -O /etc/apt/keyrings/packages.mozilla.org.asc \
	https://packages.mozilla.org/apt/repo-signing-key.gpg
echo "deb [signed-by=/etc/apt/keyrings/packages.mozilla.org.asc] https://packages.mozilla.org/apt mozilla main" \
	> /etc/apt/sources.list.d/mozilla.list
cat > /etc/apt/preferences.d/mozilla <<PIN
Package: *
Pin: origin packages.mozilla.org
Pin-Priority: 1000
PIN
apt-get update
apt-get install -y firefox
CHROOT
	then
		echo "Firefox installed from packages.mozilla.org"
	else
		echo "WARNING: could not install Firefox from packages.mozilla.org" >&2
		rm -f "$MNT/etc/apt/sources.list.d/mozilla.list" \
		      "$MNT/etc/apt/preferences.d/mozilla"
		if [ "$DISTRO" = debian ]; then
			chroot "$MNT" apt-get install -y firefox-esr ||
				echo "WARNING: firefox-esr failed too" >&2
		fi
	fi
fi

# Installer used from the live ISO (build-iso.sh).
install -D -m 755 "$SRC/desktop/wc-install" "$MNT/usr/local/sbin/wc-install"

if [ "$DESKTOP" = watercandy ]; then
	mkdir -p "$MNT/usr/local/src"
	rm -rf "$MNT/usr/local/src/watercandy"
	cp -r "$SRC/desktop/watercandy" "$MNT/usr/local/src/watercandy"
	chroot "$MNT" /bin/sh -eu <<'CHROOT'
make -C /usr/local/src/watercandy clean
make -C /usr/local/src/watercandy -j"$(nproc)"
make -C /usr/local/src/watercandy install
cat > /etc/systemd/system/watercandy.service <<UNIT
[Unit]
Description=Water Candy desktop
After=systemd-user-sessions.service systemd-logind.service NetworkManager.service
Conflicts=getty@tty1.service

[Service]
EnvironmentFile=-/etc/default/locale
ExecStart=/usr/local/bin/watercandy
Restart=always
RestartSec=1
StandardInput=null

[Install]
WantedBy=graphical.target
UNIT
systemctl enable watercandy.service
systemctl set-default graphical.target
CHROOT
else
	chroot "$MNT" /bin/sh -eu <<CHROOT
mkdir -p /etc/lightdm/lightdm.conf.d
cat > /etc/lightdm/lightdm.conf.d/50-autologin.conf <<LDM
[Seat:*]
autologin-user=$USER_NAME
autologin-session=xfce
LDM
systemctl enable lightdm
systemctl set-default graphical.target
CHROOT
fi

chroot "$MNT" apt-get clean
# Let NetworkManager write resolv.conf at boot.
rm -f "$MNT/etc/resolv.conf"

echo "Root filesystem: $IMG ($DISTRO $SUITE, $DESKTOP; login $USER_NAME / $USER_PASS)"
