// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - TextEdit: a plain text editor.
 *
 * Files are read and written as the desktop user (not as root), so the
 * editor cannot touch files the user could not.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define LINE_H 19
#define PAD 10
#define FSIZE 14

struct doc {
	char **lines;
	int n, cap;
	int cl, cc;		/* cursor: line, byte column */
	int al, ac, sel;	/* selection anchor */
	int scroll, dragging;
	char path[PATH_MAX];
	int dirty;
};

static void set_lines(struct doc *d, const char *text)
{
	const char *p = text;
	int i;

	for (i = 0; i < d->n; i++)
		free(d->lines[i]);
	d->n = 0;
	do {
		const char *nl = strchr(p, '\n');
		size_t len = nl ? (size_t)(nl - p) : strlen(p);

		if (d->n == d->cap) {
			d->cap = d->cap ? d->cap * 2 : 64;
			d->lines = realloc(d->lines, sizeof(char *) * d->cap);
		}
		d->lines[d->n] = malloc(len + 1);
		memcpy(d->lines[d->n], p, len);
		d->lines[d->n][len] = 0;
		/* tabs are shown as four spaces */
		d->n++;
		p = nl ? nl + 1 : NULL;
	} while (p && (*p || p[-1] == '\n'));
	if (d->n > 1 && !d->lines[d->n - 1][0] && text[0] &&
	    text[strlen(text) - 1] == '\n') {
		free(d->lines[--d->n]);	/* the final newline is not a line */
	}
	d->cl = d->cc = 0;
	d->sel = 0;
}

static char *join_lines(const struct doc *d, int l1, int c1, int l2, int c2)
{
	size_t len = 0, o = 0;
	char *out;
	int i;

	for (i = l1; i <= l2; i++)
		len += strlen(d->lines[i]) + 1;
	out = malloc(len + 1);
	for (i = l1; i <= l2; i++) {
		const char *s = d->lines[i];
		int from = i == l1 ? c1 : 0, to = i == l2 ? c2 : (int)strlen(s);

		memcpy(out + o, s + from, to - from);
		o += to - from;
		if (i != l2)
			out[o++] = '\n';
	}
	out[o] = 0;
	return out;
}

static void sel_range(const struct doc *d, int *l1, int *c1, int *l2, int *c2)
{
	if (d->al < d->cl || (d->al == d->cl && d->ac < d->cc)) {
		*l1 = d->al, *c1 = d->ac, *l2 = d->cl, *c2 = d->cc;
	} else {
		*l1 = d->cl, *c1 = d->cc, *l2 = d->al, *c2 = d->ac;
	}
}

static void delete_range(struct doc *d, int l1, int c1, int l2, int c2)
{
	char *tail = strdup(d->lines[l2] + c2);
	int i;

	d->lines[l1][c1] = 0;
	d->lines[l1] = realloc(d->lines[l1], c1 + strlen(tail) + 1);
	strcat(d->lines[l1], tail);
	free(tail);
	for (i = l1 + 1; i <= l2; i++)
		free(d->lines[i]);
	memmove(d->lines + l1 + 1, d->lines + l2 + 1, sizeof(char *) * (d->n - l2 - 1));
	d->n -= l2 - l1;
	d->cl = l1;
	d->cc = c1;
	d->dirty = 1;
}

static void delete_selection(struct doc *d)
{
	int l1, c1, l2, c2;

	if (!d->sel)
		return;
	sel_range(d, &l1, &c1, &l2, &c2);
	delete_range(d, l1, c1, l2, c2);
	d->sel = 0;
}

