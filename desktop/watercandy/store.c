// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - App Store: a catalog of programs installed with APT, the
 * Updates tab, and the background check that announces new updates.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define STORE_W 760
#define STORE_H 560
#define TOOLBAR_H 52
#define ROW_H 74
#define BTN_W 112

struct app_entry {
	const char *name;
	const char *pkg;
	const char *desc_en, *desc_ru;
	const char *run;	/* command to open it */
	int terminal;		/* runs in Terminal rather than as an X11 window */
	uint32_t color;
	const char *glyph;
	/* state */
	int known, installed, available;
};

static struct app_entry catalog[] = {
	{ "Firefox", "firefox", "Fast, private web browser",
	  "Быстрый и приватный веб-браузер", "firefox", 0, 0xffe66000, "globe" },
	{ "LibreOffice", "libreoffice", "Documents, spreadsheets and presentations",
	  "Документы, таблицы и презентации", "libreoffice", 0, 0xff18a303, "L" },
	{ "GIMP", "gimp", "Photo and image editor", "Редактор фотографий и изображений",
	  "gimp", 0, 0xff8a6a4a, "G" },
	{ "VLC", "vlc", "Plays any video and music", "Проигрыватель видео и музыки",
	  "vlc", 0, 0xfff28a00, "\xe2\x96\xb6" },
	{ "Telegram", "telegram-desktop", "Messenger", "Мессенджер",
	  "telegram-desktop", 0, 0xff2aa1da, "\xe2\x9e\xa4" },
	{ "Thunderbird", "thunderbird", "Email and calendar", "Почта и календарь",
	  "thunderbird", 0, 0xff2f6fd6, "@" },
	{ "Inkscape", "inkscape", "Vector graphics", "Векторная графика",
	  "inkscape", 0, 0xff222222, "I" },
	{ "Krita", "krita", "Digital painting", "Цифровое рисование",
	  "krita", 0, 0xffd2457c, "K" },
	{ "Audacity", "audacity", "Record and edit audio", "Запись и монтаж звука",
	  "audacity", 0, 0xff1f5bd1, "A" },
	{ "Transmission", "transmission-gtk", "BitTorrent client", "Торрент-клиент",
	  "transmission-gtk", 0, 0xffc4271f, "T" },
	{ "SuperTux", "supertux", "Jump'n'run game", "Игра-платформер",
	  "supertux2", 0, 0xff3aa63a, "S" },
	{ "htop", "htop", "Process viewer for Terminal", "Диспетчер процессов в Терминале",
	  "htop", 1, 0xff3a3a3a, "terminal" },
};
#define NCATALOG (int)(sizeof(catalog) / sizeof(catalog[0]))

/* One APT job at a time: install / remove / update / upgrade. */
static struct {
	int running;
	int entry;		/* catalog index, or -1 for system updates */
	float progress;
	char what[96];
	char error[256];
} job = { .entry = -1 };

static struct {
	int count;
	char names[2048];
	int checking;
	double next_check;
	int last_notified;
	double last_check;
} upd;

static window_t *store_win;
static int store_tab, store_scroll, store_pressed = -1, querying;
static double auth_until;
static void (*after_auth)(void);
static int pending_entry;

/* ---------------------------------------------------------------------- */
/* Package state                                                          */

static void parse_policy(int status, const char *out, void *ctx)
{
	const char *p = out;
	struct app_entry *cur = NULL;

	(void)status;
	(void)ctx;
	querying = 0;
	while (p && *p) {
		const char *nl = strchr(p, '\n');
		char line[256];
		int i;

		snprintf(line, sizeof(line), "%.*s", (int)(nl ? nl - p : (int)strlen(p)), p);
		p = nl ? nl + 1 : NULL;
		if (line[0] && line[0] != ' ' && line[strlen(line) - 1] == ':') {
			line[strlen(line) - 1] = 0;
			cur = NULL;
			for (i = 0; i < NCATALOG; i++)
				if (!strcmp(catalog[i].pkg, line))
					cur = &catalog[i];
			if (cur) {
				cur->known = 1;
				cur->installed = 0;
				cur->available = 0;
			}
		} else if (cur && strstr(line, "Installed:")) {
			cur->installed = !strstr(line, "(none)");
		} else if (cur && strstr(line, "Candidate:")) {
			cur->available = !strstr(line, "(none)");
		}
	}
	/* Packages apt-cache does not know at all are unavailable. */
	{
		int i;

		for (i = 0; i < NCATALOG; i++)
			catalog[i].known = 1;
	}
	if (store_win)
		wm_damage(store_win);
}

