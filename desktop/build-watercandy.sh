#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Build an initramfs that boots straight into the Water Candy desktop:
# busybox + watercandy (static) + fonts. No X server, no distribution.
# Needs: gcc (with static libc), a static busybox, cpio, gzip, DejaVu fonts.
set -eu

SRC=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-$SRC/desktop/out}
BUSYBOX=${BUSYBOX:-$(command -v busybox || true)}
ROOT=$OUT/watercandy-root

if [ -z "$BUSYBOX" ] || ! file -L "$BUSYBOX" | grep -q "statically linked"; then
	echo "Need a statically linked busybox (apt install busybox-static)" >&2
	exit 1
fi

make -C "$SRC/desktop/watercandy" clean >/dev/null
make -C "$SRC/desktop/watercandy" static

rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/sbin" "$ROOT/etc" "$ROOT/root" "$ROOT/proc" \
	 "$ROOT/sys" "$ROOT/dev" "$ROOT/tmp" "$ROOT/run"
cp "$BUSYBOX" "$ROOT/bin/busybox"
cp "$SRC/desktop/watercandy/watercandy" "$ROOT/bin/watercandy"

# Fonts are looked up by their usual Debian paths, so keep the same layout.
found=0
for f in \
	/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf \
	/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf \
	/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
	/usr/share/fonts/truetype/freefont/FreeSerif.ttf \
	/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf; do
	if [ -f "$f" ]; then
		mkdir -p "$ROOT$(dirname "$f")"
		cp "$f" "$ROOT$f"
		found=1
	fi
done
if [ "$found" = 0 ]; then
	echo "No fonts found (apt install fonts-dejavu-core fonts-freefont-ttf)" >&2
	exit 1
fi

cat > "$ROOT/etc/passwd" <<'PW'
root:x:0:0:root:/root:/bin/sh
PW
echo "root:x:0:" > "$ROOT/etc/group"
echo "clinux" > "$ROOT/etc/hostname"
cat > "$ROOT/etc/profile" <<'PR'
export PATH=/bin:/sbin
export HOME=/root
PS1='\h:\w \u\$ '
alias ls='ls --color=auto'
PR
cat > "$ROOT/root/Welcome.txt" <<'TXT'
Welcome to Water Candy!
TXT

cat > "$ROOT/init" <<'INIT'
#!/bin/busybox sh
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm
mount -t devpts -o gid=5,mode=620,ptmxmode=666 devpts /dev/pts
mount -t tmpfs tmpfs /tmp
mount -t tmpfs tmpfs /run
hostname -F /etc/hostname
ip link set lo up 2>/dev/null
export PATH=/bin:/sbin HOME=/root
cd /root

# A rescue shell on the serial console.
setsid sh -c 'exec sh -l </dev/ttyS0 >/dev/ttyS0 2>&1' &

while :; do
	/bin/watercandy $(cat /proc/cmdline | grep -o 'wc\.demo' | sed 's/wc\.demo/--demo/')
	echo "watercandy exited ($?), restarting" > /dev/console
	sleep 1
done
INIT
chmod +x "$ROOT/init"

(cd "$ROOT" && find . | cpio -o -H newc --quiet | gzip -9) > "$OUT/watercandy-initrd.gz"
echo "Initramfs: $OUT/watercandy-initrd.gz"