static void insert_text(struct doc *d, const char *text)
{
	delete_selection(d);
	while (*text) {
		const char *nl = strchr(text, '\n');
		size_t len = nl ? (size_t)(nl - text) : strlen(text);
		char *line = d->lines[d->cl];
		size_t ll = strlen(line);

		line = realloc(line, ll + len + 1);
		memmove(line + d->cc + len, line + d->cc, ll - d->cc + 1);
		memcpy(line + d->cc, text, len);
		d->lines[d->cl] = line;
		d->cc += len;
		text += len;
		if (nl) {
			if (d->n == d->cap) {
				d->cap *= 2;
				d->lines = realloc(d->lines, sizeof(char *) * d->cap);
			}
			memmove(d->lines + d->cl + 2, d->lines + d->cl + 1,
				sizeof(char *) * (d->n - d->cl - 1));
			d->lines[d->cl + 1] = strdup(line + d->cc);
			line[d->cc] = 0;
			d->n++;
			d->cl++;
			d->cc = 0;
			text++;
		}
	}
	d->dirty = 1;
}

static int prev_char(const char *s, int i)
{
	while (i > 0 && (s[--i] & 0xc0) == 0x80)
		;
	return i;
}

static int next_char(const char *s, int i)
{
	int n = strlen(s);

	if (i < n)
		i++;
	while (i < n && (s[i] & 0xc0) == 0x80)
		i++;
	return i;
}

static int width_upto(const char *s, int bytes)
{
	char buf[4096];

	snprintf(buf, sizeof(buf), "%.*s", bytes, s);
	return text_width(fonts[FONT_UI], FSIZE, buf);
}

static int col_at(const char *s, int px)
{
	int i = 0, n = strlen(s);

	while (i < n) {
		int j = next_char(s, i);

		if ((width_upto(s, i) + width_upto(s, j)) / 2 > px)
			break;
		i = j;
	}
	return i;
}

static void update_title(window_t *win, struct doc *d)
{
	const char *base = strrchr(d->path, '/');

	wm_set_title(win, d->path[0] ? (base ? base + 1 : d->path) : _("Untitled"));
	win->edited = d->dirty;
}

static void te_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct doc *d = win->data;
	int i, l1 = 0, c1 = 0, l2 = -1, c2 = 0, active = win == dt.focus;

	fill_rect(s, x, y, w, h, 0xffffffff);
	if (d->sel)
		sel_range(d, &l1, &c1, &l2, &c2);
	gfx_push_clip(s, x, y, w, h);
	for (i = d->scroll; i < d->n && (i - d->scroll) * LINE_H < h; i++) {
		int ly = y + PAD + (i - d->scroll) * LINE_H;

		if (d->sel && i >= l1 && i <= l2) {
			int sx = i == l1 ? width_upto(d->lines[i], c1) : 0;
			int ex = i == l2 ? width_upto(d->lines[i], c2) :
				 text_width(fonts[FONT_UI], FSIZE, d->lines[i]) + 6;

			fill_rect(s, x + PAD + sx, ly - 1, ex - sx, LINE_H,
				  active ? 0xffb5d5ff : 0xffdedede);
		}
		draw_text(s, fonts[FONT_UI], FSIZE, x + PAD, ly + 14, 0xff000000, d->lines[i]);
		if (i == d->cl && active && !d->sel && ((int)(dt.now * 2) & 1) == 0)
			fill_rect(s, x + PAD + width_upto(d->lines[i], d->cc), ly, 1,
				  LINE_H - 2, 0xff000000);
	}
	gfx_pop_clip();
}

static void scroll_to_cursor(window_t *win, struct doc *d)
{
	int rows = (win->h - TITLE_H - 1 - 2 * PAD) / LINE_H;

	if (d->cl < d->scroll)
		d->scroll = d->cl;
	if (d->cl >= d->scroll + rows)
		d->scroll = d->cl - rows + 1;
}

static int write_as_user(const char *path, const char *data, size_t len)
{
	pid_t pid = fork();
	int status;

	if (pid == 0) {
		int fd;

		user_become();
		fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0)
			_exit(1);
		if (write(fd, data, len) != (ssize_t)len)
			_exit(2);
		_exit(close(fd) ? 3 : 0);
	}
	if (pid < 0)
		return -1;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}