static void query_packages(void)
{
	const char *argv[NCATALOG + 3];
	int i, n = 0;

	if (querying)
		return;
	argv[n++] = "apt-cache";
	argv[n++] = "policy";
	for (i = 0; i < NCATALOG; i++)
		argv[n++] = catalog[i].pkg;
	argv[n] = NULL;
	if (proc_run(argv, 0, parse_policy, NULL, NULL) > 0)
		querying = 1;
}

/* ---------------------------------------------------------------------- */
/* APT jobs                                                               */

static void apt_status(const char *line, void *ctx)
{
	float pct;
	char kind[16];

	(void)ctx;
	/* dlstatus:1:12.5:Retrieving ...   pmstatus:pkg:45.2:Unpacking ... */
	if (sscanf(line, "%15[a-z]:%*[^:]:%f", kind, &pct) == 2) {
		if (!strcmp(kind, "dlstatus"))
			job.progress = pct / 200.0f;
		else if (!strcmp(kind, "pmstatus"))
			job.progress = 0.5f + pct / 200.0f;
	}
	if (store_win)
		wm_damage(store_win);
}

static void last_error(const char *out)
{
	const char *e = NULL, *p = out;

	while ((p = strstr(p, "E: "))) {
		e = p;
		p += 3;
	}
	job.error[0] = 0;
	if (e)
		snprintf(job.error, sizeof(job.error), "%.*s",
			 (int)strcspn(e + 3, "\n"), e + 3);
}

static void job_done(int status, const char *out, void *ctx)
{
	char msg[160];
	int entry = job.entry;

	(void)ctx;
	job.running = 0;
	if (status) {
		last_error(out);
		notify(_("Installation failed"), job.error[0] ? job.error : job.what, NULL);
	} else if (entry >= 0) {
		snprintf(msg, sizeof(msg), _("%s is ready to use"), catalog[entry].name);
		notify(job.what, msg, NULL);
	} else {
		upd.count = 0;
		upd.names[0] = 0;
		notify(_("Software Update"), _("Your computer is up to date"), NULL);
	}
	query_packages();
	if (store_win)
		wm_damage(store_win);
}

static void run_apt(int entry, const char *what, const char *const argv[])
{
	job.running = 1;
	job.entry = entry;
	job.progress = 0;
	job.error[0] = 0;
	snprintf(job.what, sizeof(job.what), "%s", what);
	if (proc_run(argv, PROC_STATUS_FD, job_done, apt_status, NULL) < 0) {
		job.running = 0;
		notify(_("Installation failed"), _("Could not start APT"), NULL);
	}
}

static void install_entry(void)
{
	static char what[96];
	const char *argv[] = { "apt-get", "install", "-y", "-o", "APT::Status-Fd=3",
			       catalog[pending_entry].pkg, NULL };

	snprintf(what, sizeof(what), _("Installing %s"), catalog[pending_entry].name);
	run_apt(pending_entry, what, argv);
}

static void remove_entry(void)
{
	static char what[96];
	const char *argv[] = { "apt-get", "remove", "-y", "-o", "APT::Status-Fd=3",
			       catalog[pending_entry].pkg, NULL };

	snprintf(what, sizeof(what), _("Removing %s"), catalog[pending_entry].name);
	run_apt(pending_entry, what, argv);
}

static void upgrade_all(void)
{
	const char *argv[] = { "apt-get", "upgrade", "-y", "-o", "APT::Status-Fd=3",
			       "-o", "Dpkg::Options::=--force-confold", NULL };

	run_apt(-1, _("Updating software"), argv);
}

