// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Files: a simple icon-view file browser.
 */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "wc.h"

#define TOOLBAR_H 40
#define STATUS_H 20
#define CELL_W 96
#define CELL_H 84
#define ICON 48

struct entry {
	char name[256];
	int is_dir, is_exec;
};

struct files {
	char path[PATH_MAX];
	char back[PATH_MAX];
	struct entry *ents;
	int n, sel, scroll;
	double last_click;
	int pressed_btn;
};

static surface_t *ic_folder, *ic_doc, *ic_exec;

static int cmp_ent(const void *a, const void *b)
{
	const struct entry *x = a, *y = b;

	return strcasecmp(x->name, y->name);
}

static void load_dir(struct files *f)
{
	DIR *d = opendir(f->path);
	struct dirent *de;
	int cap = 64;

	free(f->ents);
	f->ents = malloc(sizeof(*f->ents) * cap);
	f->n = 0;
	f->sel = -1;
	f->scroll = 0;
	if (!d)
		return;
	while ((de = readdir(d))) {
		char full[PATH_MAX + 260];
		struct stat st;
		struct entry *e;

		if (de->d_name[0] == '.')
			continue;
		if (f->n == cap) {
			cap *= 2;
			f->ents = realloc(f->ents, sizeof(*f->ents) * cap);
		}
		e = &f->ents[f->n++];
		snprintf(e->name, sizeof(e->name), "%s", de->d_name);
		snprintf(full, sizeof(full), "%s/%s", f->path, de->d_name);
		e->is_dir = !stat(full, &st) && S_ISDIR(st.st_mode);
		e->is_exec = !e->is_dir && !stat(full, &st) && (st.st_mode & 0111);
	}
	closedir(d);
	qsort(f->ents, f->n, sizeof(*f->ents), cmp_ent);
}

static void set_path(window_t *win, struct files *f, const char *path)
{
	char real[PATH_MAX];
	const char *name;

	if (!realpath(path, real))
		snprintf(real, sizeof(real), "%s", path);
	snprintf(f->back, sizeof(f->back), "%s", f->path);
	snprintf(f->path, sizeof(f->path), "%s", real);
	load_dir(f);
	name = strrchr(f->path, '/');
	name = (name && name[1]) ? name + 1 : "Water HD";
	if (!strcmp(f->path, "/root"))
		name = "Home";
	wm_set_title(win, name);
}

static int cols_for(int w)
{
	int c = (w - 16) / CELL_W;

	return c < 1 ? 1 : c;
}

static void ellipsize(char *out, size_t sz, const char *in, int maxw)
{
	size_t n = strlen(in);

	snprintf(out, sz, "%s", in);
	if (text_width(fonts[FONT_UI], 12, out) <= maxw)
		return;
	while (n > 1) {
		n--;
		snprintf(out, sz, "%.*s\xe2\x80\xa6", (int)n, in);
		if (text_width(fonts[FONT_UI], 12, out) <= maxw)
			return;
	}
}

static void files_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct files *f = win->data;
	int cols = cols_for(w), i, gy = y + TOOLBAR_H;
	char label[300], status[96];
	paint_t p;

	/* toolbar on the window's pinstripes */
	aqua_button(s, x + 10, y + 9, 54, 22, "Back", 0, f->pressed_btn == 0);
	aqua_button(s, x + 70, y + 9, 46, 22, "Up", 0, f->pressed_btn == 1);
	aqua_button(s, x + 122, y + 9, 62, 22, "Home", 0, f->pressed_btn == 2);
	gfx_clip(s, x + 196, y, w - 200, TOOLBAR_H);
	draw_text(s, fonts[FONT_UI], 12, x + 198, y + 25, 0xff404040, f->path);
	gfx_clip(s, x, y, w, h);
	fill_rect(s, x, gy - 1, w, 1, 0x50000000);

	fill_rect(s, x, gy, w, h - TOOLBAR_H - STATUS_H, 0xffffffff);
	gfx_clip(s, x, gy, w, h - TOOLBAR_H - STATUS_H);
	for (i = 0; i < f->n; i++) {
		int cx = x + 8 + (i % cols) * CELL_W;
		int cy = gy + 8 + (i / cols) * CELL_H - f->scroll;
		struct entry *e = &f->ents[i];
		surface_t *ic = e->is_dir ? ic_folder : e->is_exec ? ic_exec : ic_doc;
		int tw;

		if (cy > y + h || cy + CELL_H < gy)
			continue;
		if (i == f->sel) {
			p = paint_solid(0x30000000);
			fill_rrect(s, cx + (CELL_W - ICON) / 2 - 4, cy - 2, ICON + 8,
				   ICON + 6, 5, &p);
		}
		blit(s, ic, cx + (CELL_W - ICON) / 2, cy, 255);
		ellipsize(label, sizeof(label), e->name, CELL_W - 8);
		tw = text_width(fonts[FONT_UI], 12, label);
		if (i == f->sel) {
			p = paint_solid(0xff2a5fc8);
			fill_rrect(s, cx + (CELL_W - tw) / 2 - 4, cy + ICON + 4, tw + 8, 16,
				   8, &p);
		}
		draw_text(s, fonts[FONT_UI], 12, cx + (CELL_W - tw) / 2, cy + ICON + 16,
			  i == f->sel ? 0xffffffff : 0xff000000, label);
	}
	if (!f->n)
		draw_text_center(s, fonts[FONT_UI], 13, x, w, gy + 40, 0xff909090,
				 "This folder is empty");
	gfx_clip(s, x, y, w, h);

	fill_rect(s, x, y + h - STATUS_H, w, 1, 0x40000000);
	snprintf(status, sizeof(status), "%d item%s", f->n, f->n == 1 ? "" : "s");
	draw_text_center(s, fonts[FONT_UI], 11, x, w, y + h - 6, 0xff404040, status);
}