static void save_to(window_t *win, const char *path)
{
	struct doc *d = win->data;
	char *text = join_lines(d, 0, 0, d->n - 1, strlen(d->lines[d->n - 1]));
	size_t len = strlen(text);

	text = realloc(text, len + 2);
	text[len++] = '\n';
	if (write_as_user(path, text, len)) {
		wc_alert("Could not save the document", path, NULL, NULL);
	} else {
		snprintf(d->path, sizeof(d->path), "%s", path);
		d->dirty = 0;
		update_title(win, d);
	}
	free(text);
}

static window_t *saving;

static void save_as_done(const char *name, void *ctx)
{
	char path[PATH_MAX + 300], dir[PATH_MAX];

	(void)ctx;
	if (!name || !*name || !saving)
		return;
	if (name[0] == '/') {
		snprintf(path, sizeof(path), "%s", name);
	} else {
		snprintf(dir, sizeof(dir), "%s/%s", dt.home,
			 dt.set.lang == LANG_RU ? "Документы" : "Documents");
		mkdir(dir, 0755);
		if (chown(dir, dt.uid, dt.gid))
			snprintf(dir, sizeof(dir), "%s", dt.home);
		snprintf(path, sizeof(path), "%s/%s%s", dir, name,
			 strchr(name, '.') ? "" : ".txt");
	}
	save_to(saving, path);
	saving = NULL;
}

static void te_save(window_t *win)
{
	struct doc *d = win->data;

	if (d->path[0]) {
		save_to(win, d->path);
		return;
	}
	saving = win;
	wc_prompt("Save As", "Name of the document (it goes to your Documents folder):",
		  0, "Save", save_as_done, NULL);
}

static void te_copy(window_t *win)
{
	struct doc *d = win->data;
	int l1, c1, l2, c2;
	char *t;

	if (!d->sel)
		return;
	sel_range(d, &l1, &c1, &l2, &c2);
	t = join_lines(d, l1, c1, l2, c2);
	clip_set(t, CLIP_DESKTOP);
	free(t);
}

static void te_paste(window_t *win)
{
	struct doc *d = win->data;

	insert_text(d, clip_get());
	update_title(win, d);
	scroll_to_cursor(win, d);
	wm_damage(win);
}

static void move_cursor(struct doc *d, int key, int shift)
{
	const char *line = d->lines[d->cl];

	if (shift && !d->sel) {
		d->sel = 1;
		d->al = d->cl;
		d->ac = d->cc;
	} else if (!shift) {
		d->sel = 0;
	}
	switch (key) {
	case KEY_LEFT:
		if (d->cc > 0)
			d->cc = prev_char(line, d->cc);
		else if (d->cl > 0)
			d->cc = strlen(d->lines[--d->cl]);
		break;
	case KEY_RIGHT:
		if (d->cc < (int)strlen(line))
			d->cc = next_char(line, d->cc);
		else if (d->cl < d->n - 1)
			d->cl++, d->cc = 0;
		break;
	case KEY_UP:
	case KEY_DOWN:
	case KEY_PAGEUP:
	case KEY_PAGEDOWN: {
		int px = width_upto(line, d->cc);
		int step = key == KEY_UP ? -1 : key == KEY_DOWN ? 1 :
			   key == KEY_PAGEUP ? -20 : 20;

		d->cl += step;
		if (d->cl < 0)
			d->cl = 0;
		if (d->cl >= d->n)
			d->cl = d->n - 1;
		d->cc = col_at(d->lines[d->cl], px);
		break;
	}
	case KEY_HOME:
		d->cc = 0;
		break;
	case KEY_END:
		d->cc = strlen(line);
		break;
	}
	if (d->sel && d->al == d->cl && d->ac == d->cc)
		d->sel = 0;
}

