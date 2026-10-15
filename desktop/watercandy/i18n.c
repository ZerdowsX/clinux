// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - interface languages (English and Russian).
 *
 * Strings are written in English in the source and wrapped in _(); the
 * table below maps them to Russian.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc.h"

static const char *const ru[][2] = {
	/* menu bar */
	{ "About This Computer", "Об этом компьютере" },
	{ "System Preferences...", "Системные настройки…" },
	{ "Dock", "Dock" },
	{ "Restart...", "Перезагрузить…" },
	{ "Shut Down...", "Выключить…" },
	{ "Log Out...", "Завершить сеанс…" },
	{ "About %s", "О программе «%s»" },
	{ "Quit %s", "Завершить «%s»" },
	{ "Preferences...", "Настройки…" },
	{ "Hide Others", "Скрыть остальные" },
	{ "Show All", "Показать все" },
	{ "File", "Файл" },
	{ "Edit", "Правка" },
	{ "View", "Вид" },
	{ "Go", "Переход" },
	{ "Window", "Окно" },
	{ "Help", "Справка" },
	{ "New Files Window", "Новое окно Files" },
	{ "New Terminal", "Новый терминал" },
	{ "New Text Document", "Новый документ" },
	{ "Open...", "Открыть…" },
	{ "Save", "Сохранить" },
	{ "Close Window", "Закрыть окно" },
	{ "Undo", "Отменить" },
	{ "Cut", "Вырезать" },
	{ "Copy", "Копировать" },
	{ "Paste", "Вставить" },
	{ "Select All", "Выбрать все" },
	{ "as Icons", "Значки" },
	{ "as List", "Список" },
	{ "Show Clock", "Показать часы" },
	{ "Computer", "Компьютер" },
	{ "Home", "Домой" },
	{ "Applications", "Программы" },
	{ "Utilities", "Утилиты" },
	{ "Documents", "Документы" },
	{ "Pictures", "Изображения" },
	{ "Minimize Window", "Свернуть окно" },
	{ "Zoom Window", "Изменить масштаб" },
	{ "Water Candy Help", "Справка Water Candy" },
	{ "Take Screenshot", "Снимок экрана" },
	{ "Search", "Поиск" },
	/* apps */
	{ "Files", "Files" },
	{ "Terminal", "Терминал" },
	{ "Clock", "Часы" },
	{ "System Preferences", "Системные настройки" },
	{ "Trash", "Корзина" },
	{ "Firefox", "Firefox" },
	{ "App Store", "Магазин программ" },
	{ "TextEdit", "TextEdit" },
	{ "Preview", "Просмотр" },
	{ "Water HD", "Water HD" },
	{ "Install Water Candy", "Установить Water Candy" },
	/* about */
	{ "(Version %s)", "(Версия %s)" },
	{ "Memory:", "Память:" },
	{ "Processor:", "Процессор:" },
	{ "Kernel:", "Ядро:" },
	/* preferences */
	{ "Desktop & Dock", "Рабочий стол и Dock" },
	{ "Make Water Candy look and feel the way you like.",
	  "Настройте Water Candy так, как вам нравится." },
	{ "Animate the blue wallpaper", "Анимировать синие обои" },
	{ "Magnify Dock icons", "Увеличивать значки в Dock" },
	{ "Show seconds in the menu bar clock", "Показывать секунды в часах" },
	{ "Russian interface", "Русский интерфейс" },
	{ "Wallpaper motion: %.1fx", "Движение обоев: %.1fx" },
	{ "Calm", "Спокойно" },
	{ "Lively", "Живо" },
	{ "Network", "Сеть" },
	{ "%s is connected to the Internet", "%s: подключено к интернету" },
	{ "%s has no Internet connection", "%s: нет подключения к интернету" },
	{ "No network connection", "Нет подключения к сети" },
	{ "Interface:", "Интерфейс:" },
	{ "IP Address:", "IP-адрес:" },
	{ "Router:", "Маршрутизатор:" },
	{ "DNS Server:", "DNS-сервер:" },
	{ "Ethernet", "Ethernet" },
	{ "Wi-Fi", "Wi-Fi" },
	{ "USB Ethernet", "USB-модем" },
	/* files */
	{ "Back", "Назад" },
	{ "Up", "Вверх" },
	{ "This folder is empty", "Папка пуста" },
	/* dialogs */
	{ "OK", "OK" },
	{ "Cancel", "Отменить" },
	{ "Install", "Установить" },
	{ "Firefox is not installed", "Firefox не установлен" },
	{ "Water Candy can download it with APT. Click Install to open Terminal and run: sudo apt install firefox",
	  "Water Candy может скачать его через APT. Нажмите «Установить», откроется Терминал с командой sudo apt install firefox" },
	{ "X11 support is missing", "Нет поддержки X11" },
	{ "Could not start the program", "Не удалось запустить программу" },
	{ "The virtual X server (Xvfb) did not start.",
	  "Виртуальный X-сервер (Xvfb) не запустился." },
	{ "Starting the X server\xe2\x80\xa6", "Запуск X-сервера…" },
	{ "Starting\xe2\x80\xa6", "Запуск…" },
	/* app store */
	{ "App Store...", "Магазин программ…" },
	{ "Software Update...", "Обновление ПО…" },
	{ "Catalog", "Каталог" },
	{ "Updates", "Обновления" },
	{ "Install", "Установить" },
	{ "Open", "Открыть" },
	{ "Remove", "Удалить" },
	{ "Not available", "Недоступно" },
	{ "Installing %s", "Установка %s" },
	{ "Removing %s", "Удаление %s" },
	{ "%s is ready to use", "%s: готово" },
	{ "Installation failed", "Ошибка установки" },
	{ "Could not start APT", "Не удалось запустить APT" },
	{ "Software Update", "Обновление ПО" },
	{ "Updating software", "Обновление программ" },
	{ "Your computer is up to date", "Все программы обновлены" },
	{ "All software is current.", "Новых версий нет." },
	{ "Click Check Now to look for updates.",
	  "Нажмите «Проверить», чтобы найти обновления." },
	{ "Checking for updates\xe2\x80\xa6", "Поиск обновлений…" },
	{ "Update All", "Обновить всё" },
	{ "Check Now", "Проверить" },
	{ "Please wait", "Подождите" },
	{ "Another installation is in progress.", "Уже идёт другая установка." },
	{ "Water Candy wants to make changes.", "Water Candy хочет внести изменения." },
	{ "Type the password for %s to allow this.",
	  "Введите пароль пользователя %s, чтобы разрешить это." },
	{ "Wrong password. Try again.", "Неверный пароль. Попробуйте ещё раз." },
	/* network */
	{ "Wi-Fi: On", "Wi-Fi: вкл." },
	{ "Wi-Fi: Off", "Wi-Fi: выкл." },
	{ "Turn Wi-Fi Off", "Выключить Wi-Fi" },
	{ "Turn Wi-Fi On", "Включить Wi-Fi" },
	{ "Looking for networks\xe2\x80\xa6", "Поиск сетей…" },
	{ "Join Wi-Fi network", "Подключение к сети Wi-Fi" },
	{ "The Wi-Fi network \"%s\" requires a password.",
	  "Для сети Wi-Fi «%s» нужен пароль." },
	{ "Join", "Подключиться" },
	{ "Connected to %s", "Подключено к сети %s" },
	{ "Could not join the network", "Не удалось подключиться к сети" },
	/* documents */
	{ "Untitled", "Без названия" },
	{ "Save As", "Сохранить как" },
	{ "Name of the document (it goes to your Documents folder):",
	  "Имя документа (он будет сохранён в папке «Документы»):" },
	{ "Could not save the document", "Не удалось сохранить документ" },
	{ "Could not open the document", "Не удалось открыть документ" },
	{ "This image cannot be opened.", "Не удалось открыть изображение." },
	{ "The item cannot be found", "Объект не найден" },
	{ "There is no application to open this document",
	  "Нет программы для открытия этого документа" },
	{ "Screenshot", "Снимок экрана" },
	{ "Could not save the screenshot", "Не удалось сохранить снимок экрана" },
	{ "No results", "Ничего не найдено" },
	/* installer */
	{ "Installer", "Установщик" },
	{ "Choose the disk to install Water Candy on.",
	  "Выберите диск, на который нужно установить Water Candy." },
	{ "Everything on that disk will be erased.",
	  "Все данные на этом диске будут удалены." },
	{ "Erase the selected disk and install", "Стереть выбранный диск и установить" },
	{ "Installing. This can take a while.", "Идёт установка. Это может занять время." },
	{ "Water Candy is installed.", "Water Candy установлена." },
	{ "Remove the USB stick and restart the computer.",
	  "Извлеките флешку и перезагрузите компьютер." },
	{ "The installation failed.", "Установка не удалась." },
	{ "Could not start the installer", "Не удалось запустить установщик" },
	{ "No suitable disk was found (at least 4 GB).",
	  "Подходящий диск не найден (нужно не меньше 4 ГБ)." },
	{ "Disk", "Диск" },
	{ " (removable)", " (съёмный)" },
	{ "GB", "ГБ" },
	{ "TB", "ТБ" },
	{ "Partitioning", "Разметка диска" },
	{ "Creating file systems", "Создание файловых систем" },
	{ "Copying files", "Копирование файлов" },
	{ "Configuring the system", "Настройка системы" },
	{ "Installing the boot loader", "Установка загрузчика" },
	{ "Finishing", "Завершение" },
	{ "Done", "Готово" },
	/* login */
	{ "Name", "Имя" },
	{ "Password", "Пароль" },
	{ "Name:", "Имя:" },
	{ "Password:", "Пароль:" },
	{ "Restart", "Перезагрузить" },
	{ "Shut Down", "Выключить" },
	{ "Log In", "Войти" },
	{ "Log Out %s...", "Завершить сеанс %s…" },
	{ "The name or password you entered is incorrect.",
	  "Неверное имя или пароль." },
	{ "Network Preferences...", "Настройки сети…" },
	{ NULL, NULL }
};