/* ---------------------------------------------------------------------- */
/* Administrator password                                                 */

struct auth {
	entry_t pass;
	char error[96];
	int pressed;
};

static void auth_finish(window_t *win, int ok)
{
	void (*next)(void) = after_auth;

	wm_close(win);
	after_auth = NULL;
	if (ok && next) {
		auth_until = dt.now + 300;	/* like sudo: five minutes */
		next();
	}
}

static void auth_try(window_t *win)
{
	struct auth *a = win->data;

	if (user_check_password(dt.user, a->pass.text)) {
		auth_finish(win, 1);
	} else {
		snprintf(a->error, sizeof(a->error), "%s", _("Wrong password. Try again."));
		entry_set(&a->pass, "");
		wm_damage(win);
	}
}

static void auth_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct auth *a = win->data;
	surface_t *ic = surf_new(52, 52);
	char who[128];

	icon_drop(ic, 0, 0, 52);
	blit(s, ic, x + 20, y + 16, 255);
	surf_free(ic);
	draw_text(s, fonts[FONT_UI_BOLD], 14, x + 88, y + 30, 0xff000000,
		  _("Water Candy wants to make changes."));
	snprintf(who, sizeof(who), _("Type the password for %s to allow this."), dt.user);
	draw_text(s, fonts[FONT_UI], 12, x + 88, y + 50, 0xff303030, who);
	draw_text(s, fonts[FONT_UI], 13, x + 88, y + 86, 0xff000000, _("Password:"));
	a->pass.focused = 1;
	entry_draw(s, &a->pass, x + 160, y + 70, w - 190, 22);
	if (a->error[0])
		draw_text(s, fonts[FONT_UI], 12, x + 160, y + 110, 0xffc02020, a->error);
	aqua_button(s, x + w - 200, y + h - 38, 90, 22, "Cancel", 0, a->pressed == 1);
	aqua_button(s, x + w - 100, y + h - 38, 84, 22, "OK", 1, a->pressed == 2);
}

static void auth_mouse(window_t *win, const event_t *ev, int x, int y)
{
	struct auth *a = win->data;
	int cw = win->w - 2, ch = win->h - TITLE_H - 1, b = 0;

	if (ev->type != EV_BUTTON || ev->button != 1)
		return;
	if (y >= ch - 38 && y < ch - 16) {
		if (x >= cw - 200 && x < cw - 110)
			b = 1;
		else if (x >= cw - 100 && x < cw - 16)
			b = 2;
	}
	if (ev->pressed) {
		a->pressed = b;
	} else {
		if (b && b == a->pressed) {
			a->pressed = 0;
			if (b == 1)
				auth_finish(win, 0);
			else
				auth_try(win);
			return;
		}
		a->pressed = 0;
	}
	wm_damage(win);
}

static void auth_key(window_t *win, const event_t *ev)
{
	struct auth *a = win->data;
	int r = entry_key(&a->pass, ev);

	if (r == ENTRY_ENTER)
		auth_try(win);
	else if (r == ENTRY_ESCAPE)
		auth_finish(win, 0);
	else
		wm_damage(win);
}

static void auth_tick(window_t *win)
{
	static int blink;

	if (blink != (int)(dt.now * 2)) {
		blink = (int)(dt.now * 2);
		wm_damage(win);
	}
}

static void auth_destroy(window_t *win)
{
	free(win->data);
}

/* Run next() as root once the user has typed their password. */
static void with_admin(void (*next)(void))
{
	struct auth *a;
	window_t *win;

	if (!user_can_login() || dt.now < auth_until) {
		next();
		return;
	}
	after_auth = next;
	a = calloc(1, sizeof(*a));
	a->pass.password = 1;
	win = wm_create("", APP_ALERT, 470, 200);
	win->data = a;
	win->draw = auth_draw;
	win->mouse = auth_mouse;
	win->key = auth_key;
	win->tick = auth_tick;
	win->destroy = auth_destroy;
}