static void te_key(window_t *win, const event_t *ev)
{
	struct doc *d = win->data;

	if (ev->mods & MOD_ALT) {
		switch (ev->ch | 0x20) {
		case 's':
			te_save(win);
			break;
		case 'c':
			te_copy(win);
			break;
		case 'x':
			te_copy(win);
			delete_selection(d);
			break;
		case 'v':
			te_paste(win);
			break;
		case 'a':
			d->sel = 1;
			d->al = 0;
			d->ac = 0;
			d->cl = d->n - 1;
			d->cc = strlen(d->lines[d->cl]);
			break;
		}
	} else {
		switch (ev->key) {
		case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
		case KEY_HOME: case KEY_END: case KEY_PAGEUP: case KEY_PAGEDOWN:
			move_cursor(d, ev->key, ev->mods & MOD_SHIFT);
			break;
		case KEY_BACKSPACE:
			if (d->sel) {
				delete_selection(d);
			} else if (d->cc > 0) {
				int j = prev_char(d->lines[d->cl], d->cc);

				delete_range(d, d->cl, j, d->cl, d->cc);
			} else if (d->cl > 0) {
				int l = d->cl - 1;

				delete_range(d, l, strlen(d->lines[l]), d->cl, 0);
			}
			break;
		case KEY_DELETE:
			if (d->sel) {
				delete_selection(d);
			} else if (d->cc < (int)strlen(d->lines[d->cl])) {
				delete_range(d, d->cl, d->cc, d->cl,
					     next_char(d->lines[d->cl], d->cc));
			} else if (d->cl < d->n - 1) {
				delete_range(d, d->cl, d->cc, d->cl + 1, 0);
			}
			break;
		case KEY_ENTER:
		case KEY_KPENTER:
			insert_text(d, "\n");
			break;
		case KEY_TAB:
			insert_text(d, "    ");
			break;
		default:
			if (ev->ch >= ' ' && !(ev->mods & MOD_CTRL)) {
				char u[8];

				utf8_encode(ev->ch, u);
				insert_text(d, u);
			}
		}
	}
	update_title(win, d);
	scroll_to_cursor(win, d);
	wm_damage(win);
}

static void te_mouse(window_t *win, const event_t *ev, int x, int y)
{
	struct doc *d = win->data;
	int line = d->scroll + (y - PAD) / LINE_H;

	if (ev->type == EV_SCROLL) {
		d->scroll -= ev->dz * 3;
		if (d->scroll > d->n - 1)
			d->scroll = d->n - 1;
		if (d->scroll < 0)
			d->scroll = 0;
		wm_damage(win);
		return;
	}
	if (line < 0)
		line = 0;
	if (line >= d->n)
		line = d->n - 1;
	if (ev->type == EV_BUTTON && ev->button == 1) {
		d->dragging = ev->pressed;
		if (!ev->pressed)
			return;
		d->cl = line;
		d->cc = col_at(d->lines[line], x - PAD);
		d->al = d->cl;
		d->ac = d->cc;
		d->sel = 0;
	} else if (ev->type == EV_MOTION && d->dragging) {
		d->cl = line;
		d->cc = col_at(d->lines[line], x - PAD);
		d->sel = d->cl != d->al || d->cc != d->ac;
	} else {
		return;
	}
	wm_damage(win);
}

static void te_tick(window_t *win)
{
	static int blink;

	if (win == dt.focus && blink != (int)(dt.now * 2)) {
		blink = (int)(dt.now * 2);
		wm_damage(win);
	}
}

static void te_destroy(window_t *win)
{
	struct doc *d = win->data;
	int i;

	for (i = 0; i < d->n; i++)
		free(d->lines[i]);
	free(d->lines);
	free(d);
}

void app_textedit(const char *path)
{
	struct doc *d = calloc(1, sizeof(*d));
	window_t *win;

	if (path) {
		char *text = user_read_file_len(path, NULL);

		if (!text) {
			free(d);
			wc_alert("Could not open the document", path, NULL, NULL);
			return;
		}
		set_lines(d, text);
		free(text);
		snprintf(d->path, sizeof(d->path), "%s", path);
	} else {
		set_lines(d, "");
	}
	win = wm_create("Untitled", APP_TEXTEDIT, 640, 520);
	win->resizable = 1;
	win->min_w = 240;
	win->min_h = 160;
	win->data = d;
	win->draw = te_draw;
	win->key = te_key;
	win->mouse = te_mouse;
	win->tick = te_tick;
	win->destroy = te_destroy;
	win->copy = te_copy;
	win->paste = te_paste;
	win->save = te_save;
	update_title(win, d);
}
