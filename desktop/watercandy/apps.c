// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - small built-in applications: About, Preferences, Clock.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>

#include "wc.h"

void app_launch(enum app_id app)
{
	switch (app) {
	case APP_FILES:
		app_files(dt.home, "Home");
		break;
	case APP_FIREFOX:
		app_firefox();
		break;
	case APP_STORE:
		app_store(0);
		break;
	case APP_TEXTEDIT:
		app_textedit(NULL);
		break;
	case APP_TERMINAL:
		app_terminal();
		break;
	case APP_CLOCK:
		app_clock();
		break;
	case APP_PREFS:
		app_prefs();
		break;
	case APP_ABOUT:
		app_about();
		break;
	default:
		break;
	}
}

/* ---------------------------------------------------------------------- */
/* About This Computer                                                    */

struct about {
	surface_t *logo;
	char memory[64], cpu[160], kernel[160];
};

static void strip(char *s, const char *word)
{
	char *c;
	size_t n = strlen(word);

	while ((c = strstr(s, word)))
		memmove(c, c + n, strlen(c + n) + 1);
}

static void read_info(struct about *a)
{
	char line[256];
	struct utsname u;
	FILE *f;

	snprintf(a->memory, sizeof(a->memory), "unknown");
	snprintf(a->cpu, sizeof(a->cpu), "unknown");
	f = fopen("/proc/meminfo", "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			unsigned long kb;

			if (sscanf(line, "MemTotal: %lu kB", &kb) == 1) {
				snprintf(a->memory, sizeof(a->memory), "%lu MB", kb / 1024);
				break;
			}
		}
		fclose(f);
	}
	f = fopen("/proc/cpuinfo", "r");
	if (f) {
		int n = 0;
		char model[96] = "", *c;

		while (fgets(line, sizeof(line), f)) {
			c = strchr(line, ':');
			if (!strncmp(line, "processor", 9))
				n++;
			if (!model[0] && c && !strncmp(line, "model name", 10)) {
				c += 2;
				c[strcspn(c, "\n")] = 0;
				snprintf(model, sizeof(model), "%s", c);
			}
		}
		fclose(f);
		strip(model, "(R)");
		strip(model, "(TM)");
		strip(model, " CPU");
		if ((c = strstr(model, " @")))
			*c = 0;
		if (model[0] && n > 1)
			snprintf(a->cpu, sizeof(a->cpu), "%d x %s", n, model);
		else if (model[0])
			snprintf(a->cpu, sizeof(a->cpu), "%s", model);
	}
	if (!uname(&u))
		snprintf(a->kernel, sizeof(a->kernel), "Linux %s", u.release);
}

static void about_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct about *a = win->data;
	char buf[160];
	int cy;

	(void)h;
	blit(s, a->logo, x + (w - a->logo->w) / 2, y + 14, 255);
	cy = y + 14 + a->logo->h + 44;
	draw_text_center(s, fonts[FONT_SERIF], 46, x, w, cy, 0xff101010, WC_NAME);
	cy += 22;
	snprintf(buf, sizeof(buf), _("(Version %s)"), WC_VERSION);
	draw_text_center(s, fonts[FONT_UI], 11, x, w, cy, 0xff303030, buf);
	cy += 30;

	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, _("Memory:")), cy, 0xff000000, _("Memory:"));
	draw_text(s, fonts[FONT_UI], 13, x + w / 2, cy, 0xff000000, a->memory);
	cy += 20;
	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, _("Processor:")), cy, 0xff000000,
		  _("Processor:"));
	gfx_clip(s, x, y, w - 10, h);
	draw_text(s, fonts[FONT_UI], 13, x + w / 2, cy, 0xff000000, a->cpu);
	gfx_noclip();
	gfx_clip(s, x, y, w, h);
	cy += 20;
	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, _("Kernel:")), cy, 0xff000000, _("Kernel:"));
	draw_text(s, fonts[FONT_UI], 13, x + w / 2, cy, 0xff000000, a->kernel);

	draw_text_center(s, fonts[FONT_UI], 11, x, w, y + h - 14, 0xff202020,
			 "\xe2\x84\xa2 & \xc2\xa9 The " WC_NAME " Project 2026");
}

static void about_destroy(window_t *win)
{
	struct about *a = win->data;

	surf_free(a->logo);
	free(a);
}

