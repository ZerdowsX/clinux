// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Terminal: a pty running /bin/sh with a small VT100/xterm
 * emulator.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define FONT_SIZE 13
#define CELL_H 16
#define PAD 4
#define DEF_FG 16
#define DEF_BG 17
#define ATTR_BOLD 1
#define ATTR_REVERSE 2
#define ATTR_UNDERLINE 4

struct cell {
	uint32_t ch;
	uint8_t fg, bg, attr;
};

struct term {
	int fd;
	pid_t pid;
	int cols, rows, cell_w;
	struct cell *cells;
	int cx, cy, wrap_pending;
	int fg, bg, attr;
	int top, bot;
	int saved_cx, saved_cy;
	int cursor_visible;
	int state;
	char params[64];
	int plen;
	char priv;
	char osc[128];
	int olen;
	uint32_t utf;
	int utf_left;
};

static const uint32_t palette[18] = {
	0xff000000, 0xffc23621, 0xff1f8f1f, 0xff9a8a10, 0xff2848c8, 0xffa830a8,
	0xff1a8a9a, 0xffcbcccd, 0xff686868, 0xffe0301c, 0xff169c1a, 0xffa89a00,
	0xff2f5ff0, 0xffc030c0, 0xff0a9fb0, 0xffe9ebeb,
	0xff000000,		/* default foreground */
	0xffffffff,		/* default background */
};

static struct cell *at(struct term *t, int x, int y)
{
	return &t->cells[y * t->cols + x];
}

static void clear_cells(struct term *t, int x0, int y0, int x1, int y1)
{
	int x, y;

	for (y = y0; y <= y1 && y < t->rows; y++) {
		for (x = (y == y0 ? x0 : 0); x < t->cols; x++) {
			if (y == y1 && x > x1)
				break;
			*at(t, x, y) = (struct cell){ ' ', DEF_FG, t->bg, 0 };
		}
	}
}

static void scroll_up(struct term *t, int top, int bot, int n)
{
	int y;

	while (n-- > 0) {
		for (y = top; y < bot; y++)
			memcpy(at(t, 0, y), at(t, 0, y + 1), sizeof(struct cell) * t->cols);
		clear_cells(t, 0, bot, t->cols - 1, bot);
	}
}

static void scroll_down(struct term *t, int top, int bot, int n)
{
	int y;

	while (n-- > 0) {
		for (y = bot; y > top; y--)
			memcpy(at(t, 0, y), at(t, 0, y - 1), sizeof(struct cell) * t->cols);
		clear_cells(t, 0, top, t->cols - 1, top);
	}
}

static void newline(struct term *t)
{
	if (t->cy == t->bot)
		scroll_up(t, t->top, t->bot, 1);
	else if (t->cy < t->rows - 1)
		t->cy++;
}

static void clamp_cursor(struct term *t)
{
	if (t->cx < 0)
		t->cx = 0;
	if (t->cx >= t->cols)
		t->cx = t->cols - 1;
	if (t->cy < 0)
		t->cy = 0;
	if (t->cy >= t->rows)
		t->cy = t->rows - 1;
	t->wrap_pending = 0;
}

static void put_char(struct term *t, uint32_t c)
{
	struct cell *ce;

	if (t->wrap_pending) {
		t->cx = 0;
		newline(t);
		t->wrap_pending = 0;
	}
	ce = at(t, t->cx, t->cy);
	ce->ch = c;
	ce->fg = t->fg;
	ce->bg = t->bg;
	ce->attr = t->attr;
	if (t->cx == t->cols - 1)
		t->wrap_pending = 1;
	else
		t->cx++;
}

static void reply(struct term *t, const char *s)
{
	if (write(t->fd, s, strlen(s)) < 0)
		return;
}

static int nparams(struct term *t, int *p, int max)
{
	int n = 0;
	char *s = t->params;

	t->params[t->plen] = 0;
	p[0] = 0;
	while (n < max) {
		p[n] = 0;
		while (*s >= '0' && *s <= '9')
			p[n] = p[n] * 10 + (*s++ - '0');
		n++;
		if (*s != ';')
			break;
		s++;
	}
	return n;
}

