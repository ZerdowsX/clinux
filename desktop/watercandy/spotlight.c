// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Spotlight: find programs (built in, or installed with APT
 * and described by .desktop files) and files in the home folder.
 */
#include <dirent.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <wchar.h>
#include <wctype.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define MENU_W 420
#define HEAD_H 46
#define ROW_H 22
#define GROUP_H 20
#define MAX_RESULTS 14
#define MAX_APPS 256

enum kind { R_APP, R_FILE, R_FOLDER };

struct result {
	enum kind kind;
	char name[128];
	char detail[PATH_MAX];
	char exec[512];
	int terminal;
	enum app_id app;
};

struct app_def {
	char name[128], name_ru[128], exec[512];
	int terminal;
	enum app_id app;
};

static struct app_def apps[MAX_APPS];
static int napps;
static double apps_loaded = -1000;
static entry_t field = { .placeholder = "Spotlight" };
static struct result res[MAX_RESULTS];
static int nres, hover = -1;
static surface_t *ic_app, *ic_doc, *ic_folder;

struct menu_custom spotlight_menu;

/* ---------------------------------------------------------------------- */

static void lower(const char *in, wchar_t *out, size_t n)
{
	size_t i, k = mbstowcs(out, in, n - 1);

	if (k == (size_t)-1)
		k = 0;
	out[k] = 0;
	for (i = 0; i < k; i++)
		out[i] = towlower(out[i]);
}

static int matches(const char *hay, const wchar_t *needle)
{
	wchar_t h[512];

	lower(hay, h, 512);
	return wcsstr(h, needle) != NULL;
}

static void add_builtin(const char *name, enum app_id app)
{
	struct app_def *a = &apps[napps++];

	memset(a, 0, sizeof(*a));
	snprintf(a->name, sizeof(a->name), "%s", name);
	a->app = app;
}

/* Exec= without the %f/%U field codes. */
static void clean_exec(char *s)
{
	char *r = s, *w = s;

	while (*r) {
		if (r[0] == '%' && r[1]) {
			if (r[1] == '%')
				*w++ = '%';
			r += 2;
		} else {
			*w++ = *r++;
		}
	}
	while (w > s && w[-1] == ' ')
		w--;
	*w = 0;
}

static void load_desktop_files(void)
{
	DIR *d = opendir("/usr/share/applications");
	struct dirent *de;

	napps = 0;
	add_builtin("Files", APP_FILES);
	add_builtin("Terminal", APP_TERMINAL);
	add_builtin("TextEdit", APP_TEXTEDIT);
	add_builtin("App Store", APP_STORE);
	add_builtin("System Preferences", APP_PREFS);
	add_builtin("Clock", APP_CLOCK);
	add_builtin("About This Computer", APP_ABOUT);
	if (firefox_path())
		add_builtin("Firefox", APP_FIREFOX);
	apps_loaded = dt.now;
	while (d && (de = readdir(d)) && napps < MAX_APPS) {
		char path[PATH_MAX], line[1024];
		struct app_def a = { 0 };
		int in_entry = 0, hidden = 0;
		size_t n = strlen(de->d_name);
		FILE *f;

		if (n < 9 || strcmp(de->d_name + n - 8, ".desktop") ||
		    !strncmp(de->d_name, "firefox", 7))
			continue;
		snprintf(path, sizeof(path), "/usr/share/applications/%s", de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (line[0] == '[') {
				in_entry = !strcmp(line, "[Desktop Entry]");
				continue;
			}
			if (!in_entry)
				continue;
			if (!strncmp(line, "Name=", 5))
				snprintf(a.name, sizeof(a.name), "%.127s", line + 5);
			else if (!strncmp(line, "Name[ru]=", 9))
				snprintf(a.name_ru, sizeof(a.name_ru), "%.127s", line + 9);
			else if (!strncmp(line, "Exec=", 5))
				snprintf(a.exec, sizeof(a.exec), "%.511s", line + 5);
			else if (!strcmp(line, "Terminal=true"))
				a.terminal = 1;
			else if (!strcmp(line, "NoDisplay=true") || !strcmp(line, "Hidden=true"))
				hidden = 1;
			else if (!strncmp(line, "Type=", 5) && strcmp(line + 5, "Application"))
				hidden = 1;
		}
		fclose(f);
		if (hidden || !a.name[0] || !a.exec[0])
			continue;
		clean_exec(a.exec);
		a.app = APP_X11;
		apps[napps++] = a;
	}
	if (d)
		closedir(d);
}