/* ---------------------------------------------------------------------- */
/* Updates                                                                */

static void open_updates(void)
{
	app_store(1);
}

static void simulate_done(int status, const char *out, void *ctx)
{
	const char *p = out;
	size_t len = 0;

	(void)ctx;
	upd.checking = 0;
	upd.last_check = dt.now;
	if (status)
		return;
	upd.count = 0;
	upd.names[0] = 0;
	/* "Inst <package> [old] (new ...)" lines name the upgrades. */
	while (p && *p) {
		char name[96];

		if (!strncmp(p, "Inst ", 5) && sscanf(p + 5, "%95s", name) == 1) {
			upd.count++;
			if (len + strlen(name) + 2 < sizeof(upd.names))
				len += snprintf(upd.names + len, sizeof(upd.names) - len,
						"%s%s", len ? ", " : "", name);
		}
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	if (upd.count && upd.count != upd.last_notified) {
		char msg[96];

		upd.last_notified = upd.count;
		if (dt.set.lang == LANG_RU)
			snprintf(msg, sizeof(msg), "Доступно %d %s", upd.count,
				 plural(upd.count, "обновление", "обновления", "обновлений"));
		else
			snprintf(msg, sizeof(msg), "%d update%s available", upd.count,
				 upd.count == 1 ? "" : "s");
		notify(_("Software Update"), msg, open_updates);
	}
	if (store_win)
		wm_damage(store_win);
}

static void update_done(int status, const char *out, void *ctx)
{
	const char *argv[] = { "apt-get", "-s", "-o", "Debug::NoLocking=1", "upgrade", NULL };

	(void)status;
	(void)out;
	(void)ctx;
	if (proc_run(argv, 0, simulate_done, NULL, NULL) < 0)
		upd.checking = 0;
}

void updates_check_start(void)
{
	const char *argv[] = { "apt-get", "update", "-q", NULL };

	if (upd.checking || job.running || getuid() != 0 || access("/usr/bin/apt-get", X_OK))
		return;
	upd.checking = 1;
	if (proc_run(argv, 0, update_done, NULL, NULL) < 0)
		upd.checking = 0;
	if (store_win)
		wm_damage(store_win);
}

void updates_tick(void)
{
	if (login_active() || !upd.next_check)
		return;
	if (dt.now >= upd.next_check) {
		upd.next_check = dt.now + 6 * 3600;
		updates_check_start();
	}
}

void updates_schedule(double delay)
{
	upd.next_check = dt.now + delay;
}

/* ---------------------------------------------------------------------- */
/* The window                                                             */

static void draw_app_icon(surface_t *s, const struct app_entry *e, int x, int y, int sz)
{
	paint_t p;

	if (!strcmp(e->glyph, "globe")) {
		icon_globe(s, x, y, sz);
		return;
	}
	if (!strcmp(e->glyph, "terminal")) {
		icon_terminal(s, x, y, sz);
		return;
	}
	p = paint_solid(0x40000000);
	fill_rrect(s, x + 3, y + 5, sz - 6, sz - 6, sz * 0.22f, &p);
	p = paint_vgrad(y, y + sz, color_lerp(e->color, 0xffffffff, 0.35f),
			shade(e->color, 0.8f));
	fill_rrect(s, x + 3, y + 2, sz - 6, sz - 6, sz * 0.22f, &p);
	p = paint_vgrad(y + 2, y + sz * 0.5f, 0xb0ffffff, 0x10ffffff);
	fill_rrect(s, x + 6, y + 4, sz - 12, sz * 0.42f, sz * 0.18f, &p);
	draw_text_center(s, fonts[FONT_UI_BOLD], sz * 0.46f, x, sz, y + sz * 0.68f,
			 0xffffffff, e->glyph);
}

static void progress_bar(surface_t *s, int x, int y, int w, float v)
{
	paint_t p = paint_vgrad(y, y + 12, 0xffc8c8c8, 0xffeeeeee);
	int fw;

	fill_rrect(s, x, y, w, 12, 6, &p);
	if (v < 0)
		v = 0;
	if (v > 1)
		v = 1;
	fw = (int)((w - 2) * v);
	if (fw > 10) {
		p = paint_vgrad(y, y + 12, 0xff8ec0f8, 0xff2a6ad8);
		fill_rrect(s, x + 1, y + 1, fw, 10, 5, &p);
		p = paint_vgrad(y + 1, y + 6, 0xa0ffffff, 0x10ffffff);
		fill_rrect(s, x + 2, y + 1.5f, fw - 2, 4, 2, &p);
	}
	p = paint_solid(0xff7a7a7a);
	stroke_rrect(s, x, y, w, 12, 6, 1, &p);
}

static void segmented(surface_t *s, int x, int y, int w)
{
	const char *labels[2] = { _("Catalog"), _("Updates") };
	char upd_label[64];
	paint_t p;
	int i;

	if (upd.count) {
		snprintf(upd_label, sizeof(upd_label), "%s (%d)", _("Updates"), upd.count);
		labels[1] = upd_label;
	}
	p = paint_solid(0xff8a8a8a);
	fill_rrect(s, x, y, w, 24, 12, &p);
	for (i = 0; i < 2; i++) {
		int sx = x + 1 + i * (w - 2) / 2, sw = (w - 2) / 2;
		uint32_t top = store_tab == i ? 0xff4b8fe6 : 0xffffffff;
		uint32_t bot = store_tab == i ? 0xff8cc4fa : 0xffdcdcdc;

		gfx_push_clip(s, sx, y, sw, 24);
		p = paint_vgrad(y, y + 24, top, bot);
		fill_rrect(s, x + 1, y + 1, w - 2, 22, 11, &p);
		p = paint_vgrad(y + 1, y + 12, 0xc0ffffff, 0x20ffffff);
		fill_rrect(s, x + 3, y + 2, w - 6, 10, 6, &p);
		gfx_pop_clip();
		draw_text_center(s, fonts[FONT_UI], 13, sx, sw, y + 17,
				 store_tab == i ? 0xff07224a : 0xff000000, labels[i]);
	}
	fill_rect(s, x + w / 2, y + 1, 1, 22, 0xff8a8a8a);
}

static void row_button_rect(int w, int i, int *bx, int *by)
{
	*bx = w - BTN_W - 24;
	*by = TOOLBAR_H + i * ROW_H - store_scroll + (ROW_H - 24) / 2;
}

static void draw_catalog(surface_t *s, int x, int y, int w, int h)
{
	int i;

	gfx_push_clip(s, x, y + TOOLBAR_H, w, h - TOOLBAR_H);
	for (i = 0; i < NCATALOG; i++) {
		struct app_entry *e = &catalog[i];
		int ry = y + TOOLBAR_H + i * ROW_H - store_scroll, bx, by;

		if (ry + ROW_H < y + TOOLBAR_H || ry > y + h)
			continue;
		fill_rect(s, x, ry, w, ROW_H, i & 1 ? 0xfff3f6fa : 0xffffffff);
		draw_app_icon(s, e, x + 18, ry + 10, 54);
		draw_text(s, fonts[FONT_UI_BOLD], 15, x + 88, ry + 32, 0xff000000, e->name);
		draw_text(s, fonts[FONT_UI], 12, x + 88, ry + 52, 0xff606060,
			  dt.set.lang == LANG_RU ? e->desc_ru : e->desc_en);
		row_button_rect(w, i, &bx, &by);
		bx += x;
		by += y;
		if (job.running && job.entry == i) {
			progress_bar(s, bx - 20, by + 6, BTN_W + 20, job.progress);
			draw_text(s, fonts[FONT_UI], 11, bx - 20, by + 32, 0xff505050, job.what);
		} else if (!e->known) {
			draw_text(s, fonts[FONT_UI], 12, bx + 20, by + 16, 0xff909090,
				  "\xe2\x80\xa6");
		} else if (e->installed) {
			aqua_button(s, bx, by, BTN_W, 24, "Open", 0, store_pressed == 2 * i);
			draw_text(s, fonts[FONT_UI], 11, bx + 30, by + 40,
				  store_pressed == 2 * i + 1 ? 0xff1a3d8a : 0xff2a5fc8,
				  _("Remove"));
		} else if (e->available) {
			aqua_button(s, bx, by, BTN_W, 24, "Install", 1,
				    store_pressed == 2 * i);
		} else {
			draw_text(s, fonts[FONT_UI], 12, bx + 6, by + 16, 0xff909090,
				  _("Not available"));
		}
		fill_rect(s, x, ry + ROW_H - 1, w, 1, 0x14000000);
	}
	gfx_pop_clip();
}

static void draw_updates(surface_t *s, int x, int y, int w, int h)
{
	char line[160];
	int cy = y + TOOLBAR_H + 40;

	fill_rect(s, x, y + TOOLBAR_H, w, h - TOOLBAR_H, 0xffffffff);
	icon_gear(s, x + 30, cy - 18, 64);
	if (job.running && job.entry < 0) {
		draw_text(s, fonts[FONT_UI_BOLD], 16, x + 112, cy + 10, 0xff000000, job.what);
		progress_bar(s, x + 112, cy + 26, w - 160, job.progress);
		return;
	}
	if (upd.checking) {
		draw_text(s, fonts[FONT_UI_BOLD], 16, x + 112, cy + 10, 0xff000000,
			  _("Checking for updates\xe2\x80\xa6"));
		return;
	}
	if (!upd.count) {
		draw_text(s, fonts[FONT_UI_BOLD], 16, x + 112, cy + 10, 0xff000000,
			  _("Your computer is up to date"));
		draw_text(s, fonts[FONT_UI], 12, x + 112, cy + 30, 0xff606060,
			  upd.last_check ? _("All software is current.") :
			  _("Click Check Now to look for updates."));
		aqua_button(s, x + 112, cy + 46, 150, 24, "Check Now", 0, store_pressed == 1001);
		return;
	}
	if (dt.set.lang == LANG_RU)
		snprintf(line, sizeof(line), "Доступно %d %s", upd.count,
			 plural(upd.count, "обновление", "обновления", "обновлений"));
	else
		snprintf(line, sizeof(line), "%d updates available", upd.count);
	draw_text(s, fonts[FONT_UI_BOLD], 16, x + 112, cy + 10, 0xff000000, line);
	gfx_push_clip(s, x + 112, cy + 16, w - 140, 80);
	{
		/* wrap the package list over a few lines */
		const char *p = upd.names;
		int ly = cy + 32;

		while (*p && ly < cy + 96) {
			int n = 0, last = 0;
			char buf[256];

			while (p[n] && n < 255) {
				snprintf(buf, sizeof(buf), "%.*s", n + 1, p);
				if (text_width(fonts[FONT_UI], 12, buf) > w - 150)
					break;
				if (p[n] == ' ')
					last = n;
				n++;
			}
			if (p[n] && last)
				n = last;
			snprintf(buf, sizeof(buf), "%.*s", n, p);
			draw_text(s, fonts[FONT_UI], 12, x + 112, ly, 0xff505050, buf);
			p += n;
			while (*p == ' ')
				p++;
			ly += 16;
		}
	}
	gfx_pop_clip();
	aqua_button(s, x + 112, cy + 104, 150, 24, "Update All", 1, store_pressed == 1000);
	aqua_button(s, x + 272, cy + 104, 150, 24, "Check Now", 0, store_pressed == 1001);
}

static void store_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	(void)win;
	fill_rect(s, x, y + TOOLBAR_H - 1, w, 1, 0x50000000);
	segmented(s, x + (w - 260) / 2, y + 14, 260);
	if (store_tab == 0)
		draw_catalog(s, x, y, w, h);
	else
		draw_updates(s, x, y, w, h);
	if (job.error[0] && !job.running)
		draw_text(s, fonts[FONT_UI], 11, x + 12, y + h - 8, 0xffc02020, job.error);
}

