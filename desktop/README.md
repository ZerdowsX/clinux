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

* **окно входа** с паролем (PAM), как в Mac OS X; «Завершить сеанс» в меню-капле;
* **русский интерфейс** (или английский — переключатель в Системных
  настройках) и **русская раскладка**: Alt+Shift или флажок в строке меню,
  работает и в Терминале, и в X11-программах вроде Firefox;
* строка меню с логотипом-каплей; справа — Wi-Fi/сеть, громкость, флажок
  раскладки, часы и **Spotlight** (Alt+Space): поиск программ и файлов;
* окна в стиле Aqua: «светофор», тени, перетаскивание, изменение размера,
  анимации, точка в красной кнопке у несохранённого документа;
* Dock с увеличением значков, миниатюрами свёрнутых окон и Корзиной;
* динамические синие обои;
* программы: **Files**, **Firefox**, **Магазин программ**, **Терминал**,
  **TextEdit**, **Просмотр** (картинки), **Часы**, **Системные настройки**
  (в том числе раздел «Сеть»), **Об этом компьютере**;
* **Магазин программ** поверх APT: Firefox, LibreOffice, GIMP, VLC, Telegram,
  Thunderbird, Inkscape, Krita, Audacity и другие — кнопки «Установить»,
  «Открыть», «Удалить», вкладка «Обновления» и «Обновить всё»; перед
  изменениями спрашивает пароль;
* **уведомления** (как Growl): например, «Доступно 3 обновления» — система
  сама проверяет обновления через минуту после входа и потом каждые 6 часов;
* **буфер обмена** общий для Терминала, TextEdit и X11-программ: в Терминале
  выделите мышью и нажмите Alt+C, вставка — Alt+V (в Firefox — как обычно,
  Ctrl+C / Ctrl+V);
* **звук** через PipeWire: регулятор громкости в строке меню, клавиши
  громкости и большой индикатор уровня на экране;
* **Wi-Fi**: список сетей в меню значка сети, подключение с паролем;
* **снимок экрана**: Alt+Shift+3, файл появляется в папке «Изображения»;
* **установка на компьютер** с загрузочной флешки (см. ниже).

Сам Water Candy работает от root (ему нужны видеокарта и устройства ввода),
а Терминал, программы и чтение/запись файлов — от имени пользователя.

Клавиша Alt (или Win) работает как Command: Alt+T — терминал, Alt+N — окно
Files, Alt+W — закрыть окно, Alt+M — свернуть, Alt+Q — выйти из программы,
Alt+S — сохранить, Alt+C / Alt+V — копировать / вставить, Alt+Space — Spotlight,
Alt+Shift+3 — снимок экрана.

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
virtio, USB-модем с телефона и Wi-Fi-адаптеры Intel, Realtek, Atheros,
MediaTek и Broadcom (их прошивки ставятся при сборке образа). Wi-Fi-сети —
в меню значка сети в строке меню.

### Настройки сборки

| Переменная              | По умолчанию                   | Где              |
|-------------------------|--------------------------------|------------------|
| `DISTRO`                | `debian` (или `ubuntu`)        | build-rootfs.sh  |
| `SUITE`                 | `trixie` / `noble`             | build-rootfs.sh  |
| `MIRROR`                | официальное зеркало            | build-rootfs.sh  |
| `DESKTOP`               | `watercandy` (или `xfce`)      | build-rootfs.sh  |
| `FIREFOX`               | `1` (`0` — без Firefox)        | build-rootfs.sh  |
| `FIRMWARE`              | `1` (прошивки Wi-Fi и др.)     | build-rootfs.sh  |
| `LOCALE`                | `ru_RU.UTF-8`                  | build-rootfs.sh  |
| `TIMEZONE`              | `Europe/Moscow`                | build-rootfs.sh  |
| `EXTRA_PACKAGES`        | —                              | build-rootfs.sh  |
| `SIZE`                  | `8G`                           | build-rootfs.sh  |
| `USER_NAME`/`USER_PASS` | `user` / `user`                | build-rootfs.sh  |
| `MEM`, `CPUS`           | `4G`, `4`                      | run-qemu.sh      |
| `RES`                   | `1280x800`                     | run-qemu.sh      |
| `DISPLAY_BACKEND`       | `gtk` (или `sdl`, `vnc=:0`)    | run-qemu.sh      |
| `OUT`                   | `desktop/out`                  | все скрипты      |

## Загрузочная флешка и установка на компьютер

Готовый ISO собирается автоматически на серверах GitHub (workflow
`.github/workflows/build-iso.yml`): после каждого изменения в `master` его
можно скачать со страницы релиза **latest-iso** в репозитории (вкладка
Releases), а для PR — в разделе Artifacts запуска на вкладке Actions.
Собрать вручную: Actions → Build ISO → Run workflow.

Или соберите сами:

    sudo apt install grub-pc-bin grub-efi-amd64-bin xorriso mtools \
                     squashfs-tools busybox-static cpio
    ./desktop/build-kernel.sh
    sudo ./desktop/build-rootfs.sh
    sudo ./desktop/build-iso.sh

Получится `desktop/out/clinux-watercandy.iso` — он загружается и на новых
компьютерах (UEFI), и на старых (BIOS). Запишите его на флешку (всё на ней
сотрётся; вместо `/dev/sdX` — ваша флешка, проверьте через `lsblk`):

    sudo dd if=desktop/out/clinux-watercandy.iso of=/dev/sdX bs=4M status=progress oflag=sync

Загрузитесь с флешки: система запустится сразу, без установки. На рабочем
столе будет значок **«Установить Water Candy»**: выберите диск, поставьте
галочку «Стереть выбранный диск» и нажмите «Установить». Установщик
(`/usr/local/sbin/wc-install`) размечает диск (GPT: раздел EFI + корневой
ext4), копирует систему и ставит загрузчик GRUB для UEFI и BIOS.

Проверить ISO в QEMU:

    qemu-system-x86_64 -enable-kvm -m 4G -bios /usr/share/OVMF/OVMF_CODE_4M.fd \
        -cdrom desktop/out/clinux-watercandy.iso -drive file=disk.img,if=virtio,format=raw \
        -device virtio-vga -device virtio-tablet-pci -device virtio-keyboard-pci

Ограничения на настоящем железе: видеокарты NVIDIA и новые AMD работают
через простой видеовывод прошивки (без ускорения); звук на ноутбуках с Intel
SOF и часть Wi-Fi-адаптеров могут не заработать.

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