static int max_scroll(window_t *win, struct files *f)
{
	int cols = cols_for(win->w - 2);
	int rows = (f->n + cols - 1) / cols;
	int view = win->h - TITLE_H - 1 - TOOLBAR_H - STATUS_H;
	int m = rows * CELL_H + 16 - view;

	return m > 0 ? m : 0;
}

static void files_mouse(window_t *win, const event_t *ev, int x, int y)
{
	struct files *f = win->data;
	int cols = cols_for(win->w - 2), idx;

	if (ev->type == EV_SCROLL) {
		f->scroll -= ev->dz * 40;
		if (f->scroll < 0)
			f->scroll = 0;
		if (f->scroll > max_scroll(win, f))
			f->scroll = max_scroll(win, f);
		wm_damage(win);
		return;
	}
	if (ev->type != EV_BUTTON)
		return;
	if (!ev->pressed) {
		int b = f->pressed_btn;

		f->pressed_btn = -1;
		if (b >= 0 && y >= 9 && y < 31) {
			if (b == 0 && f->back[0]) {
				char tmp[PATH_MAX];

				snprintf(tmp, sizeof(tmp), "%s", f->back);
				set_path(win, f, tmp);
			} else if (b == 1) {
				char tmp[PATH_MAX + 4];

				snprintf(tmp, sizeof(tmp), "%s/..", f->path);
				set_path(win, f, tmp);
			} else if (b == 2) {
				set_path(win, f, "/root");
			}
		}
		wm_damage(win);
		return;
	}
	if (y < TOOLBAR_H) {
		if (y >= 9 && y < 31) {
			if (x >= 10 && x < 64)
				f->pressed_btn = 0;
			else if (x >= 70 && x < 116)
				f->pressed_btn = 1;
			else if (x >= 122 && x < 184)
				f->pressed_btn = 2;
		}
		wm_damage(win);
		return;
	}
	idx = -1;
	if (x >= 8 && x < 8 + cols * CELL_W) {
		int col = (x - 8) / CELL_W;
		int row = (y - TOOLBAR_H - 8 + f->scroll) / CELL_H;

		if (y - TOOLBAR_H - 8 + f->scroll >= 0)
			idx = row * cols + col;
		if (idx >= f->n)
			idx = -1;
	}
	if (idx >= 0 && idx == f->sel && dt.now - f->last_click < 0.5 &&
	    f->ents[idx].is_dir) {
		char tmp[PATH_MAX + 260];

		snprintf(tmp, sizeof(tmp), "%s/%s", strcmp(f->path, "/") ? f->path : "",
			 f->ents[idx].name);
		set_path(win, f, tmp);
		f->last_click = 0;
	} else {
		f->sel = idx;
		f->last_click = dt.now;
	}
	wm_damage(win);
}

static void files_destroy(window_t *win)
{
	struct files *f = win->data;

	free(f->ents);
	free(f);
}

void app_files(const char *path, const char *title)
{
	struct files *f = calloc(1, sizeof(*f));
	window_t *win;

	if (!ic_folder) {
		ic_folder = surf_new(ICON, ICON);
		icon_folder(ic_folder, 0, 0, ICON);
		ic_doc = surf_new(ICON, ICON);
		icon_document(ic_doc, 0, 0, ICON);
		ic_exec = surf_new(ICON, ICON);
		icon_app(ic_exec, 0, 0, ICON);
	}
	mkdir(path, 0755);	/* e.g. the Trash folder */
	win = wm_create(title, APP_FILES, 600, 420);
	win->resizable = 1;
	win->min_w = 260;
	win->min_h = 180;
	win->data = f;
	win->draw = files_draw;
	win->mouse = files_mouse;
	win->destroy = files_destroy;
	f->pressed_btn = -1;
	set_path(win, f, path);
	f->back[0] = 0;
	if (title)
		wm_set_title(win, title);
}
