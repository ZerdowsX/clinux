# Рабочий стол для этого ядра

Здесь два варианта:

1. **Water Candy** — своя оболочка в стиле Mac OS X (Aqua). Свой движок
   вывода: рисует напрямую через DRM/KMS ядра (или `/dev/fb0`), без X11 и без
   дистрибутива. Загружается из initramfs за секунды.
2. **X11 + XFCE** — обычный Debian с Xorg и XFCE.

## Water Candy

Что есть:

* строка меню с логотипом-каплей, меню приложений, часы;
* окна в стиле Aqua: полосатый фон, «светофор» (закрыть / свернуть /
  развернуть), тени, перетаскивание, изменение размера за правый нижний угол,
  анимации открытия и сворачивания в Dock (двойной клик по заголовку тоже
  сворачивает);
* Dock с увеличением иконок под курсором, подписями, «прыжком» при запуске,
  миниатюрами свёрнутых окон и Корзиной;
* динамические синие обои (процедурные, медленно переливаются);
* приложения: **Files** (файловый менеджер), **Terminal** (настоящий
  терминал с `/bin/sh`), **Clock**, **System Preferences**,
  **About This Computer**;
* иконка диска **Water HD** на рабочем столе (двойной клик открывает `/`).

Клавиша Alt (или Win) работает как Command: Alt+T — терминал, Alt+N — окно
Files, Alt+W — закрыть окно, Alt+M — свернуть, Alt+Q — выйти из приложения.

Сборка и запуск в QEMU:

    sudo apt install build-essential flex bison bc libelf-dev libssl-dev \
                     busybox-static cpio fonts-dejavu-core fonts-freefont-ttf \
                     qemu-system-x86
    ./desktop/build-kernel.sh
    ./desktop/build-watercandy.sh
    ./desktop/run-watercandy.sh

`RES=1600x1000 ./desktop/run-watercandy.sh` меняет разрешение. Если в
командной строке ядра есть `wc.demo`, при старте откроется окно «About This
Computer» (`APPEND=wc.demo ./desktop/run-watercandy.sh`).

Исходники — в `desktop/watercandy/`. Посмотреть картинку без виртуальной
машины можно так:

    make -C desktop/watercandy
    ./desktop/watercandy/watercandy --render shot.ppm 1280x800 about files

## X11 + XFCE

X11 (Xorg) и XFCE — это программы пространства пользователя, в само ядро их
«вшить» нельзя. От ядра нужно другое: драйверы графики (DRM/KMS), ввода (evdev),
звука и базовые подсистемы, на которые опираются systemd, D-Bus и Xorg.
Всё это включает фрагмент конфигурации `kernel/configs/x11_desktop.config`.
Сам рабочий стол ставится в отдельный образ корневой ФС (Debian + Xorg + XFCE +
LightDM).

### Быстрый старт (x86_64)

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

### Настройки

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

### Только ядро, без скриптов

    make x86_64_defconfig kvm_guest.config x11_desktop.config
    make -j$(nproc)

На реальном железе с GPU AMD/NVIDIA дополнительно включите `CONFIG_DRM_AMDGPU`
или `CONFIG_DRM_NOUVEAU` (`make menuconfig`) и поставьте нужные прошивки
(`firmware-amd-graphics` / `firmware-misc-nonfree`) в rootfs.
