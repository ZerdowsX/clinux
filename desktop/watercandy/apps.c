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
	snprintf(buf, sizeof(buf), "(Version %s)", WC_VERSION);
	draw_text_center(s, fonts[FONT_UI], 11, x, w, cy, 0xff303030, buf);
	cy += 30;

	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, "Memory:"), cy, 0xff000000, "Memory:");
	draw_text(s, fonts[FONT_UI], 13, x + w / 2, cy, 0xff000000, a->memory);
	cy += 20;
	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, "Processor:"), cy, 0xff000000,
		  "Processor:");
	gfx_clip(s, x, y, w - 10, h);
	draw_text(s, fonts[FONT_UI], 13, x + w / 2, cy, 0xff000000, a->cpu);
	gfx_noclip();
	gfx_clip(s, x, y, w, h);
	cy += 20;
	draw_text(s, fonts[FONT_UI], 13, x + w / 2 - 8 -
		  text_width(fonts[FONT_UI], 13, "Kernel:"), cy, 0xff000000, "Kernel:");
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
#define SLIDER_Y 200
#define SLIDER_X 40
#define SLIDER_W 300
#define NET_Y 262

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
	draw_text(s, fonts[FONT_UI_BOLD], 15, x + 66, y + 14, 0xff000000, "Network");
	p = paint_solid(st.connected ? 0xff2fbf3a : 0xffd03a30);
	fill_circle(s, x + 70, y + 31, 5, &p);
	if (st.connected)
		snprintf(buf, sizeof(buf), "%s is connected to the Internet", st.kind);
	else if (st.ifname[0])
		snprintf(buf, sizeof(buf), "%s has no Internet connection", st.kind);
	else
		snprintf(buf, sizeof(buf), "No network connection");
	draw_text(s, fonts[FONT_UI], 12, x + 82, y + 35, 0xff404040, buf);

	y += 64;
	prefs_row(s, x, y, "Interface:", st.ifname[0] ? st.ifname : "\xe2\x80\x94");
	prefs_row(s, x, y + 20, "IP Address:", st.addr[0] ? st.addr : "\xe2\x80\x94");
	prefs_row(s, x, y + 40, "Router:", st.gateway[0] ? st.gateway : "\xe2\x80\x94");
	prefs_row(s, x, y + 60, "DNS Server:", st.dns[0] ? st.dns : "\xe2\x80\x94");
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
		  "Desktop & Dock");
	draw_text(s, fonts[FONT_UI], 12, x + 66, y + 44, 0xff505050,
		  "Make " WC_NAME " look and feel the way you like.");

	aqua_checkbox(s, x + 40, y + PREF_ROW0, "Animate the blue wallpaper",
		      dt.set.animate_wallpaper);
	aqua_checkbox(s, x + 40, y + PREF_ROW0 + PREF_ROWH, "Magnify Dock icons",
		      dt.set.magnify_dock);
	aqua_checkbox(s, x + 40, y + PREF_ROW0 + 2 * PREF_ROWH,
		      "Show seconds in the menu bar clock", dt.set.show_seconds);

	snprintf(buf, sizeof(buf), "Wallpaper motion: %.1fx", dt.set.wallpaper_speed);
	draw_text(s, fonts[FONT_UI], 13, x + SLIDER_X, y + SLIDER_Y - 16, 0xff000000,
		  buf);
	aqua_slider(s, x + SLIDER_X, y + SLIDER_Y, SLIDER_W,
		    dt.set.wallpaper_speed / 4.0f);
	draw_text(s, fonts[FONT_UI], 11, x + SLIDER_X, y + SLIDER_Y + 26, 0xff606060,
		  "Calm");
	draw_text(s, fonts[FONT_UI], 11, x + SLIDER_X + SLIDER_W -
		  text_width(fonts[FONT_UI], 11, "Lively"), y + SLIDER_Y + 26,
		  0xff606060, "Lively");

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
	win = wm_create("System Preferences", APP_PREFS, 440, 430);
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
	strftime(buf, sizeof(buf), "%A, %B %e", &tm);
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

	snprintf(a->title, sizeof(a->title), "%s", title);
	snprintf(a->msg, sizeof(a->msg), "%s", message);
	if (button2)
		snprintf(a->button2, sizeof(a->button2), "%s", button2);
	a->act2 = act2;
	text_h = wrap_text(NULL, fonts[FONT_UI], 12, 0, 0, ALERT_W - 122, 16, 0,
			   message);
	win = wm_create("", APP_ALERT, ALERT_W,
			TITLE_H + 1 + (text_h + 60 > 90 ? text_h + 60 : 90) + 52);
	win->data = a;
	win->draw = alert_draw;
	win->mouse = alert_mouse;
	win->key = alert_key;
	win->destroy = alert_destroy;
}