static void sgr(struct term *t, int *p, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		int v = p[i];

		if (v == 0) {
			t->fg = DEF_FG;
			t->bg = DEF_BG;
			t->attr = 0;
		} else if (v == 1) {
			t->attr |= ATTR_BOLD;
		} else if (v == 4) {
			t->attr |= ATTR_UNDERLINE;
		} else if (v == 7) {
			t->attr |= ATTR_REVERSE;
		} else if (v == 22) {
			t->attr &= ~ATTR_BOLD;
		} else if (v == 24) {
			t->attr &= ~ATTR_UNDERLINE;
		} else if (v == 27) {
			t->attr &= ~ATTR_REVERSE;
		} else if (v >= 30 && v <= 37) {
			t->fg = v - 30;
		} else if (v == 39) {
			t->fg = DEF_FG;
		} else if (v >= 40 && v <= 47) {
			t->bg = v - 40;
		} else if (v == 49) {
			t->bg = DEF_BG;
		} else if (v >= 90 && v <= 97) {
			t->fg = v - 90 + 8;
		} else if (v >= 100 && v <= 107) {
			t->bg = v - 100 + 8;
		} else if ((v == 38 || v == 48) && i + 2 < n && p[i + 1] == 5) {
			int c = p[i + 2] < 16 ? p[i + 2] : (v == 38 ? DEF_FG : DEF_BG);

			if (v == 38)
				t->fg = c;
			else
				t->bg = c;
			i += 2;
		}
	}
}

static void csi(struct term *t, char f)
{
	int p[16], n = nparams(t, p, 16), i;
	int p0 = p[0] ? p[0] : 1;
	char buf[32];

	if (t->priv == '?') {
		for (i = 0; i < n; i++) {
			if (p[i] == 25)
				t->cursor_visible = f == 'h';
			if ((p[i] == 1049 || p[i] == 47 || p[i] == 1047) && f == 'h')
				clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
		}
		return;
	}
	if (t->priv)
		return;

	switch (f) {
	case 'A':
		t->cy -= p0;
		clamp_cursor(t);
		break;
	case 'B':
	case 'e':
		t->cy += p0;
		clamp_cursor(t);
		break;
	case 'C':
	case 'a':
		t->cx += p0;
		clamp_cursor(t);
		break;
	case 'D':
		t->cx -= p0;
		clamp_cursor(t);
		break;
	case 'E':
		t->cx = 0;
		t->cy += p0;
		clamp_cursor(t);
		break;
	case 'F':
		t->cx = 0;
		t->cy -= p0;
		clamp_cursor(t);
		break;
	case 'G':
	case '`':
		t->cx = p0 - 1;
		clamp_cursor(t);
		break;
	case 'd':
		t->cy = p0 - 1;
		clamp_cursor(t);
		break;
	case 'H':
	case 'f':
		t->cy = p0 - 1;
		t->cx = (n > 1 && p[1] ? p[1] : 1) - 1;
		clamp_cursor(t);
		break;
	case 'J':
		if (p[0] == 0)
			clear_cells(t, t->cx, t->cy, t->cols - 1, t->rows - 1);
		else if (p[0] == 1)
			clear_cells(t, 0, 0, t->cx, t->cy);
		else
			clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
		break;
	case 'K':
		if (p[0] == 0)
			clear_cells(t, t->cx, t->cy, t->cols - 1, t->cy);
		else if (p[0] == 1)
			clear_cells(t, 0, t->cy, t->cx, t->cy);
		else
			clear_cells(t, 0, t->cy, t->cols - 1, t->cy);
		break;
	case 'L':
		if (t->cy >= t->top && t->cy <= t->bot)
			scroll_down(t, t->cy, t->bot, p0);
		break;
	case 'M':
		if (t->cy >= t->top && t->cy <= t->bot)
			scroll_up(t, t->cy, t->bot, p0);
		break;
	case 'P':
		for (i = t->cx; i < t->cols; i++)
			*at(t, i, t->cy) = i + p0 < t->cols ? *at(t, i + p0, t->cy) :
				(struct cell){ ' ', DEF_FG, t->bg, 0 };
		break;
	case '@':
		for (i = t->cols - 1; i >= t->cx; i--)
			*at(t, i, t->cy) = i - p0 >= t->cx ? *at(t, i - p0, t->cy) :
				(struct cell){ ' ', DEF_FG, t->bg, 0 };
		break;
	case 'X':
		clear_cells(t, t->cx, t->cy,
			    t->cx + p0 - 1 < t->cols ? t->cx + p0 - 1 : t->cols - 1, t->cy);
		break;
	case 'm':
		sgr(t, p, n);
		break;
	case 'r':
		t->top = (p[0] ? p[0] : 1) - 1;
		t->bot = (n > 1 && p[1] ? p[1] : t->rows) - 1;
		if (t->bot >= t->rows || t->top >= t->bot) {
			t->top = 0;
			t->bot = t->rows - 1;
		}
		t->cx = 0;
		t->cy = 0;
		t->wrap_pending = 0;
		break;
	case 's':
		t->saved_cx = t->cx;
		t->saved_cy = t->cy;
		break;
	case 'u':
		t->cx = t->saved_cx;
		t->cy = t->saved_cy;
		clamp_cursor(t);
		break;
	case 'n':
		if (p[0] == 6) {
			snprintf(buf, sizeof(buf), "\033[%d;%dR", t->cy + 1, t->cx + 1);
			reply(t, buf);
		} else if (p[0] == 5) {
			reply(t, "\033[0n");
		}
		break;
	case 'c':
		reply(t, "\033[?6c");
		break;
	}
}