static void add_result(enum kind kind, const char *name, const char *detail,
		       const struct app_def *a)
{
	struct result *r;

	if (nres >= MAX_RESULTS)
		return;
	r = &res[nres++];
	memset(r, 0, sizeof(*r));
	r->kind = kind;
	snprintf(r->name, sizeof(r->name), "%s", name);
	snprintf(r->detail, sizeof(r->detail), "%s", detail ? detail : "");
	if (a) {
		snprintf(r->exec, sizeof(r->exec), "%s", a->exec);
		r->terminal = a->terminal;
		r->app = a->app;
	}
}

static void search_files(const char *dir, const wchar_t *q, int depth, int *budget)
{
	DIR *d;
	struct dirent *de;

	if (depth > 5 || *budget <= 0 || nres >= MAX_RESULTS)
		return;
	d = opendir(dir);
	if (!d)
		return;
	while ((de = readdir(d)) && *budget > 0 && nres < MAX_RESULTS) {
		char path[PATH_MAX];
		struct stat st;

		if (de->d_name[0] == '.')
			continue;
		(*budget)--;
		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (lstat(path, &st))
			continue;
		if (matches(de->d_name, q))
			add_result(S_ISDIR(st.st_mode) ? R_FOLDER : R_FILE, de->d_name, path, NULL);
		if (S_ISDIR(st.st_mode))
			search_files(path, q, depth + 1, budget);
	}
	closedir(d);
}

static int nres_apps;

static void search(void)
{
	wchar_t q[256];
	int i, budget = 4000;

	nres = 0;
	hover = 0;
	if (!field.text[0]) {
		nres_apps = 0;
		return;
	}
	if (dt.now - apps_loaded > 30)
		load_desktop_files();
	lower(field.text, q, 256);
	for (i = 0; i < napps && nres < 7; i++) {
		const char *shown = apps[i].app == APP_X11 ? apps[i].name : _(apps[i].name);

		if (matches(apps[i].name, q) || matches(_(apps[i].name), q) ||
		    (apps[i].name_ru[0] && matches(apps[i].name_ru, q)))
			add_result(R_APP, dt.set.lang == LANG_RU && apps[i].name_ru[0] ?
				   apps[i].name_ru : shown, NULL, &apps[i]);
	}
	nres_apps = nres;
	search_files(dt.home, q, 0, &budget);
	if (!nres)
		hover = -1;
}

/* ---------------------------------------------------------------------- */

static int content_h(void)
{
	int h = HEAD_H;

	if (!field.text[0])
		return h;
	if (!nres)
		return h + 34;
	if (nres_apps)
		h += GROUP_H + nres_apps * ROW_H;
	if (nres > nres_apps)
		h += GROUP_H + (nres - nres_apps) * ROW_H;
	return h + 6;
}

static int row_y(int i)
{
	int y = HEAD_H;

	if (nres_apps) {
		y += GROUP_H;
		if (i < nres_apps)
			return y + i * ROW_H;
		y += nres_apps * ROW_H;
	}
	return y + GROUP_H + (i - nres_apps) * ROW_H;
}