void app_about(void)
{
	window_t *win = wm_find_app(APP_ABOUT);
	struct about *a;

	if (win) {
		if (win->minimized)
			wm_restore(win);
		wm_focus(win);
		return;
	}
	a = calloc(1, sizeof(*a));
	read_info(a);
	a->logo = surf_new(170, 170);
	icon_drop(a->logo, 0, 0, 170);

	win = wm_create("About This Computer", APP_ABOUT, 420, 420);
	win->draw = about_draw;
	win->destroy = about_destroy;
	win->data = a;
}

/* ---------------------------------------------------------------------- */
/* System Preferences                                                     */

#define PREF_ROW0 76
#define PREF_ROWH 28
#define SLIDER_Y 228
#define SLIDER_X 40
#define SLIDER_W 300
#define NET_Y 290

static void prefs_row(surface_t *s, int x, int y, const char *label,
		      const char *value)
{
	draw_text(s, fonts[FONT_UI], 13, x + 150 - text_width(fonts[FONT_UI], 13,
							       label), y, 0xff000000, label);
	draw_text(s, fonts[FONT_UI], 13, x + 160, y, 0xff000000, value);
}

static void prefs_draw_network(surface_t *s, int x, int y, int w)
{
	struct net_status st;
	surface_t *ic = surf_new(40, 40);
	char buf[96];
	paint_t p;

	net_poll(&st);
	fill_rect(s, x, y - 14, w, 1, 0x30000000);
	icon_network(ic, 0, 0, 40, st.connected);
	blit(s, ic, x + 16, y - 4, 255);
	surf_free(ic);
	draw_text(s, fonts[FONT_UI_BOLD], 15, x + 66, y + 14, 0xff000000, _("Network"));
	p = paint_solid(st.connected ? 0xff2fbf3a : 0xffd03a30);
	fill_circle(s, x + 70, y + 31, 5, &p);
	if (st.connected)
		snprintf(buf, sizeof(buf), _("%s is connected to the Internet"), _(st.kind));
	else if (st.ifname[0])
		snprintf(buf, sizeof(buf), _("%s has no Internet connection"), _(st.kind));
	else
		snprintf(buf, sizeof(buf), "%s", _("No network connection"));
	draw_text(s, fonts[FONT_UI], 12, x + 82, y + 35, 0xff404040, buf);

	y += 64;
	prefs_row(s, x, y, _("Interface:"), st.ifname[0] ? st.ifname : "\xe2\x80\x94");
	prefs_row(s, x, y + 20, _("IP Address:"), st.addr[0] ? st.addr : "\xe2\x80\x94");
	prefs_row(s, x, y + 40, _("Router:"), st.gateway[0] ? st.gateway : "\xe2\x80\x94");
	prefs_row(s, x, y + 60, _("DNS Server:"), st.dns[0] ? st.dns : "\xe2\x80\x94");
}

static void prefs_tick(window_t *win)
{
	static double last;

	if (dt.now - last > 2) {
		last = dt.now;
		wm_damage(win);
	}
}

static void prefs_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	surface_t *gear = surf_new(40, 40);
	char buf[64];
	paint_t p;

	(void)win;
	(void)h;
	p = paint_solid(0x0c000000);
	fill_rect_paint(s, x, y, w, 56, &p);
	fill_rect(s, x, y + 56, w, 1, 0x30000000);
	icon_gear(gear, 0, 0, 40);
	blit(s, gear, x + 16, y + 8, 255);
	surf_free(gear);
	draw_text(s, fonts[FONT_UI_BOLD], 15, x + 66, y + 26, 0xff000000,
		  _("Desktop & Dock"));
	draw_text(s, fonts[FONT_UI], 12, x + 66, y + 44, 0xff505050,
		  _("Make Water Candy look and feel the way you like."));

	aqua_checkbox(s, x + 40, y + PREF_ROW0, _("Animate the blue wallpaper"),
		      dt.set.animate_wallpaper);
	aqua_checkbox(s, x + 40, y + PREF_ROW0 + PREF_ROWH, _("Magnify Dock icons"),
		      dt.set.magnify_dock);
	aqua_checkbox(s, x + 40, y + PREF_ROW0 + 2 * PREF_ROWH,
		      _("Show seconds in the menu bar clock"), dt.set.show_seconds);
	aqua_checkbox(s, x + 40, y + PREF_ROW0 + 3 * PREF_ROWH,
		      "Russian interface", dt.set.lang == LANG_RU);

	snprintf(buf, sizeof(buf), _("Wallpaper motion: %.1fx"), dt.set.wallpaper_speed);
	draw_text(s, fonts[FONT_UI], 13, x + SLIDER_X, y + SLIDER_Y - 16, 0xff000000,
		  buf);
	aqua_slider(s, x + SLIDER_X, y + SLIDER_Y, SLIDER_W,
		    dt.set.wallpaper_speed / 4.0f);
	draw_text(s, fonts[FONT_UI], 11, x + SLIDER_X, y + SLIDER_Y + 26, 0xff606060,
		  _("Calm"));
	draw_text(s, fonts[FONT_UI], 11, x + SLIDER_X + SLIDER_W -
		  text_width(fonts[FONT_UI], 11, _("Lively")), y + SLIDER_Y + 26,
		  0xff606060, _("Lively"));

	prefs_draw_network(s, x, y + NET_Y, w);
}