static void feed(window_t *win, struct term *t, unsigned char c)
{
	if (t->utf_left && (c & 0xc0) == 0x80) {
		t->utf = t->utf << 6 | (c & 0x3f);
		if (--t->utf_left == 0 && t->state == 0)
			put_char(t, t->utf);
		return;
	}
	t->utf_left = 0;

	switch (t->state) {
	case 1:			/* ESC */
		t->state = 0;
		switch (c) {
		case '[':
			t->state = 2;
			t->plen = 0;
			t->priv = 0;
			break;
		case ']':
			t->state = 3;
			t->olen = 0;
			break;
		case '(':
		case ')':
		case '*':
		case '+':
		case '#':
			t->state = 4;
			break;
		case '7':
			t->saved_cx = t->cx;
			t->saved_cy = t->cy;
			break;
		case '8':
			t->cx = t->saved_cx;
			t->cy = t->saved_cy;
			clamp_cursor(t);
			break;
		case 'D':
			newline(t);
			break;
		case 'E':
			t->cx = 0;
			newline(t);
			break;
		case 'M':
			if (t->cy == t->top)
				scroll_down(t, t->top, t->bot, 1);
			else if (t->cy > 0)
				t->cy--;
			break;
		case 'c':
			t->fg = DEF_FG;
			t->bg = DEF_BG;
			t->attr = 0;
			t->top = 0;
			t->bot = t->rows - 1;
			t->cx = t->cy = 0;
			clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
			break;
		}
		return;
	case 2:			/* CSI */
		if ((c >= '0' && c <= '9') || c == ';') {
			if (t->plen < (int)sizeof(t->params) - 1)
				t->params[t->plen++] = c;
		} else if (c == '?' || c == '>' || c == '!' || c == '=') {
			t->priv = c;
		} else if (c >= 0x40 && c <= 0x7e) {
			t->state = 0;
			csi(t, c);
		} else if (c < 0x20) {
			goto control;
		}
		return;
	case 3:			/* OSC */
		if (c == 7 || c == 0x1b || c == 0x9c) {
			t->osc[t->olen] = 0;
			if ((t->osc[0] == '0' || t->osc[0] == '2') && t->osc[1] == ';')
				wm_set_title(win, t->osc + 2);
			t->state = c == 0x1b ? 4 : 0;
		} else if (t->olen < (int)sizeof(t->osc) - 1) {
			t->osc[t->olen++] = c;
		}
		return;
	case 4:			/* skip one byte */
		t->state = 0;
		return;
	}

control:
	switch (c) {
	case '\r':
		t->cx = 0;
		t->wrap_pending = 0;
		return;
	case '\n':
	case '\v':
	case '\f':
		newline(t);
		t->wrap_pending = 0;
		return;
	case '\b':
		if (t->cx > 0)
			t->cx--;
		t->wrap_pending = 0;
		return;
	case '\t':
		t->cx = (t->cx / 8 + 1) * 8;
		if (t->cx >= t->cols)
			t->cx = t->cols - 1;
		return;
	case 0x1b:
		t->state = 1;
		return;
	case 7:
	case 0:
	case 0x0e:
	case 0x0f:
		return;
	}
	if (c >= 0xf0) {
		t->utf = c & 7;
		t->utf_left = 3;
	} else if (c >= 0xe0) {
		t->utf = c & 15;
		t->utf_left = 2;
	} else if (c >= 0xc0) {
		t->utf = c & 31;
		t->utf_left = 1;
	} else if (c >= 0x20 && c != 0x7f) {
		put_char(t, c);
	}
}