static int store_hit(window_t *win, int x, int y)
{
	int w = win->w - 2, i;

	if (y < TOOLBAR_H) {
		int sx = (w - 260) / 2;

		if (y >= 14 && y < 38 && x >= sx && x < sx + 260)
			return x < sx + 130 ? 2000 : 2001;
		return -1;
	}
	if (store_tab == 1) {
		int cy = TOOLBAR_H + 40;

		if (upd.count && y >= cy + 104 && y < cy + 128) {
			if (x >= 112 && x < 262)
				return 1000;
			if (x >= 272 && x < 422)
				return 1001;
		}
		if (!upd.count && y >= cy + 46 && y < cy + 70 && x >= 112 && x < 262)
			return 1001;
		return -1;
	}
	for (i = 0; i < NCATALOG; i++) {
		int bx, by;

		row_button_rect(w, i, &bx, &by);
		if (x >= bx && x < bx + BTN_W && y >= by && y < by + 24)
			return 2 * i;
		if (catalog[i].installed && x >= bx + 20 && x < bx + 100 &&
		    y >= by + 28 && y < by + 44)
			return 2 * i + 1;
	}
	return -1;
}

static void open_entry(int i)
{
	struct app_entry *e = &catalog[i];

	if (!strcmp(e->pkg, "firefox"))
		app_firefox();
	else if (e->terminal)
		app_terminal_cmd(e->run);
	else
		app_x11(e->run, e->name, APP_X11);
}