static void spot_draw(surface_t *s, int w, int h)
{
	paint_t p;
	int i;

	(void)h;
	if (!ic_app) {
		ic_app = surf_new(18, 18);
		icon_app(ic_app, 0, 0, 18);
		ic_doc = surf_new(18, 18);
		icon_document(ic_doc, 0, 0, 18);
		ic_folder = surf_new(18, 18);
		icon_folder(ic_folder, 0, 0, 18);
	}
	p = paint_vgrad(0, HEAD_H, 0xfff0f4fa, 0xffdfe7f2);
	fill_rect_paint(s, 1, 0, w - 2, HEAD_H, &p);
	draw_text(s, fonts[FONT_UI_BOLD], 13, 14, 28, 0xff2a4a7a, "Spotlight");
	field.focused = 1;
	entry_draw(s, &field, 100, 12, w - 116, 22);
	fill_rect(s, 1, HEAD_H - 1, w - 2, 1, 0x30000000);
	if (field.text[0] && !nres)
		draw_text(s, fonts[FONT_UI], 13, 16, HEAD_H + 22, 0xff808080,
			  _("No results"));
	for (i = 0; i < nres; i++) {
		int y = row_y(i);

		if (i == 0 && nres_apps)
			draw_text(s, fonts[FONT_UI_BOLD], 11, 12, y - 6, 0xff707070,
				  _("Applications"));
		if (i == nres_apps)
			draw_text(s, fonts[FONT_UI_BOLD], 11, 12, y - 6, 0xff707070,
				  _("Documents"));
		if (i == hover)
			aqua_highlight(s, 1, y, w - 2, ROW_H, 0);
		blit(s, res[i].kind == R_APP ? ic_app : res[i].kind == R_FOLDER ? ic_folder :
		     ic_doc, 14, y + 2, 255);
		gfx_push_clip(s, 0, y, w - 8, ROW_H);
		draw_text(s, fonts[FONT_UI], 13, 40, y + 16,
			  i == hover ? 0xffffffff : 0xff000000, res[i].name);
		if (res[i].kind != R_APP) {
			const char *dir = res[i].detail;
			char shown[PATH_MAX];
			int nx = 40 + text_width(fonts[FONT_UI], 13, res[i].name) + 10;

			if (!strncmp(dir, dt.home, strlen(dt.home)))
				snprintf(shown, sizeof(shown), "~%s", dir + strlen(dt.home));
			else
				snprintf(shown, sizeof(shown), "%s", dir);
			draw_text(s, fonts[FONT_UI], 11, nx, y + 16,
				  i == hover ? 0xffd8e6ff : 0xff909090, shown);
		}
		gfx_pop_clip();
	}
}

static void open_result(int i)
{
	struct result *r = &res[i];

	if (r->kind != R_APP)
		wc_open_file(r->detail);
	else if (r->app == APP_X11 && r->terminal)
		app_terminal_cmd(r->exec);
	else if (r->app == APP_X11)
		app_x11(r->exec, r->name, APP_X11);
	else if (r->app == APP_TEXTEDIT)
		app_textedit(NULL);
	else
		app_launch(r->app);
	entry_set(&field, "");
	nres = 0;
}

static int spot_mouse(const event_t *ev, int x, int y)
{
	int i, at = -1;

	(void)x;
	for (i = 0; i < nres; i++)
		if (y >= row_y(i) && y < row_y(i) + ROW_H)
			at = i;
	if (ev->type == EV_MOTION && at >= 0)
		hover = at;
	if (ev->type == EV_BUTTON && !ev->pressed && at >= 0) {
		open_result(at);
		return 1;
	}
	return 0;
}

static int spot_key(const event_t *ev)
{
	int r;

	if (ev->key == KEY_DOWN && nres) {
		hover = (hover + 1) % nres;
		return 0;
	}
	if (ev->key == KEY_UP && nres) {
		hover = (hover + nres - 1) % nres;
		return 0;
	}
	r = entry_key(&field, ev);
	if (r == ENTRY_ENTER) {
		if (hover >= 0 && hover < nres) {
			open_result(hover);
			return 1;
		}
		return 0;
	}
	if (r == ENTRY_CHANGED)
		search();
	spotlight_menu.h = content_h();
	return 0;
}

static void spot_opened(void)
{
	setlocale(LC_CTYPE, "C.UTF-8");
	spotlight_menu.h = content_h();
}

struct menu_custom spotlight_menu = {
	.w = MENU_W, .h = HEAD_H, .draw = spot_draw, .mouse = spot_mouse,
	.key = spot_key, .opened = spot_opened,
};

void spotlight_menu_icon(surface_t *s, int x, int w, int on)
{
	uint32_t c = on ? 0xffffffff : 0xff000000;
	paint_t p = paint_solid(c);
	float cx = x + w / 2.0f - 2, cy = 9.5f;

	stroke_circle(s, cx, cy, 5, 1.8f, &p);
	draw_line(s, cx + 3.6f, cy + 3.6f, cx + 8, cy + 8, 2.4f, &p);
}
