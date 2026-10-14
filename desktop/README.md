# X11 + XFCE поверх этого ядра

X11 (Xorg) и XFCE — это программы пространства пользователя, в само ядро их
«вшить» нельзя. От ядра нужно другое: драйверы графики (DRM/KMS), ввода (evdev),
звука и базовые подсистемы, на которые опираются systemd, D-Bus и Xorg.
Всё это включает фрагмент конфигурации `kernel/configs/x11_desktop.config`.
Сам рабочий стол ставится в отдельный образ корневой ФС (Debian + Xorg + XFCE +
LightDM).

## Быстрый старт (x86_64)

Зависимости на хосте (Debian/Ubuntu):

    sudo apt install build-essential flex bison bc libelf-dev libssl-dev \
                     debootstrap qemu-system-x86

1. Собрать ядро:

       ./desktop/build-kernel.sh

2. Собрать образ с XFCE (нужен root, ~6 ГБ, скачивает пакеты Debian):

       sudo ./desktop/build-rootfs.sh

3. Запустить в QEMU:

       ./desktop/run-qemu.sh

После загрузки LightDM автоматически входит в сессию XFCE.
Логин/пароль: `user` / `user` (root: `root`).

Всё складывается в `desktop/out/`.

## Настройки

Переменные окружения:

| Переменная        | По умолчанию                   | Где              |
|-------------------|--------------------------------|------------------|
| `OUT`             | `desktop/out`                  | все скрипты      |
| `SUITE`           | `bookworm`                     | build-rootfs.sh  |
| `MIRROR`          | `http://deb.debian.org/debian` | build-rootfs.sh  |
| `SIZE`            | `6G`                           | build-rootfs.sh  |
| `USER_NAME`/`USER_PASS` | `user` / `user`          | build-rootfs.sh  |
| `MEM`, `CPUS`     | `2G`, `2`                      | run-qemu.sh      |
| `DISPLAY_BACKEND` | `gtk` (или `sdl`, `vnc=:0`)    | run-qemu.sh      |

## Только ядро, без скриптов

    make x86_64_defconfig kvm_guest.config x11_desktop.config
    make -j$(nproc)

На реальном железе с GPU AMD/NVIDIA дополнительно включите `CONFIG_DRM_AMDGPU`
или `CONFIG_DRM_NOUVEAU` (`make menuconfig`) и поставьте нужные прошивки
(`firmware-amd-graphics` / `firmware-misc-nonfree`) в rootfs.