static void prefs_mouse(window_t *win, const event_t *ev, int x, int y)
{
	static int sliding;
	int row;

	if (ev->type == EV_BUTTON && !ev->pressed) {
		sliding = 0;
		return;
	}
	if (ev->type == EV_BUTTON && ev->pressed && ev->button == 1) {
		if (abs(y - SLIDER_Y) < 12 && x > SLIDER_X - 10 &&
		    x < SLIDER_X + SLIDER_W + 10) {
			sliding = 1;
		} else if (x >= 36 && x < 380 && y >= PREF_ROW0 - 4) {
			row = (y - PREF_ROW0 + 4) / PREF_ROWH;
			if (row == 0)
				dt.set.animate_wallpaper = !dt.set.animate_wallpaper;
			else if (row == 1)
				dt.set.magnify_dock = !dt.set.magnify_dock;
			else if (row == 2)
				dt.set.show_seconds = !dt.set.show_seconds;
			else if (row == 3) {
				dt.set.lang = dt.set.lang == LANG_RU ? LANG_EN : LANG_RU;
				wm_set_title(win, _("System Preferences"));
				wm_relayout_language();
			}
			wm_damage(win);
		}
	}
	if (sliding && (ev->type == EV_MOTION || ev->type == EV_BUTTON)) {
		float v = (float)(x - SLIDER_X) / SLIDER_W;

		if (v < 0)
			v = 0;
		if (v > 1)
			v = 1;
		dt.set.wallpaper_speed = (int)(v * 40 + 0.5f) / 10.0f;
		wm_damage(win);
	}
}

void app_prefs(void)
{
	window_t *win = wm_find_app(APP_PREFS);

	if (win) {
		if (win->minimized)
			wm_restore(win);
		wm_focus(win);
		return;
	}
	win = wm_create("System Preferences", APP_PREFS, 460, 458);
	win->draw = prefs_draw;
	win->mouse = prefs_mouse;
	win->tick = prefs_tick;
}

/* ---------------------------------------------------------------------- */
/* Clock                                                                  */

static void clock_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	time_t t = time(NULL);
	struct tm tm;
	char buf[64];
	int size = w - 40;

	(void)win;
	(void)h;
	localtime_r(&t, &tm);
	icon_clock(s, x + 20, y + 14, size, tm.tm_hour, tm.tm_min, tm.tm_sec);
	format_date(buf, sizeof(buf), &tm);
	draw_text_center(s, fonts[FONT_UI], 13, x, w, y + size + 38, 0xff202020, buf);
}

static void clock_tick(window_t *win)
{
	static time_t last;
	time_t t = time(NULL);

	if (t != last) {
		last = t;
		wm_damage(win);
	}
}

void app_clock(void)
{
	window_t *win = wm_find_app(APP_CLOCK);

	if (win) {
		if (win->minimized)
			wm_restore(win);
		wm_focus(win);
		return;
	}
	win = wm_create("Clock", APP_CLOCK, 260, 310);
	win->draw = clock_draw;
	win->tick = clock_tick;
}

/* ---------------------------------------------------------------------- */
/* Alerts                                                                 */

struct alert {
	char title[128], msg[512], button2[32];
	void (*act2)(void);
	int pressed;		/* 1 = OK, 2 = second button */
};

#define ALERT_W 460