static void term_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct term *t = win->data;
	int cx, cy, active = win == dt.focus;
	char buf[8];

	fill_rect(s, x, y, w, h, palette[DEF_BG]);
	for (cy = 0; cy < t->rows; cy++) {
		for (cx = 0; cx < t->cols; cx++) {
			struct cell *c = at(t, cx, cy);
			int px = x + PAD + cx * t->cell_w, py = y + PAD + cy * CELL_H;
			int fg = c->fg, bg = c->bg;
			uint32_t fgc, bgc;
			int is_cursor = t->cursor_visible && cx == t->cx && cy == t->cy;

			if (c->attr & ATTR_BOLD && fg < 8)
				fg += 8;
			fgc = palette[fg];
			bgc = palette[bg];
			if (c->attr & ATTR_REVERSE) {
				uint32_t tmp = fgc;

				fgc = bgc;
				bgc = tmp;
			}
			if (is_cursor && active) {
				bgc = 0xff3a3a3a;
				fgc = 0xffffffff;
			}
			if (bgc != palette[DEF_BG])
				fill_rect(s, px, py, t->cell_w, CELL_H, bgc);
			if (is_cursor && !active) {
				paint_t p = paint_solid(0xff606060);

				stroke_rrect(s, px + 0.5f, py + 0.5f, t->cell_w - 1, CELL_H - 1,
					     0, 1, &p);
			}
			if (c->ch > ' ') {
				const char *e = buf;
				uint32_t ch = c->ch;

				if (ch < 0x80) {
					buf[0] = ch;
					buf[1] = 0;
				} else if (ch < 0x800) {
					buf[0] = 0xc0 | ch >> 6;
					buf[1] = 0x80 | (ch & 63);
					buf[2] = 0;
				} else if (ch < 0x10000) {
					buf[0] = 0xe0 | ch >> 12;
					buf[1] = 0x80 | ((ch >> 6) & 63);
					buf[2] = 0x80 | (ch & 63);
					buf[3] = 0;
				} else {
					buf[0] = 0xf0 | ch >> 18;
					buf[1] = 0x80 | ((ch >> 12) & 63);
					buf[2] = 0x80 | ((ch >> 6) & 63);
					buf[3] = 0x80 | (ch & 63);
					buf[4] = 0;
				}
				draw_text(s, fonts[FONT_MONO], FONT_SIZE, px, py + 12, fgc, e);
				if (c->attr & ATTR_BOLD)
					draw_text(s, fonts[FONT_MONO], FONT_SIZE, px + 1, py + 12,
						  ALPHA(fgc, 120), e);
			}
			if (c->attr & ATTR_UNDERLINE)
				fill_rect(s, px, py + CELL_H - 2, t->cell_w, 1, fgc);
		}
	}
}

static void set_title(window_t *win, struct term *t)
{
	char title[64];

	snprintf(title, sizeof(title), "Terminal \xe2\x80\x94 %d\xc3\x97%d", t->cols,
		 t->rows);
	wm_set_title(win, title);
}

static void term_resize(window_t *win)
{
	struct term *t = win->data;
	int cols = (win->w - 2 - 2 * PAD) / t->cell_w;
	int rows = (win->h - TITLE_H - 1 - 2 * PAD) / CELL_H;
	struct cell *nc;
	struct winsize ws;
	int x, y, shift;

	if (cols < 10)
		cols = 10;
	if (rows < 3)
		rows = 3;
	if (cols == t->cols && rows == t->rows)
		return;
	nc = malloc(sizeof(*nc) * cols * rows);
	for (y = 0; y < rows; y++)
		for (x = 0; x < cols; x++)
			nc[y * cols + x] = (struct cell){ ' ', DEF_FG, DEF_BG, 0 };
	/* keep the bottom part (where the prompt is) when shrinking */
	shift = t->cy >= rows ? t->cy - rows + 1 : 0;
	for (y = 0; t->cells && y < rows && y + shift < t->rows; y++)
		for (x = 0; x < cols && x < t->cols; x++)
			nc[y * cols + x] = t->cells[(y + shift) * t->cols + x];
	free(t->cells);
	t->cells = nc;
	t->cols = cols;
	t->rows = rows;
	t->cy -= shift;
	t->top = 0;
	t->bot = rows - 1;
	clamp_cursor(t);
	ws = (struct winsize){ .ws_row = rows, .ws_col = cols };
	ioctl(t->fd, TIOCSWINSZ, &ws);
	set_title(win, t);
}

static void term_key(window_t *win, const event_t *ev)
{
	struct term *t = win->data;
	const char *seq = NULL;
	char c;

	switch (ev->key) {
	case KEY_UP: seq = "\033[A"; break;
	case KEY_DOWN: seq = "\033[B"; break;
	case KEY_RIGHT: seq = "\033[C"; break;
	case KEY_LEFT: seq = "\033[D"; break;
	case KEY_HOME: seq = "\033[H"; break;
	case KEY_END: seq = "\033[F"; break;
	case KEY_DELETE: seq = "\033[3~"; break;
	case KEY_PAGEUP: seq = "\033[5~"; break;
	case KEY_PAGEDOWN: seq = "\033[6~"; break;
	case KEY_INSERT: seq = "\033[2~"; break;
	case KEY_BACKSPACE: seq = "\177"; break;
	}
	if (seq) {
		reply(t, seq);
		return;
	}
	if (ev->ch) {
		c = (char)ev->ch;
		if (write(t->fd, &c, 1) < 0)
			return;
	}
}

