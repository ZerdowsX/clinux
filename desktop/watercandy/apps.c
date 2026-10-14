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
		app_files("/root", "Home");
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
	win = wm_create("System Preferences", APP_PREFS, 440, 280);
	win->draw = prefs_draw;
	win->mouse = prefs_mouse;
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