/* Word-wrap text into width w; draws when s is set. Returns the height. */
static int wrap_text(surface_t *s, font_t *f, float size, int x, int y, int w,
		     int lh, uint32_t color, const char *text)
{
	char line[800], word[256];
	const char *p = text;
	int lines = 0;

	line[0] = 0;
	while (*p) {
		size_t n = strcspn(p, " ");
		char trial[800];

		snprintf(word, sizeof(word), "%.*s", (int)n, p);
		p += n;
		while (*p == ' ')
			p++;
		snprintf(trial, sizeof(trial), "%s%s%s", line, line[0] ? " " : "", word);
		if (line[0] && text_width(f, size, trial) > w) {
			if (s)
				draw_text(s, f, size, x, y + lines * lh, color, line);
			lines++;
			snprintf(line, sizeof(line), "%s", word);
		} else {
			snprintf(line, sizeof(line), "%s", trial);
		}
	}
	if (line[0]) {
		if (s)
			draw_text(s, f, size, x, y + lines * lh, color, line);
		lines++;
	}
	return lines * lh;
}

static void alert_buttons(window_t *win, int *ok_x, int *b2_x, int *by)
{
	*ok_x = win->w - 2 - 100;
	*b2_x = *ok_x - 110;
	*by = win->h - TITLE_H - 1 - 40;
}

static void alert_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct alert *a = win->data;
	surface_t *ic = surf_new(56, 56);
	int ok_x, b2_x, by;

	(void)h;
	icon_drop(ic, 0, 0, 56);
	blit(s, ic, x + 22, y + 16, 255);
	surf_free(ic);
	draw_text(s, fonts[FONT_UI_BOLD], 14, x + 96, y + 32, 0xff000000, a->title);
	wrap_text(s, fonts[FONT_UI], 12, x + 96, y + 54, w - 120, 16, 0xff303030,
		  a->msg);
	alert_buttons(win, &ok_x, &b2_x, &by);
	if (a->button2[0]) {
		aqua_button(s, x + b2_x, y + by, 96, 22, a->button2, 1, a->pressed == 2);
		aqua_button(s, x + ok_x, y + by, 80, 22, "Cancel", 0, a->pressed == 1);
	} else {
		aqua_button(s, x + ok_x, y + by, 80, 22, "OK", 1, a->pressed == 1);
	}
}

static int alert_hit(window_t *win, int x, int y)
{
	struct alert *a = win->data;
	int ok_x, b2_x, by;

	alert_buttons(win, &ok_x, &b2_x, &by);
	if (y < by || y >= by + 22)
		return 0;
	if (x >= ok_x && x < ok_x + 80)
		return 1;
	if (a->button2[0] && x >= b2_x && x < b2_x + 96)
		return 2;
	return 0;
}

static void alert_mouse(window_t *win, const event_t *ev, int x, int y)
{
	struct alert *a = win->data;

	if (ev->type != EV_BUTTON || ev->button != 1)
		return;
	if (ev->pressed) {
		a->pressed = alert_hit(win, x, y);
	} else if (a->pressed && a->pressed == alert_hit(win, x, y)) {
		void (*act)(void) = a->pressed == 2 ? a->act2 : NULL;

		a->pressed = 0;
		wm_close(win);
		if (act)
			act();
		return;
	} else {
		a->pressed = 0;
	}
	wm_damage(win);
}

static void alert_key(window_t *win, const event_t *ev)
{
	struct alert *a = win->data;

	if (ev->ch == '\r') {
		void (*act)(void) = a->button2[0] ? a->act2 : NULL;

		wm_close(win);
		if (act)
			act();
	} else if (ev->ch == 27) {
		wm_close(win);
	}
}

static void alert_destroy(window_t *win)
{
	free(win->data);
}

void wc_alert(const char *title, const char *message, const char *button2,
	      void (*act2)(void))
{
	struct alert *a = calloc(1, sizeof(*a));
	window_t *win;
	int text_h;

	snprintf(a->title, sizeof(a->title), "%s", _(title));
	snprintf(a->msg, sizeof(a->msg), "%s", _(message));
	if (button2)
		snprintf(a->button2, sizeof(a->button2), "%s", _(button2));
	a->act2 = act2;
	text_h = wrap_text(NULL, fonts[FONT_UI], 12, 0, 0, ALERT_W - 122, 16, 0,
			   a->msg);
	win = wm_create("", APP_ALERT, ALERT_W,
			TITLE_H + 1 + (text_h + 60 > 90 ? text_h + 60 : 90) + 52);
	win->data = a;
	win->draw = alert_draw;
	win->mouse = alert_mouse;
	win->key = alert_key;
	win->destroy = alert_destroy;
}