static const char *const days_ru[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
static const char *const days_full_ru[] = { "воскресенье", "понедельник", "вторник",
					    "среда", "четверг", "пятница", "суббота" };
static const char *const months_ru[] = { "января", "февраля", "марта", "апреля",
					 "мая", "июня", "июля", "августа",
					 "сентября", "октября", "ноября", "декабря" };

void i18n_init(void)
{
	const char *l = getenv("WC_LANG");

	if (!l || !*l)
		l = getenv("LANG");
	dt.set.lang = (l && !strncmp(l, "ru", 2)) ? LANG_RU : LANG_EN;
}

const char *tr(const char *s)
{
	int i;

	if (dt.set.lang != LANG_RU || !s)
		return s;
	for (i = 0; ru[i][0]; i++)
		if (!strcmp(ru[i][0], s))
			return ru[i][1];
	return s;
}

/* Russian plural forms: one / few / many. */
const char *plural(int n, const char *one, const char *few, const char *many)
{
	int a = abs(n) % 100, b = a % 10;

	if (a >= 11 && a <= 14)
		return many;
	if (b == 1)
		return one;
	if (b >= 2 && b <= 4)
		return few;
	return many;
}

void format_items(char *buf, size_t sz, int n)
{
	if (dt.set.lang == LANG_RU)
		snprintf(buf, sz, "%d %s", n, plural(n, "объект", "объекта", "объектов"));
	else
		snprintf(buf, sz, "%d item%s", n, n == 1 ? "" : "s");
}

void format_clock(char *buf, size_t sz, const struct tm *tm, int seconds)
{
	if (dt.set.lang == LANG_RU) {
		if (seconds)
			snprintf(buf, sz, "%s %d:%02d:%02d", days_ru[tm->tm_wday],
				 tm->tm_hour, tm->tm_min, tm->tm_sec);
		else
			snprintf(buf, sz, "%s %d:%02d", days_ru[tm->tm_wday],
				 tm->tm_hour, tm->tm_min);
		return;
	}
	strftime(buf, sz, seconds ? "%a %l:%M:%S %p" : "%a %l:%M %p", tm);
	{
		char *dbl = strstr(buf, "  ");

		if (dbl)
			memmove(dbl, dbl + 1, strlen(dbl));
	}
}

void format_date(char *buf, size_t sz, const struct tm *tm)
{
	if (dt.set.lang == LANG_RU)
		snprintf(buf, sz, "%s, %d %s", days_full_ru[tm->tm_wday], tm->tm_mday,
			 months_ru[tm->tm_mon]);
	else
		strftime(buf, sz, "%A, %B %e", tm);
}
