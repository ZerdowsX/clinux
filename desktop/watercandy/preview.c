// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Preview: an image viewer. Left/right show the other images
 * in the same folder.
 */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <linux/input-event-codes.h>

#include "wc.h"

struct viewer {
	char dir[PATH_MAX];
	char **files;
	int n, cur;
	surface_t *img;
};

int is_image_file(const char *name)
{
	static const char *const ext[] = { ".png", ".jpg", ".jpeg", ".gif", ".bmp", NULL };
	size_t n = strlen(name);
	int i;

	for (i = 0; ext[i]; i++) {
		size_t e = strlen(ext[i]);

		if (n > e && !strcasecmp(name + n - e, ext[i]))
			return 1;
	}
	return 0;
}

static int cmp(const void *a, const void *b)
{
	return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static void load_current(window_t *win, struct viewer *v)
{
	char path[PATH_MAX + 260], title[400];
	size_t len = 0;
	char *data;

	surf_free(v->img);
	v->img = NULL;
	snprintf(path, sizeof(path), "%s/%s", v->dir, v->files[v->cur]);
	data = user_read_file_len(path, &len);
	if (data) {
		v->img = image_load_mem((const unsigned char *)data, len);
		free(data);
	}
	if (dt.set.lang == LANG_RU)
		snprintf(title, sizeof(title), "%s (%d из %d)", v->files[v->cur], v->cur + 1, v->n);
	else
		snprintf(title, sizeof(title), "%s (%d of %d)", v->files[v->cur], v->cur + 1, v->n);
	wm_set_title(win, title);
}

static void pv_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct viewer *v = win->data;
	float k, dw, dh;

	fill_rect(s, x, y, w, h, 0xff2b2b2b);
	if (!v->img) {
		draw_text_center(s, fonts[FONT_UI], 13, x, w, y + h / 2, 0xffc0c0c0,
				 _("This image cannot be opened."));
		return;
	}
	k = (float)w / v->img->w;
	if ((float)h / v->img->h < k)
		k = (float)h / v->img->h;
	if (k > 1)
		k = 1;		/* never enlarge small pictures */
	dw = v->img->w * k;
	dh = v->img->h * k;
	blit_scaled(s, v->img, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh, 255);
}

static void pv_key(window_t *win, const event_t *ev)
{
	struct viewer *v = win->data;

	if (ev->key == KEY_RIGHT || ev->key == KEY_DOWN || ev->key == KEY_SPACE)
		v->cur = (v->cur + 1) % v->n;
	else if (ev->key == KEY_LEFT || ev->key == KEY_UP)
		v->cur = (v->cur + v->n - 1) % v->n;
	else
		return;
	load_current(win, v);
	wm_damage(win);
}

static void pv_destroy(window_t *win)
{
	struct viewer *v = win->data;
	int i;

	for (i = 0; i < v->n; i++)
		free(v->files[i]);
	free(v->files);
	surf_free(v->img);
	free(v);
}

void app_preview(const char *path)
{
	struct viewer *v = calloc(1, sizeof(*v));
	const char *slash = strrchr(path, '/');
	const char *name = slash ? slash + 1 : path;
	window_t *win;
	struct dirent *de;
	DIR *d;
	int w = 800, h = 600, i;

	snprintf(v->dir, sizeof(v->dir), "%.*s", slash ? (int)(slash - path) : 1,
		 slash ? path : ".");
	if (!v->dir[0])
		snprintf(v->dir, sizeof(v->dir), "/");
	d = opendir(v->dir);
	while (d && (de = readdir(d))) {
		if (de->d_name[0] == '.' || !is_image_file(de->d_name))
			continue;
		v->files = realloc(v->files, sizeof(char *) * (v->n + 1));
		v->files[v->n++] = strdup(de->d_name);
	}
	if (d)
		closedir(d);
	if (!v->n) {
		v->files = malloc(sizeof(char *));
		v->files[v->n++] = strdup(name);
	}
	qsort(v->files, v->n, sizeof(char *), cmp);
	for (i = 0; i < v->n; i++)
		if (!strcmp(v->files[i], name))
			v->cur = i;

	win = wm_create("Preview", APP_PREVIEW, w, h);
	win->resizable = 1;
	win->min_w = 200;
	win->min_h = 150;
	win->data = v;
	win->draw = pv_draw;
	win->key = pv_key;
	win->destroy = pv_destroy;
	load_current(win, v);
	if (v->img) {
		/* size the window to the picture, within the screen */
		int mw = dt.w - 160, mh = dt.h - MENUBAR_H - 160;
		float k = 1;

		if (v->img->w > mw)
			k = (float)mw / v->img->w;
		if (v->img->h * k > mh)
			k = (float)mh / v->img->h;
		win->w = (int)(v->img->w * k) + 2;
		win->h = (int)(v->img->h * k) + TITLE_H + 1;
		if (win->w < 300)
			win->w = 300;
		if (win->h < 200)
			win->h = 200;
		win->x = (dt.w - win->w) / 2;
	}
}