/* ---------------------------------------------------------------------- */
/* Text prompt                                                            */

struct prompt {
	char title[128], msg[256], ok[32];
	entry_t text;
	void (*cb)(const char *text, void *ctx);
	void *ctx;
	int pressed;
};

static void prompt_finish(window_t *win, int ok)
{
	struct prompt *p = win->data;
	void (*cb)(const char *, void *) = p->cb;
	char text[sizeof(p->text.text)];

	snprintf(text, sizeof(text), "%s", p->text.text);
	p->cb = NULL;
	wm_close(win);
	if (cb)
		cb(ok ? text : NULL, p->ctx);
}

static void prompt_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	struct prompt *p = win->data;
	surface_t *ic = surf_new(52, 52);

	icon_drop(ic, 0, 0, 52);
	blit(s, ic, x + 20, y + 16, 255);
	surf_free(ic);
	draw_text(s, fonts[FONT_UI_BOLD], 14, x + 88, y + 30, 0xff000000, p->title);
	wrap_text(s, fonts[FONT_UI], 12, x + 88, y + 50, w - 110, 16, 0xff303030, p->msg);
	p->text.focused = 1;
	entry_draw(s, &p->text, x + 88, y + h - 78, w - 110, 22);
	aqua_button(s, x + w - 200, y + h - 38, 90, 22, "Cancel", 0, p->pressed == 1);
	aqua_button(s, x + w - 100, y + h - 38, 84, 22, p->ok, 1, p->pressed == 2);
}

static void prompt_mouse(window_t *win, const event_t *ev, int x, int y)
{
	struct prompt *p = win->data;
	int cw = win->w - 2, ch = win->h - TITLE_H - 1, b = 0;

	if (ev->type != EV_BUTTON || ev->button != 1)
		return;
	if (y >= ch - 38 && y < ch - 16)
		b = x >= cw - 200 && x < cw - 110 ? 1 : x >= cw - 100 && x < cw - 16 ? 2 : 0;
	if (ev->pressed) {
		p->pressed = b;
	} else {
		int was = p->pressed;

		p->pressed = 0;
		if (b && b == was) {
			prompt_finish(win, b == 2);
			return;
		}
	}
	wm_damage(win);
}

static void prompt_key(window_t *win, const event_t *ev)
{
	struct prompt *p = win->data;
	int r = entry_key(&p->text, ev);

	if (r == ENTRY_ENTER)
		prompt_finish(win, 1);
	else if (r == ENTRY_ESCAPE)
		prompt_finish(win, 0);
	else
		wm_damage(win);
}

static void prompt_tick(window_t *win)
{
	static int blink;

	if (blink != (int)(dt.now * 2)) {
		blink = (int)(dt.now * 2);
		wm_damage(win);
	}
}

static void prompt_destroy(window_t *win)
{
	struct prompt *p = win->data;

	if (p->cb)
		p->cb(NULL, p->ctx);	/* closed with the red button */
	free(p);
}

void wc_prompt(const char *title, const char *message, int password,
	       const char *ok_label, void (*cb)(const char *text, void *ctx),
	       void *ctx)
{
	struct prompt *p = calloc(1, sizeof(*p));
	window_t *win;
	int text_h;

	snprintf(p->title, sizeof(p->title), "%s", _(title));
	snprintf(p->msg, sizeof(p->msg), "%s", _(message));
	snprintf(p->ok, sizeof(p->ok), "%s", _(ok_label ? ok_label : "OK"));
	p->text.password = password;
	p->cb = cb;
	p->ctx = ctx;
	text_h = wrap_text(NULL, fonts[FONT_UI], 12, 0, 0, ALERT_W - 110, 16, 0, p->msg);
	win = wm_create("", APP_ALERT, ALERT_W, TITLE_H + 1 + 56 + text_h + 90);
	win->data = p;
	win->draw = prompt_draw;
	win->mouse = prompt_mouse;
	win->key = prompt_key;
	win->tick = prompt_tick;
	win->destroy = prompt_destroy;
}
