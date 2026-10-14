# Рабочий стол для этого ядра

**Water Candy** — своя оболочка в стиле Mac OS X (Aqua) со своим движком
вывода: рисует напрямую через DRM/KMS ядра (или `/dev/fb0`), без X-сервера.

Её можно запустить двумя способами:

1. **Полная система с APT** (рекомендуется): Debian или Ubuntu, пакетный
   менеджер APT, интернет через NetworkManager, Firefox, а Water Candy —
   рабочий стол. Программы для X11 (Firefox и любые другие) открываются в
   окнах Water Candy через мост X11, как X11.app в Mac OS X.
2. **Мини-система в initramfs**: только busybox и Water Candy, загружается за
   секунды. Без APT и без X11-программ.

Вместо Water Candy можно поставить обычный XFCE (`DESKTOP=xfce`).

## Что умеет Water Candy

* строка меню с логотипом-каплей, меню приложений, индикатор сети, часы;
* окна в стиле Aqua: полосатый фон, «светофор» (закрыть / свернуть /
  развернуть), тени, перетаскивание, изменение размера за правый нижний угол,
  анимации открытия и сворачивания в Dock (двойной клик по заголовку тоже
  сворачивает);
* Dock с увеличением иконок под курсором, подписями, «прыжком» при запуске,
  миниатюрами свёрнутых окон и Корзиной;
* динамические синие обои (процедурные, медленно переливаются);
* приложения: **Files**, **Firefox**, **Terminal**, **Clock**,
  **System Preferences** (там же раздел **Network**: интерфейс, IP, роутер,
  DNS), **About This Computer**;
* иконка диска **Water HD** на рабочем столе (двойной клик открывает `/`).

Сам Water Candy работает от root (ему нужны видеокарта и устройства ввода),
а Терминал и программы запускает от имени обычного пользователя (`user`, или
тот, что указан в `WC_USER`). Для команд администратора — `sudo`.

Клавиша Alt (или Win) работает как Command: Alt+T — терминал, Alt+N — окно
Files, Alt+W — закрыть окно, Alt+M — свернуть, Alt+Q — выйти из приложения.

## Полная система: APT, интернет, Firefox

    sudo apt install build-essential flex bison bc libelf-dev libssl-dev \
                     debootstrap qemu-system-x86
    ./desktop/build-kernel.sh
    sudo ./desktop/build-rootfs.sh        # Debian 13 + Water Candy + Firefox
    ./desktop/run-qemu.sh

Пароль пользователя `user` — `user` (у root — `root`).

### Установка программ (APT)

Откройте Terminal в Dock:

    sudo apt update
    sudo apt install gimp            # любая программа из репозитория
    watercandy --open gimp           # показать X11-программу в окне Water Candy

Firefox ставится при сборке образа из официального APT-репозитория Mozilla
(`packages.mozilla.org`), так что обновляется обычным `sudo apt upgrade`. Если
его нет, щелчок по Firefox в Dock предложит установить: откроется Terminal с
командой `sudo apt install firefox`.

### Интернет

В образе работает NetworkManager: проводная сеть настраивается сама по DHCP,
DNS тоже. В QEMU сеть уже подключена (`-nic user`). Состояние видно в строке
меню (значок со столбиками) и в System Preferences → Network. Из Терминала:

    nmcli device status
    ping -c 3 deb.debian.org

В ядре включены сетевые карты Intel (e1000/e1000e/igb), Realtek (r8169),
virtio и USB-модем с телефона. Wi-Fi пока не поддерживается: для него нужны
драйверы и прошивки, которых нет в этой сборке.

### Настройки сборки

| Переменная              | По умолчанию                   | Где              |
|-------------------------|--------------------------------|------------------|
| `DISTRO`                | `debian` (или `ubuntu`)        | build-rootfs.sh  |
| `SUITE`                 | `trixie` / `noble`             | build-rootfs.sh  |
| `MIRROR`                | официальное зеркало            | build-rootfs.sh  |
| `DESKTOP`               | `watercandy` (или `xfce`)      | build-rootfs.sh  |
| `FIREFOX`               | `1` (`0` — без Firefox)        | build-rootfs.sh  |
| `EXTRA_PACKAGES`        | —                              | build-rootfs.sh  |
| `SIZE`                  | `8G`                           | build-rootfs.sh  |
| `USER_NAME`/`USER_PASS` | `user` / `user`                | build-rootfs.sh  |
| `MEM`, `CPUS`           | `4G`, `4`                      | run-qemu.sh      |
| `RES`                   | `1280x800`                     | run-qemu.sh      |
| `DISPLAY_BACKEND`       | `gtk` (или `sdl`, `vnc=:0`)    | run-qemu.sh      |
| `OUT`                   | `desktop/out`                  | все скрипты      |

## Мини-система в initramfs

    sudo apt install busybox-static cpio fonts-dejavu-core fonts-freefont-ttf
    ./desktop/build-kernel.sh
    ./desktop/build-watercandy.sh
    ./desktop/run-watercandy.sh

`RES=1600x1000 ./desktop/run-watercandy.sh` меняет разрешение;
`APPEND=wc.demo ./desktop/run-watercandy.sh` сразу открывает «About This
Computer».

## Разработка

Исходники Water Candy — в `desktop/watercandy/`. Мост X11 собирается, если
установлены `libx11-dev` и `libxtst-dev` (и нужен `xvfb` для запуска).
Посмотреть картинку без виртуальной машины:

    make -C desktop/watercandy
    ./desktop/watercandy/watercandy --render shot.ppm 1280x800 about files
    WC_SETTLE=3 ./desktop/watercandy/watercandy --render shot.ppm x11:xcalc

## Только ядро

    make x86_64_defconfig kvm_guest.config x11_desktop.config
    make -j$(nproc)

На реальном железе с GPU AMD/NVIDIA дополнительно включите `CONFIG_DRM_AMDGPU`
или `CONFIG_DRM_NOUVEAU` (`make menuconfig`) и поставьте нужные прошивки
(`firmware-amd-graphics` / `firmware-misc-nonfree`) в rootfs.