static void term_tick(window_t *win)
{
	struct term *t = win->data;
	unsigned char buf[4096];
	ssize_t n;
	int i, got = 0;

	while ((n = read(t->fd, buf, sizeof(buf))) > 0) {
		for (i = 0; i < n; i++)
			feed(win, t, buf[i]);
		got = 1;
	}
	if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
		win->tick = NULL;
		wm_close(win);
		return;
	}
	if (got)
		wm_damage(win);
}

static void term_mouse(window_t *win, const event_t *ev, int x, int y)
{
	(void)win;
	(void)ev;
	(void)x;
	(void)y;
}

static void term_destroy(window_t *win)
{
	struct term *t = win->data;

	if (t->pid > 0)
		kill(t->pid, SIGHUP);
	close(t->fd);
	free(t->cells);
	free(t);
}

static int spawn_shell(struct term *t, const char *cmd)
{
	char *slave_name;

	t->fd = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (t->fd < 0 || grantpt(t->fd) || unlockpt(t->fd))
		return -1;
	slave_name = ptsname(t->fd);
	if (!slave_name)
		return -1;
	t->pid = fork();
	if (t->pid < 0)
		return -1;
	if (t->pid == 0) {
		struct winsize ws = { .ws_row = t->rows, .ws_col = t->cols };
		const char *shell = access(dt.shell, X_OK) ? "/bin/sh" : dt.shell;
		const char *base = strrchr(shell, '/') ? strrchr(shell, '/') + 1 : shell;
		char login[160], script[1024];
		int s;

		setsid();
		s = open(slave_name, O_RDWR);
		if (s < 0)
			_exit(1);
		ioctl(s, TIOCSCTTY, 0);
		ioctl(s, TIOCSWINSZ, &ws);
		dup2(s, 0);
		dup2(s, 1);
		dup2(s, 2);
		if (s > 2)
			close(s);
		signal(SIGCHLD, SIG_DFL);
		signal(SIGPIPE, SIG_DFL);
		user_become();
		setenv("TERM", "xterm", 1);
		setenv("PS1", "\\h:\\w \\u\\$ ", 1);
		snprintf(login, sizeof(login), "-%s", base);
		if (cmd) {
			/* Run the command, then stay in an interactive shell. */
			snprintf(script, sizeof(script), "%s; echo; exec %s -l", cmd,
				 shell);
			execl(shell, base, "-c", script, (char *)NULL);
		} else {
			execl(shell, login, (char *)NULL);
		}
		execl("/bin/sh", "-sh", (char *)NULL);
		_exit(127);
	}
	fcntl(t->fd, F_SETFL, fcntl(t->fd, F_GETFL) | O_NONBLOCK);
	return 0;
}

static void open_terminal(const char *cmd)
{
	struct term *t = calloc(1, sizeof(*t));
	window_t *win;

	t->cell_w = text_width(fonts[FONT_MONO], FONT_SIZE, "M");
	t->fg = DEF_FG;
	t->bg = DEF_BG;
	t->cursor_visible = 1;

	win = wm_create("Terminal", APP_TERMINAL, 80 * t->cell_w + 2 * PAD + 2,
			24 * CELL_H + 2 * PAD + TITLE_H + 1);
	win->resizable = 1;
	win->min_w = 20 * t->cell_w;
	win->min_h = 6 * CELL_H + TITLE_H;
	win->data = t;
	win->draw = term_draw;
	win->key = term_key;
	win->tick = term_tick;
	win->mouse = term_mouse;
	win->resized = term_resize;
	win->destroy = term_destroy;
	t->fd = -1;
	term_resize(win);
	if (spawn_shell(t, cmd)) {
		const char *msg = "watercandy: cannot start a shell\r\n";

		while (*msg)
			feed(win, t, (unsigned char)*msg++);
		win->tick = NULL;
	}
	{
		struct winsize ws = { .ws_row = t->rows, .ws_col = t->cols };

		ioctl(t->fd, TIOCSWINSZ, &ws);
	}
}

void app_terminal(void)
{
	open_terminal(NULL);
}

void app_terminal_cmd(const char *cmd)
{
	open_terminal(cmd);
}
