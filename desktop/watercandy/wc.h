/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Water Candy - a small Aqua-style desktop shell.
 */
#ifndef WC_H
#define WC_H

#include "font.h"
#include "gfx.h"
#include "input.h"

#define WC_NAME		"Water Candy"
#define WC_VERSION	"1.0"

#define MENUBAR_H	22
#define TITLE_H		22
#define GROW_BOX	15

enum app_id {
	APP_NONE,
	APP_FILES,
	APP_TERMINAL,
	APP_CLOCK,
	APP_PREFS,
	APP_ABOUT,
	APP_COUNT
};

enum anim_kind { ANIM_NONE, ANIM_OPEN, ANIM_CLOSE, ANIM_MINIMIZE, ANIM_RESTORE };

typedef struct window window_t;

struct window {
	int x, y, w, h;
	char title[128];
	enum app_id app;

	surface_t *surf;
	int dirty;
	surface_t *shadow;
	int shadow_w, shadow_h, shadow_active;

	int minimized, zoomed, saved[4];
	int resizable, min_w, min_h;
	int hover_btns, pressed_btn;

	enum anim_kind anim;
	double anim_t0;

	/* Content callbacks; coordinates are relative to the content area. */
	void (*draw)(window_t *win, surface_t *s, int x, int y, int w, int h);
	void (*mouse)(window_t *win, const event_t *ev, int x, int y);
	void (*key)(window_t *win, const event_t *ev);
	void (*tick)(window_t *win);
	void (*resized)(window_t *win);
	void (*destroy)(window_t *win);
	void *data;

	window_t *next;		/* stacking order, bottom first */
};

struct settings {
	int animate_wallpaper;
	int magnify_dock;
	int show_seconds;
	float wallpaper_speed;
};

struct desktop {
	int w, h;
	surface_t *frame;
	surface_t *wall;
	window_t *windows;
	window_t *focus;
	struct settings set;
	double now;
	int redraw;
	int quit;
	unsigned long frames;
};

extern struct desktop dt;

double now_s(void);

/* Shading helpers. */
uint32_t shade(uint32_t c, float f);

/* wallpaper.c */
void wallpaper_init(int w, int h);
void wallpaper_render(surface_t *dst, double t);

/* icons.c - all icons are drawn procedurally at any size. */
void icon_drop(surface_t *s, float x, float y, float size);
void icon_folder(surface_t *s, float x, float y, float size);
void icon_document(surface_t *s, float x, float y, float size);
void icon_terminal(surface_t *s, float x, float y, float size);
void icon_clock(surface_t *s, float x, float y, float size, int hh, int mm,
		int ss);
void icon_gear(surface_t *s, float x, float y, float size);
void icon_trash(surface_t *s, float x, float y, float size);
void icon_disk(surface_t *s, float x, float y, float size);
void icon_app(surface_t *s, float x, float y, float size);

/* aqua.c - Aqua widgets. */
void aqua_gel_circle(surface_t *s, float cx, float cy, float r, uint32_t base);
void aqua_traffic(surface_t *s, float cx, float cy, float r, int which,
		  int active, int show_symbol);
void aqua_button(surface_t *s, int x, int y, int w, int h, const char *label,
		 int primary, int pressed);
void aqua_checkbox(surface_t *s, int x, int y, const char *label, int checked);
void aqua_slider(surface_t *s, int x, int y, int w, float value);
void aqua_highlight(surface_t *s, int x, int y, int w, int h, float r);
surface_t *glass_text(font_t *f, float size, const char *text);

/* wm.c - windows, menu bar, dock, desktop. */
void wm_init(void);
window_t *wm_create(const char *title, enum app_id app, int w, int h);
void wm_close(window_t *win);
void wm_focus(window_t *win);
void wm_damage(window_t *win);
void wm_set_title(window_t *win, const char *title);
void wm_minimize(window_t *win);
void wm_restore(window_t *win);
void wm_zoom(window_t *win);
window_t *wm_find_app(enum app_id app);
void wm_event(const event_t *ev);
void wm_tick(void);
int wm_animating(void);
void wm_render(void);
void wm_dock_bounce(enum app_id app);

/* Applications. */
void app_launch(enum app_id app);
void app_about(void);
void app_terminal(void);
void app_files(const char *path, const char *title);
void app_prefs(void);
void app_clock(void);

void wc_shutdown(int reboot_instead);

#endif