static void store_click(int hit)
{
	if (hit == 2000 || hit == 2001) {
		store_tab = hit - 2000;
		store_scroll = 0;
		if (store_tab == 1 && !upd.last_check)
			updates_check_start();
		return;
	}
	if (hit == 1000 && !job.running) {
		with_admin(upgrade_all);
		return;
	}
	if (hit == 1001) {
		updates_check_start();
		return;
	}
	if (hit >= 0 && hit < 2 * NCATALOG) {
		int i = hit / 2;

		if (catalog[i].installed && !(hit & 1)) {
			open_entry(i);
		} else if (job.running) {
			notify(_("Please wait"), _("Another installation is in progress."), NULL);
		} else if (catalog[i].installed) {
			pending_entry = i;
			with_admin(remove_entry);
		} else if (catalog[i].available) {
			pending_entry = i;
			with_admin(install_entry);
		}
	}
}

static void store_mouse(window_t *win, const event_t *ev, int x, int y)
{
	if (ev->type == EV_SCROLL) {
		int max = NCATALOG * ROW_H - (win->h - TITLE_H - 1 - TOOLBAR_H);

		store_scroll -= ev->dz * 40;
		if (store_scroll > max)
			store_scroll = max;
		if (store_scroll < 0)
			store_scroll = 0;
		wm_damage(win);
		return;
	}
	if (ev->type != EV_BUTTON || ev->button != 1)
		return;
	if (ev->pressed) {
		store_pressed = store_hit(win, x, y);
	} else {
		int hit = store_hit(win, x, y);

		if (hit >= 0 && hit == store_pressed)
			store_click(hit);
		store_pressed = -1;
	}
	wm_damage(win);
}

static void store_tick(window_t *win)
{
	static double last;

	/* keep progress bars moving */
	if ((job.running || upd.checking) && dt.now - last > 0.25) {
		last = dt.now;
		wm_damage(win);
	}
}

static void store_destroy(window_t *win)
{
	(void)win;
	store_win = NULL;
}

void app_store(int updates_tab)
{
	if (store_win) {
		if (store_win->minimized)
			wm_restore(store_win);
		wm_focus(store_win);
	} else {
		store_win = wm_create("App Store", APP_STORE, STORE_W, STORE_H);
		store_win->draw = store_draw;
		store_win->mouse = store_mouse;
		store_win->tick = store_tick;
		store_win->destroy = store_destroy;
		store_win->resizable = 1;
		store_win->min_w = 560;
		store_win->min_h = 320;
	}
	store_tab = updates_tab ? 1 : 0;
	query_packages();
	if (store_tab == 1 && !upd.last_check)
		updates_check_start();
}
