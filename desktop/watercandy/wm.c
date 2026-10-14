// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - window manager, menu bar, dock and desktop.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wc.h"

struct desktop dt;

/* ---------------------------------------------------------------------- */
/* Menus                                                                  */

struct mitem {
	const char *label;
	const char *key;
	int enabled;
	void (*act)(void);
	window_t *win;
};

#define MAX_MENUS	8
#define MAX_ITEMS	24
#define ITEM_H		20
#define SEP_H		10

struct menu {
	const char *title;
	int x, w;
	struct mitem items[MAX_ITEMS];
	int n;
};

static struct menu menus[MAX_MENUS];
static int nmenus;
static int open_menu = -1, hover_item = -1;
static int dd_x, dd_y, dd_w, dd_h;
static surface_t *menubar_surf, *dropdown_surf, *dropdown_shadow;
static int menubar_dirty = 1, dropdown_dirty;
static char menubar_clock[64];

/* ---------------------------------------------------------------------- */
/* Dock                                                                   */

enum dock_kind { DOCK_APP, DOCK_SEPARATOR, DOCK_WINDOW, DOCK_TRASH };

struct dock_item {
	enum dock_kind kind;
	enum app_id app;
	const char *name;
	window_t *win;
	surface_t *icon;
	double bounce_t0;
	/* layout, recomputed every frame */
	float x, size;
};

#define DOCK_MAX 32
static struct dock_item dock[DOCK_MAX];
static int ndock;
static int dock_base, dock_max;
static int dock_hover = -1;
static float dock_panel_x, dock_panel_w;
static int clock_icon_minute = -1;

static struct dock_item apps_dock[] = {
	{ .kind = DOCK_APP, .app = APP_FILES, .name = "Files" },
	{ .kind = DOCK_APP, .app = APP_TERMINAL, .name = "Terminal" },
	{ .kind = DOCK_APP, .app = APP_CLOCK, .name = "Clock" },
	{ .kind = DOCK_APP, .app = APP_PREFS, .name = "System Preferences" },
};
#define NAPPS (int)(sizeof(apps_dock) / sizeof(apps_dock[0]))
static struct dock_item trash_item = { .kind = DOCK_TRASH, .name = "Trash" };

/* ---------------------------------------------------------------------- */
/* Desktop icons and pointer state                                        */

static surface_t *cursor_surf;
static surface_t *disk_icon;
static int disk_x, disk_y, disk_selected;
#define DESK_ICON 64

static int ptr_x, ptr_y, button_down;
static window_t *drag_win, *resize_win, *grab_win, *btn_win;
static int drag_dx, drag_dy, rs_w, rs_h, rs_mx, rs_my;
static double last_click_t;
static int last_click_x, last_click_y;

static const char *const app_names[APP_COUNT] = {
	[APP_NONE] = "Files",
	[APP_FILES] = "Files",
	[APP_TERMINAL] = "Terminal",
	[APP_CLOCK] = "Clock",
	[APP_PREFS] = "System Preferences",
	[APP_ABOUT] = "Files",
};

double now_s(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static float ease(float t)
{
	if (t <= 0)
		return 0;
	if (t >= 1)
		return 1;
	return 1 - (1 - t) * (1 - t) * (1 - t);
}

/* ---------------------------------------------------------------------- */
/* Cursor                                                                 */

static surface_t *make_cursor(void)
{
	static const float arrow[] = { 0, 0, 0, 17, 4.2f, 13.2f, 7.2f, 20, 10.2f,
				       18.8f, 7.4f, 12.2f, 12.6f, 12.2f };
	float pts[14], sh[14];
	surface_t *s = surf_new(24, 28), *shadow = surf_new(24, 28);
	paint_t p;
	int i;

	for (i = 0; i < 7; i++) {
		pts[2 * i] = arrow[2 * i] + 2.5f;
		pts[2 * i + 1] = arrow[2 * i + 1] + 2.5f;
		sh[2 * i] = pts[2 * i] + 1;
		sh[2 * i + 1] = pts[2 * i + 1] + 2;
	}
	p = paint_solid(0x80000000);
	fill_poly(shadow, sh, 7, &p);
	blur_surface(shadow, 1);
	blit(s, shadow, 0, 0, 255);
	surf_free(shadow);
	p = paint_solid(0xffffffff);
	stroke_poly(s, pts, 7, 2.4f, &p);
	p = paint_solid(0xff000000);
	fill_poly(s, pts, 7, &p);
	return s;
}

/* ---------------------------------------------------------------------- */
/* Windows                                                                */

window_t *wm_create(const char *title, enum app_id app, int w, int h)
{
	window_t *win = calloc(1, sizeof(*win)), **pp;
	static int cascade;
	int n = cascade++ % 6;

	snprintf(win->title, sizeof(win->title), "%s", title);
	win->app = app;
	if (w > dt.w - 40)
		w = dt.w - 40;
	if (h > dt.h - MENUBAR_H - dock_base - 40)
		h = dt.h - MENUBAR_H - dock_base - 40;
	win->w = w;
	win->h = h;
	win->x = (dt.w - w) / 2 - 60 + n * 26;
	win->y = MENUBAR_H + 30 + n * 24;
	if (win->x < 10)
		win->x = 10;
	win->min_w = 160;
	win->min_h = 100;
	win->dirty = 1;
	win->anim = ANIM_OPEN;
	win->anim_t0 = now_s();
	win->pressed_btn = -1;
	for (pp = &dt.windows; *pp; pp = &(*pp)->next)
		;
	*pp = win;
	wm_focus(win);
	return win;
}

static void wm_unlink(window_t *win)
{
	window_t **pp;

	for (pp = &dt.windows; *pp; pp = &(*pp)->next) {
		if (*pp == win) {
			*pp = win->next;
			break;
		}
	}
	win->next = NULL;
}

static window_t *top_visible(void)
{
	window_t *w, *top = NULL;

	for (w = dt.windows; w; w = w->next)
		if (!w->minimized && w->anim != ANIM_CLOSE && w->anim != ANIM_MINIMIZE)
			top = w;
	return top;
}

static void wm_finalize(window_t *win)
{
	wm_unlink(win);
	if (drag_win == win)
		drag_win = NULL;
	if (resize_win == win)
		resize_win = NULL;
	if (grab_win == win)
		grab_win = NULL;
	if (btn_win == win)
		btn_win = NULL;
	if (win->destroy)
		win->destroy(win);
	surf_free(win->surf);
	surf_free(win->shadow);
	free(win);
}

void wm_close(window_t *win)
{
	if (win->anim == ANIM_CLOSE)
		return;
	win->anim = ANIM_CLOSE;
	win->anim_t0 = now_s();
	if (dt.focus == win) {
		dt.focus = NULL;
		wm_focus(top_visible());
	}
	menubar_dirty = 1;
	dt.redraw = 1;
}

void wm_focus(window_t *win)
{
	window_t *old = dt.focus;

	if (win && (win->minimized || win->anim == ANIM_CLOSE))
		return;
	if (win && win->next) {
		wm_unlink(win);
		window_t **pp;

		for (pp = &dt.windows; *pp; pp = &(*pp)->next)
			;
		*pp = win;
	}
	dt.focus = win;
	if (old != win) {
		if (old)
			old->dirty = 1;
		if (win)
			win->dirty = 1;
	}
	menubar_dirty = 1;
	dt.redraw = 1;
}

void wm_damage(window_t *win)
{
	win->dirty = 1;
	dt.redraw = 1;
}

void wm_set_title(window_t *win, const char *title)
{
	snprintf(win->title, sizeof(win->title), "%s", title);
	wm_damage(win);
	menubar_dirty = 1;
}

void wm_minimize(window_t *win)
{
	if (win->minimized || win->anim == ANIM_MINIMIZE)
		return;
	win->anim = ANIM_MINIMIZE;
	win->anim_t0 = now_s();
	if (dt.focus == win) {
		dt.focus = NULL;
		wm_focus(top_visible());
	}
	dt.redraw = 1;
}

void wm_restore(window_t *win)
{
	win->minimized = 0;
	win->anim = ANIM_RESTORE;
	win->anim_t0 = now_s();
	wm_focus(win);
}

void wm_zoom(window_t *win)
{
	if (!win->resizable)
		return;
	if (win->zoomed) {
		win->x = win->saved[0];
		win->y = win->saved[1];
		win->w = win->saved[2];
		win->h = win->saved[3];
	} else {
		win->saved[0] = win->x;
		win->saved[1] = win->y;
		win->saved[2] = win->w;
		win->saved[3] = win->h;
		win->x = 8;
		win->y = MENUBAR_H + 6;
		win->w = dt.w - 16;
		win->h = dt.h - MENUBAR_H - dock_base - 34;
	}
	win->zoomed = !win->zoomed;
	if (win->resized)
		win->resized(win);
	wm_damage(win);
}

window_t *wm_find_app(enum app_id app)
{
	window_t *w;

	for (w = dt.windows; w; w = w->next)
		if (w->app == app && w->anim != ANIM_CLOSE)
			return w;
	return NULL;
}

int wm_animating(void)
{
	window_t *w;
	int i;

	for (w = dt.windows; w; w = w->next)
		if (w->anim != ANIM_NONE)
			return 1;
	for (i = 0; i < ndock; i++)
		if (dock[i].bounce_t0 > 0)
			return 1;
	return 0;
}

/* Scale down the alpha of pixels outside a rounded rectangle. */
static void round_corners(surface_t *s, float r)
{
	int x, y, c;

	for (c = 0; c < 4; c++) {
		int x0 = (c & 1) ? s->w - (int)r - 1 : 0;
		int y0 = (c & 2) ? s->h - (int)r - 1 : 0;

		for (y = y0; y < y0 + (int)r + 1; y++) {
			for (x = x0; x < x0 + (int)r + 1; x++) {
				float cx = (c & 1) ? s->w - r : r;
				float cy = (c & 2) ? s->h - r : r;
				float px = x + 0.5f, py = y + 0.5f, d, cov;
				uint32_t v, k;

				if (((c & 1) ? px < cx : px > cx) ||
				    ((c & 2) ? py < cy : py > cy))
					continue;
				d = hypotf(px - cx, py - cy) - r;
				cov = 0.5f - d;
				if (cov >= 1)
					continue;
				if (cov < 0)
					cov = 0;
				k = (uint32_t)(cov * 256);
				v = s->px[(size_t)y * s->w + x];
				s->px[(size_t)y * s->w + x] =
					((((v >> 24) & 255) * k >> 8) << 24) |
					((((v >> 16) & 255) * k >> 8) << 16) |
					((((v >> 8) & 255) * k >> 8) << 8) |
					((v & 255) * k >> 8);
			}
		}
	}
}

static void draw_window(window_t *win)
{
	int active = win == dt.focus;
	surface_t *s;
	paint_t p;
	int i;

	if (!win->surf || win->surf->w != win->w || win->surf->h != win->h) {
		surf_free(win->surf);
		win->surf = surf_new(win->w, win->h);
	}
	s = win->surf;
	surf_clear(s, 0);

	p = paint_stripes(0xffeeeeee, 0xfff7f7f7, 2);
	fill_rect_paint(s, 0, 0, s->w, s->h, &p);
	p = paint_vgrad(0, TITLE_H, active ? 0x90ffffff : 0x60ffffff, 0x00ffffff);
	fill_rect_paint(s, 0, 0, s->w, TITLE_H, &p);
	fill_rect(s, 0, TITLE_H - 1, s->w, 1, active ? 0x40000000 : 0x24000000);

	for (i = 0; i < 3; i++) {
		int show = active && win->hover_btns;

		if (i == 2 && !win->resizable)
			show = 0;
		aqua_traffic(s, 14.5f + 21 * i, 11, 6.5f, i, active, show);
	}
	{
		int tw = text_width(fonts[FONT_UI], 13, win->title);
		int tx = (s->w - tw) / 2;

		if (tx < 76)
			tx = 76;
		gfx_clip(s, 0, 0, s->w - 8, TITLE_H);
		draw_text(s, fonts[FONT_UI], 13, tx, 15,
			  active ? 0xff1a1a1a : 0xff8a8a8a, win->title);
		gfx_noclip();
	}

	if (win->draw) {
		int cx = 1, cy = TITLE_H, cw = s->w - 2, ch = s->h - TITLE_H - 1;

		gfx_clip(s, cx, cy, cw, ch);
		win->draw(win, s, cx, cy, cw, ch);
		gfx_noclip();
	}

	if (win->resizable) {
		p = paint_solid(0x80000000);
		for (i = 0; i < 3; i++) {
			float o = 4 + i * 4;

			draw_line(s, s->w - o, s->h - 3, s->w - 3, s->h - o, 1, &p);
		}
	}

	round_corners(s, 7);
	p = paint_solid(active ? 0x70000000 : 0x48000000);
	stroke_rrect(s, 0, 0, s->w, s->h, 7, 1, &p);
	win->dirty = 0;
}

static void ensure_shadow(window_t *win)
{
	int active = win == dt.focus;

	if (win->shadow && win->shadow_w == win->w && win->shadow_h == win->h &&
	    win->shadow_active == active)
		return;
	if (win->shadow && resize_win == win)
		return;		/* stretch the old one while resizing */
	surf_free(win->shadow);
	win->shadow = make_shadow(win->w, win->h, 7, 28, active ? 27 : 21,
				  active ? 0xe0000000 : 0x90000000);
	win->shadow_w = win->w;
	win->shadow_h = win->h;
	win->shadow_active = active;
}

/* ---------------------------------------------------------------------- */
/* Dock                                                                   */

static surface_t *render_icon(void (*fn)(surface_t *, float, float, float),
			      int size)
{
	surface_t *s = surf_new(size, size);

	fn(s, 0, 0, size);
	return s;
}

static void update_clock_icon(void)
{
	time_t t = time(NULL);
	struct tm tm;
	int i;

	localtime_r(&t, &tm);
	if (tm.tm_min == clock_icon_minute)
		return;
	clock_icon_minute = tm.tm_min;
	for (i = 0; i < NAPPS; i++) {
		if (apps_dock[i].app == APP_CLOCK) {
			surf_free(apps_dock[i].icon);
			apps_dock[i].icon = surf_new(dock_max, dock_max);
			icon_clock(apps_dock[i].icon, 0, 0, dock_max, tm.tm_hour,
				   tm.tm_min, -1);
		}
	}
	dt.redraw = 1;
}

static void dock_build(void)
{
	window_t *w;
	int i, n = 0;
	double bounces[APP_COUNT] = { 0 };

	for (i = 0; i < ndock; i++)
		if (dock[i].kind == DOCK_APP)
			bounces[dock[i].app] = dock[i].bounce_t0;
	for (i = 0; i < NAPPS; i++) {
		dock[n] = apps_dock[i];
		dock[n].bounce_t0 = bounces[apps_dock[i].app];
		n++;
	}
	dock[n++] = (struct dock_item){ .kind = DOCK_SEPARATOR };
	for (w = dt.windows; w && n < DOCK_MAX - 1; w = w->next) {
		if (w->minimized || w->anim == ANIM_MINIMIZE) {
			dock[n] = (struct dock_item){ .kind = DOCK_WINDOW,
						      .win = w, .name = w->title };
			n++;
		}
	}
	dock[n++] = trash_item;
	ndock = n;
}

static float item_base_w(const struct dock_item *it)
{
	return it->kind == DOCK_SEPARATOR ? 14 : dock_base;
}

static int pointer_in_dock_zone(void)
{
	return ptr_y > dt.h - dock_base - 26 && ptr_x > dock_panel_x - 20 &&
	       ptr_x < dock_panel_x + dock_panel_w + 20;
}

static void dock_layout(void)
{
	float base_total = 0, x, total = 0;
	float centers[DOCK_MAX];
	int i, magnify = dt.set.magnify_dock && pointer_in_dock_zone() && !button_down;

	for (i = 0; i < ndock; i++)
		base_total += item_base_w(&dock[i]) + 6;
	x = (dt.w - base_total) / 2;
	for (i = 0; i < ndock; i++) {
		centers[i] = x + item_base_w(&dock[i]) / 2 + 3;
		x += item_base_w(&dock[i]) + 6;
	}
	for (i = 0; i < ndock; i++) {
		float sz = item_base_w(&dock[i]);

		if (magnify && dock[i].kind != DOCK_SEPARATOR) {
			float d = (centers[i] - ptr_x) / (dock_base * 1.5f);

			sz += (dock_max - dock_base) * expf(-d * d);
		}
		dock[i].size = sz;
		total += sz + 6;
	}
	x = (dt.w - total) / 2;
	for (i = 0; i < ndock; i++) {
		dock[i].x = x + 3;
		x += dock[i].size + 6;
	}
	dock_panel_x = (dt.w - total) / 2 - 8;
	dock_panel_w = total + 16;
}

static int dock_hit(int x, int y)
{
	int i;

	if (y < dt.h - dock_base - 16 && !(y >= dt.h - dock_max - 16 &&
					    pointer_in_dock_zone()))
		return -1;
	for (i = 0; i < ndock; i++) {
		if (dock[i].kind == DOCK_SEPARATOR)
			continue;
		if (x >= dock[i].x - 3 && x < dock[i].x + dock[i].size + 3 &&
		    y >= dt.h - 8 - dock[i].size - 6)
			return i;
	}
	return -1;
}

static void dock_target(window_t *win, float *x, float *y, float *sz)
{
	int i;

	for (i = 0; i < ndock; i++) {
		if (dock[i].win == win) {
			*x = dock[i].x;
			*sz = dock[i].size;
			*y = dt.h - 8 - dock[i].size;
			return;
		}
	}
	*x = dt.w / 2.0f;
	*y = dt.h - dock_base;
	*sz = dock_base;
}

void wm_dock_bounce(enum app_id app)
{
	int i;

	for (i = 0; i < ndock; i++)
		if (dock[i].kind == DOCK_APP && dock[i].app == app)
			dock[i].bounce_t0 = now_s();
	dt.redraw = 1;
}

static int app_running(enum app_id app)
{
	window_t *w;

	for (w = dt.windows; w; w = w->next)
		if (w->app == app && w->anim != ANIM_CLOSE)
			return 1;
	return 0;
}

static void draw_dock(surface_t *f)
{
	float top = dt.h - dock_base - 14;
	paint_t p;
	int i;

	p = paint_vgrad(top, dt.h, 0x60ffffff, 0x38ffffff);
	fill_rrect(f, dock_panel_x, top, dock_panel_w, dt.h - top + 12, 9, &p);
	p = paint_stripes(0x14ffffff, 0x00ffffff, 2);
	fill_rrect(f, dock_panel_x, top, dock_panel_w, dt.h - top + 12, 9, &p);
	p = paint_solid(0x90ffffff);
	stroke_rrect(f, dock_panel_x, top, dock_panel_w, dt.h - top + 12, 9, 1, &p);
	p = paint_solid(0x40000000);
	stroke_rrect(f, dock_panel_x - 1, top - 1, dock_panel_w + 2, dt.h - top + 14,
		     10, 1, &p);

	for (i = 0; i < ndock; i++) {
		struct dock_item *it = &dock[i];
		float sz = it->size, iy = dt.h - 9 - sz;

		if (it->kind == DOCK_SEPARATOR) {
			int yy;

			for (yy = (int)top + 6; yy < dt.h - 6; yy += 4)
				fill_rect(f, (int)(it->x + sz / 2), yy, 1, 2, 0xb0ffffff);
			continue;
		}
		if (it->bounce_t0 > 0) {
			double bt = dt.now - it->bounce_t0;

			if (bt > 1.4)
				it->bounce_t0 = 0;
			else
				iy -= fabs(sin(bt * M_PI * 2.2)) * dock_base * 0.45 *
				      (1.4 - bt) / 1.4;
		}
		if (it->kind == DOCK_WINDOW && it->win->surf) {
			surface_t *ws = it->win->surf;
			float k = sz / (ws->w > ws->h ? ws->w : ws->h);
			float tw = ws->w * k, th = ws->h * k;

			blit_scaled(f, ws, it->x + (sz - tw) / 2, iy + (sz - th),
				    tw, th, 255);
			continue;
		}
		if (it->icon)
			blit_scaled(f, it->icon, it->x, iy, sz, sz, 255);
		if (it->kind == DOCK_APP && app_running(it->app)) {
			float cx = it->x + sz / 2, tri[] = { cx - 4, dt.h - 2.0f, cx + 4,
							      dt.h - 2.0f, cx, dt.h - 6.0f };

			p = paint_solid(0xff101010);
			fill_poly(f, tri, 3, &p);
		}
	}

	if (dock_hover >= 0 && dock_hover < ndock && !button_down) {
		struct dock_item *it = &dock[dock_hover];
		const char *name = it->kind == DOCK_WINDOW ? it->win->title : it->name;
		int tw = text_width(fonts[FONT_UI_BOLD], 14, name);
		int tx = (int)(it->x + it->size / 2 - tw / 2.0f);
		int ty = (int)(dt.h - 9 - it->size - 10);

		if (tx < 6)
			tx = 6;
		if (tx + tw > dt.w - 6)
			tx = dt.w - 6 - tw;
		draw_text_shadow(f, fonts[FONT_UI_BOLD], 14, tx, ty, 0xffffffff,
				 0xc0000000, name);
	}
}

/* ---------------------------------------------------------------------- */
/* Menu bar                                                               */

static void act_about(void) { app_about(); }
static void act_prefs(void) { app_prefs(); }
static void act_restart(void) { wc_shutdown(1); }
static void act_shutdown(void) { wc_shutdown(0); }
static void act_new_files(void) { app_files("/root", "Home"); }
static void act_new_term(void) { app_terminal(); }
static void act_clock(void) { app_clock(); }
static void act_close(void) { if (dt.focus) wm_close(dt.focus); }
static void act_minimize(void) { if (dt.focus) wm_minimize(dt.focus); }
static void act_zoom(void) { if (dt.focus) wm_zoom(dt.focus); }
static void act_go_computer(void) { app_files("/", "Water HD"); }
static void act_go_home(void) { app_files("/root", "Home"); }
static void act_go_apps(void) { app_files("/bin", "Applications"); }
static void act_go_utils(void) { app_files("/sbin", "Utilities"); }

static void act_quit_app(void)
{
	enum app_id app = dt.focus ? dt.focus->app : APP_NONE;
	window_t *w;

	for (w = dt.windows; w; w = w->next)
		if (w->app == app)
			wm_close(w);
}

static enum app_id front_app(void)
{
	return dt.focus ? dt.focus->app : APP_NONE;
}

static struct menu *add_menu(const char *title)
{
	struct menu *m = &menus[nmenus++];

	memset(m, 0, sizeof(*m));
	m->title = title;
	return m;
}

static void add_item(struct menu *m, const char *label, const char *key,
		     int enabled, void (*act)(void))
{
	struct mitem *it;

	if (m->n >= MAX_ITEMS)
		return;
	it = &m->items[m->n++];
	it->label = label;
	it->key = key;
	it->enabled = enabled;
	it->act = act;
	it->win = NULL;
}

static void build_menus(void)
{
	static char about_app[64], quit_app[64];
	enum app_id app = front_app();
	int has_win = dt.focus != NULL;
	struct menu *m;
	window_t *w;

	nmenus = 0;
	m = add_menu("");
	add_item(m, "About This Computer", NULL, 1, act_about);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "System Preferences...", NULL, 1, act_prefs);
	add_item(m, "Dock", NULL, 0, NULL);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Restart...", NULL, 1, act_restart);
	add_item(m, "Shut Down...", NULL, 1, act_shutdown);

	m = add_menu(app_names[app]);
	snprintf(about_app, sizeof(about_app), "About %s", app_names[app]);
	snprintf(quit_app, sizeof(quit_app), "Quit %s", app_names[app]);
	add_item(m, about_app, NULL, 1, act_about);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Preferences...", NULL, 1, act_prefs);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Hide Others", NULL, 0, NULL);
	add_item(m, "Show All", NULL, 0, NULL);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, quit_app, "\xe2\x8c\x98Q", app != APP_NONE && has_win,
		 act_quit_app);

	m = add_menu("File");
	add_item(m, "New Files Window", "\xe2\x8c\x98N", 1, act_new_files);
	add_item(m, "New Terminal", "\xe2\x8c\x98T", 1, act_new_term);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Close Window", "\xe2\x8c\x98W", has_win, act_close);

	m = add_menu("Edit");
	add_item(m, "Undo", "\xe2\x8c\x98Z", 0, NULL);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Cut", "\xe2\x8c\x98X", 0, NULL);
	add_item(m, "Copy", "\xe2\x8c\x98" "C", 0, NULL);
	add_item(m, "Paste", "\xe2\x8c\x98V", 0, NULL);
	add_item(m, "Select All", "\xe2\x8c\x98" "A", 0, NULL);

	m = add_menu("View");
	add_item(m, "as Icons", NULL, 0, NULL);
	add_item(m, "as List", NULL, 0, NULL);
	add_item(m, "-", NULL, 0, NULL);
	add_item(m, "Show Clock", NULL, 1, act_clock);

	m = add_menu("Go");
	add_item(m, "Computer", "\xe2\x8c\x98\xe2\x87\xa7" "C", 1, act_go_computer);
	add_item(m, "Home", "\xe2\x8c\x98\xe2\x87\xa7H", 1, act_go_home);
	add_item(m, "Applications", NULL, 1, act_go_apps);
	add_item(m, "Utilities", NULL, 1, act_go_utils);

	m = add_menu("Window");
	add_item(m, "Minimize Window", "\xe2\x8c\x98M", has_win, act_minimize);
	add_item(m, "Zoom Window", NULL, has_win && dt.focus->resizable, act_zoom);
	add_item(m, "-", NULL, 0, NULL);
	for (w = dt.windows; w; w = w->next) {
		if (w->anim == ANIM_CLOSE || m->n >= MAX_ITEMS)
			continue;
		add_item(m, w->title, w == dt.focus ? "\xe2\x9c\x93" : NULL, 1, NULL);
		m->items[m->n - 1].win = w;
	}

	m = add_menu("Help");
	add_item(m, WC_NAME " Help", "\xe2\x8c\x98?", 0, NULL);
}

static void draw_menubar(void)
{
	surface_t *s;
	paint_t p;
	int i, x;
	time_t t = time(NULL);
	struct tm tm;

	if (!menubar_surf)
		menubar_surf = surf_new(dt.w, MENUBAR_H);
	s = menubar_surf;
	surf_clear(s, 0);
	build_menus();

	p = paint_stripes(0xf4fbfbfb, 0xf4f1f1f1, 2);
	fill_rect_paint(s, 0, 0, s->w, s->h, &p);
	p = paint_vgrad(0, MENUBAR_H, 0x70ffffff, 0x00ffffff);
	fill_rect_paint(s, 0, 0, s->w, s->h, &p);
	fill_rect(s, 0, MENUBAR_H - 1, s->w, 1, 0x80606060);

	x = 8;
	for (i = 0; i < nmenus; i++) {
		struct menu *m = &menus[i];
		font_t *f = i == 1 ? fonts[FONT_UI_BOLD] : fonts[FONT_UI];
		int w = i == 0 ? 30 : text_width(f, 14, m->title) + 22;

		m->x = x;
		m->w = w;
		if (open_menu == i)
			aqua_highlight(s, x, 0, w, MENUBAR_H - 1, 0);
		if (i == 0)
			icon_drop(s, x + 5, 1, 20);
		else
			draw_text(s, f, 14, x + 11, 16,
				  open_menu == i ? 0xffffffff : 0xff000000, m->title);
		x += w;
	}

	localtime_r(&t, &tm);
	strftime(menubar_clock, sizeof(menubar_clock),
		 dt.set.show_seconds ? "%a %l:%M:%S %p" : "%a %l:%M %p", &tm);
	{
		const char *c = menubar_clock;
		char buf[64];

		/* collapse the padding space strftime adds for %l */
		snprintf(buf, sizeof(buf), "%s", c);
		{
			char *dbl = strstr(buf, "  ");

			if (dbl)
				memmove(dbl, dbl + 1, strlen(dbl));
		}
		draw_text(s, fonts[FONT_UI], 14,
			  s->w - 16 - text_width(fonts[FONT_UI], 14, buf), 16,
			  0xff000000, buf);
	}
	menubar_dirty = 0;
}

static int item_y(const struct menu *m, int idx)
{
	int y = 4, i;

	for (i = 0; i < idx; i++)
		y += strcmp(m->items[i].label, "-") ? ITEM_H : SEP_H;
	return y;
}

static void draw_dropdown(void)
{
	struct menu *m = &menus[open_menu];
	int i, w = 150, kw = 0, h;
	paint_t p;
	surface_t *s;

	for (i = 0; i < m->n; i++) {
		int tw = text_width(fonts[FONT_UI], 14, m->items[i].label);

		if (tw + 44 > w)
			w = tw + 44;
		if (m->items[i].key) {
			int k = text_width(fonts[FONT_UI], 14, m->items[i].key);

			if (k > kw)
				kw = k;
		}
	}
	w += kw + (kw ? 20 : 0);
	h = item_y(m, m->n) + 5;
	dd_x = m->x;
	if (dd_x + w > dt.w - 2)
		dd_x = dt.w - 2 - w;
	dd_y = MENUBAR_H - 1;
	if (!dropdown_surf || dd_w != w || dd_h != h) {
		surf_free(dropdown_surf);
		surf_free(dropdown_shadow);
		dropdown_surf = surf_new(w, h);
		dropdown_shadow = make_shadow(w, h, 5, 16, 14, 0x70000000);
	}
	dd_w = w;
	dd_h = h;
	s = dropdown_surf;
	surf_clear(s, 0);
	p = paint_stripes(0xf2fbfbfb, 0xf2f1f1f1, 2);
	fill_rrect(s, 0, -8, w, h + 8, 6, &p);
	p = paint_solid(0x50000000);
	stroke_rrect(s, 0, -8, w, h + 8, 6, 1, &p);

	for (i = 0; i < m->n; i++) {
		struct mitem *it = &m->items[i];
		int y = item_y(m, i);
		uint32_t col = it->enabled ? 0xff000000 : 0xffa2a2a2;

		if (!strcmp(it->label, "-")) {
			fill_rect(s, 1, y + SEP_H / 2, w - 2, 1, 0x30000000);
			continue;
		}
		if (i == hover_item && it->enabled) {
			aqua_highlight(s, 1, y, w - 2, ITEM_H, 0);
			col = 0xffffffff;
		}
		gfx_clip(s, 0, y, w - (it->key ? kw + 26 : 8), ITEM_H);
		draw_text(s, fonts[FONT_UI], 14, 20, y + 15, col, it->label);
		gfx_noclip();
		if (it->key)
			draw_text(s, fonts[FONT_UI], 14, w - 14 - kw, y + 15, col,
				  it->key);
	}
	dropdown_dirty = 0;
}

static int dropdown_item_at(int x, int y)
{
	struct menu *m;
	int i;

	if (open_menu < 0)
		return -1;
	m = &menus[open_menu];
	if (x < dd_x || x >= dd_x + dd_w || y < dd_y || y >= dd_y + dd_h)
		return -1;
	for (i = 0; i < m->n; i++) {
		int iy = dd_y + item_y(m, i);
		int ih = strcmp(m->items[i].label, "-") ? ITEM_H : SEP_H;

		if (y >= iy && y < iy + ih)
			return strcmp(m->items[i].label, "-") ? i : -1;
	}
	return -1;
}

static int menubar_title_at(int x)
{
	int i;

	for (i = 0; i < nmenus; i++)
		if (x >= menus[i].x && x < menus[i].x + menus[i].w)
			return i;
	return -1;
}

static void open_menu_at(int idx)
{
	if (idx == open_menu)
		return;
	open_menu = idx;
	hover_item = -1;
	menubar_dirty = 1;
	dropdown_dirty = 1;
	dt.redraw = 1;
}

/* ---------------------------------------------------------------------- */
/* Hit testing and events                                                 */

static window_t *window_at(int x, int y)
{
	window_t *w, *hit = NULL;

	for (w = dt.windows; w; w = w->next) {
		if (w->minimized || w->anim == ANIM_CLOSE || w->anim == ANIM_MINIMIZE)
			continue;
		if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
			hit = w;
	}
	return hit;
}

static int traffic_at(window_t *w, int x, int y)
{
	int i;

	for (i = 0; i < 3; i++) {
		float cx = w->x + 14.5f + 21 * i, cy = w->y + 11;

		if (hypotf(x - cx, y - cy) <= 8)
			return i;
	}
	return -1;
}

static void dock_click(int i)
{
	struct dock_item *it = &dock[i];
	window_t *w;

	if (it->kind == DOCK_WINDOW) {
		wm_restore(it->win);
		return;
	}
	if (it->kind == DOCK_TRASH) {
		app_files("/root/.Trash", "Trash");
		return;
	}
	/* Restore a minimized window of this app, otherwise launch. */
	for (w = dt.windows; w; w = w->next) {
		if (w->app == it->app && w->minimized) {
			wm_restore(w);
			return;
		}
	}
	w = wm_find_app(it->app);
	if (w && it->app != APP_TERMINAL && it->app != APP_FILES) {
		wm_focus(w);
		return;
	}
	if (!w)
		it->bounce_t0 = dt.now;
	app_launch(it->app);
}

static void handle_button(const event_t *ev)
{
	window_t *w;
	int i;

	button_down = ev->pressed;
	if (!ev->pressed) {
		if (open_menu >= 0) {
			int it = dropdown_item_at(ev->x, ev->y);

			if (it >= 0) {
				struct mitem *mi = &menus[open_menu].items[it];

				open_menu_at(-1);
				if (mi->enabled && mi->win) {
					if (mi->win->minimized)
						wm_restore(mi->win);
					else
						wm_focus(mi->win);
				} else if (mi->enabled && mi->act) {
					mi->act();
				}
			}
			return;
		}
		if (btn_win) {
			int b = traffic_at(btn_win, ev->x, ev->y);
			window_t *bw = btn_win;

			btn_win = NULL;
			if (b == bw->pressed_btn) {
				if (b == 0)
					wm_close(bw);
				else if (b == 1)
					wm_minimize(bw);
				else if (b == 2)
					wm_zoom(bw);
			}
			bw->pressed_btn = -1;
			wm_damage(bw);
		}
		if (grab_win && grab_win->mouse) {
			grab_win->mouse(grab_win, ev, ev->x - grab_win->x - 1,
					ev->y - grab_win->y - TITLE_H);
		}
		if (resize_win) {
			window_t *rw = resize_win;

			resize_win = NULL;
			rw->dirty = 1;
		}
		drag_win = NULL;
		grab_win = NULL;
		dt.redraw = 1;
		return;
	}

	/* press */
	if (ev->button != 1 && !(ev->y >= MENUBAR_H && window_at(ev->x, ev->y)))
		return;
	if (open_menu >= 0) {
		int t = ev->y < MENUBAR_H ? menubar_title_at(ev->x) : -1;

		if (dropdown_item_at(ev->x, ev->y) >= 0 ||
		    (ev->x >= dd_x && ev->x < dd_x + dd_w && ev->y >= dd_y &&
		     ev->y < dd_y + dd_h))
			return;
		open_menu_at(t == open_menu ? -1 : t);
		return;
	}
	if (ev->y < MENUBAR_H) {
		open_menu_at(menubar_title_at(ev->x));
		return;
	}

	i = dock_hit(ev->x, ev->y);
	if (i >= 0) {
		dock_click(i);
		return;
	}

	w = window_at(ev->x, ev->y);
	if (w) {
		int lx = ev->x - w->x, ly = ev->y - w->y;
		int b;

		if (disk_selected) {
			disk_selected = 0;
			dt.redraw = 1;
		}
		if (dt.focus != w)
			wm_focus(w);
		if (ev->button != 1) {
			if (ly >= TITLE_H && w->mouse)
				w->mouse(w, ev, lx - 1, ly - TITLE_H);
			return;
		}
		b = traffic_at(w, ev->x, ev->y);
		if (b >= 0 && (b != 2 || w->resizable)) {
			btn_win = w;
			w->pressed_btn = b;
			return;
		}
		if (w->resizable && lx >= w->w - GROW_BOX && ly >= w->h - GROW_BOX) {
			resize_win = w;
			rs_w = w->w;
			rs_h = w->h;
			rs_mx = ev->x;
			rs_my = ev->y;
			return;
		}
		if (ly < TITLE_H) {
			if (dt.now - last_click_t < 0.4 && abs(ev->x - last_click_x) < 4 &&
			    abs(ev->y - last_click_y) < 4) {
				wm_minimize(w);
				last_click_t = 0;
				return;
			}
			last_click_t = dt.now;
			last_click_x = ev->x;
			last_click_y = ev->y;
			drag_win = w;
			drag_dx = lx;
			drag_dy = ly;
			return;
		}
		grab_win = w;
		if (w->mouse)
			w->mouse(w, ev, lx - 1, ly - TITLE_H);
		return;
	}

	/* desktop */
	if (ev->x >= disk_x - 10 && ev->x < disk_x + DESK_ICON + 10 &&
	    ev->y >= disk_y && ev->y < disk_y + DESK_ICON + 22) {
		if (disk_selected && dt.now - last_click_t < 0.45)
			act_go_computer();
		disk_selected = 1;
	} else {
		disk_selected = 0;
	}
	last_click_t = dt.now;
	if (dt.focus) {
		window_t *old = dt.focus;

		dt.focus = NULL;
		old->dirty = 1;
		menubar_dirty = 1;
	}
	dt.redraw = 1;
}

static void handle_motion(const event_t *ev)
{
	window_t *w;
	int hover_dock;

	ptr_x = ev->x;
	ptr_y = ev->y;
	dt.redraw = 1;

	if (open_menu >= 0) {
		int it = dropdown_item_at(ev->x, ev->y);

		if (ev->y < MENUBAR_H) {
			int t = menubar_title_at(ev->x);

			if (t >= 0)
				open_menu_at(t);
		}
		if (it != hover_item) {
			hover_item = it;
			dropdown_dirty = 1;
		}
		return;
	}
	if (drag_win) {
		drag_win->x = ev->x - drag_dx;
		drag_win->y = ev->y - drag_dy;
		if (drag_win->y < MENUBAR_H)
			drag_win->y = MENUBAR_H;
		if (drag_win->y > dt.h - 40)
			drag_win->y = dt.h - 40;
		return;
	}
	if (resize_win) {
		int nw = rs_w + ev->x - rs_mx, nh = rs_h + ev->y - rs_my;

		if (nw < resize_win->min_w)
			nw = resize_win->min_w;
		if (nh < resize_win->min_h)
			nh = resize_win->min_h;
		if (nw != resize_win->w || nh != resize_win->h) {
			resize_win->w = nw;
			resize_win->h = nh;
			resize_win->zoomed = 0;
			if (resize_win->resized)
				resize_win->resized(resize_win);
			wm_damage(resize_win);
		}
		return;
	}
	if (grab_win) {
		if (grab_win->mouse)
			grab_win->mouse(grab_win, ev, ev->x - grab_win->x - 1,
					ev->y - grab_win->y - TITLE_H);
		return;
	}

	/* traffic light hover */
	for (w = dt.windows; w; w = w->next) {
		int over = w == dt.focus && ev->x >= w->x + 4 && ev->x < w->x + 70 &&
			   ev->y >= w->y + 2 && ev->y < w->y + 21 &&
			   window_at(ev->x, ev->y) == w;

		if (over != w->hover_btns) {
			w->hover_btns = over;
			w->dirty = 1;
		}
	}
	hover_dock = dock_hit(ev->x, ev->y);
	dock_hover = hover_dock;
	w = window_at(ev->x, ev->y);
	if (w && w == dt.focus && w->mouse && ev->y >= w->y + TITLE_H)
		w->mouse(w, ev, ev->x - w->x - 1, ev->y - w->y - TITLE_H);
}

static int is_key(const event_t *ev, unsigned ch)
{
	return (ev->mods & MOD_ALT) && (ev->ch == ch || ev->ch == ch - 32);
}

void wm_event(const event_t *ev)
{
	switch (ev->type) {
	case EV_MOTION:
		handle_motion(ev);
		break;
	case EV_BUTTON:
		ptr_x = ev->x;
		ptr_y = ev->y;
		handle_button(ev);
		dt.redraw = 1;
		break;
	case EV_SCROLL:
		{
			window_t *w = window_at(ev->x, ev->y);

			if (w && w->mouse)
				w->mouse(w, ev, ev->x - w->x - 1, ev->y - w->y - TITLE_H);
		}
		break;
	case EV_KEYPRESS:
		if (open_menu >= 0) {
			if (ev->ch == 27)
				open_menu_at(-1);
			return;
		}
		/* Alt (or the Windows key) acts as the Command key. */
		if (is_key(ev, 'w')) {
			act_close();
		} else if (is_key(ev, 'n')) {
			act_new_files();
		} else if (is_key(ev, 't')) {
			act_new_term();
		} else if (is_key(ev, 'm')) {
			act_minimize();
		} else if (is_key(ev, 'q')) {
			act_quit_app();
		} else if (dt.focus && dt.focus->key) {
			dt.focus->key(dt.focus, ev);
		}
		dt.redraw = 1;
		break;
	}
}

/* ---------------------------------------------------------------------- */
/* Main hooks                                                             */

void wm_init(void)
{
	int i;

	dock_base = dt.h >= 900 ? 60 : dt.h >= 700 ? 52 : 42;
	dock_max = dock_base * 3 / 2 + 8;

	for (i = 0; i < NAPPS; i++) {
		switch (apps_dock[i].app) {
		case APP_FILES:
			apps_dock[i].icon = render_icon(icon_folder, dock_max);
			break;
		case APP_TERMINAL:
			apps_dock[i].icon = render_icon(icon_terminal, dock_max);
			break;
		case APP_PREFS:
			apps_dock[i].icon = render_icon(icon_gear, dock_max);
			break;
		default:
			break;
		}
	}
	trash_item.icon = render_icon(icon_trash, dock_max);
	update_clock_icon();

	cursor_surf = make_cursor();
	ptr_x = dt.w / 2;
	ptr_y = dt.h / 2;
	disk_icon = render_icon(icon_disk, DESK_ICON);
	disk_x = dt.w - DESK_ICON - 34;
	disk_y = MENUBAR_H + 18;
	dock_build();
	dock_layout();
	draw_menubar();
}

void wm_tick(void)
{
	static char last_clock[64];
	window_t *w, *next;
	time_t t = time(NULL);
	struct tm tm;
	char buf[64];

	for (w = dt.windows; w; w = next) {
		next = w->next;
		if (w->tick)
			w->tick(w);
		switch (w->anim) {
		case ANIM_OPEN:
		case ANIM_RESTORE:
			if (dt.now - w->anim_t0 > (w->anim == ANIM_OPEN ? 0.22 : 0.35))
				w->anim = ANIM_NONE;
			break;
		case ANIM_CLOSE:
			if (dt.now - w->anim_t0 > 0.18)
				wm_finalize(w);
			break;
		case ANIM_MINIMIZE:
			if (dt.now - w->anim_t0 > 0.35) {
				w->anim = ANIM_NONE;
				w->minimized = 1;
			}
			break;
		default:
			break;
		}
	}
	update_clock_icon();

	localtime_r(&t, &tm);
	strftime(buf, sizeof(buf), dt.set.show_seconds ? "%T" : "%H:%M", &tm);
	if (strcmp(buf, last_clock)) {
		strcpy(last_clock, buf);
		menubar_dirty = 1;
		dt.redraw = 1;
	}
}

static void draw_window_animated(surface_t *f, window_t *w)
{
	float k = 1, x = w->x, y = w->y, ww = w->w, hh = w->h;
	int op = 255;

	switch (w->anim) {
	case ANIM_OPEN:
		k = ease((float)((dt.now - w->anim_t0) / 0.22));
		op = (int)(255 * k);
		ww = w->w * (0.86f + 0.14f * k);
		hh = w->h * (0.86f + 0.14f * k);
		x = w->x + (w->w - ww) / 2;
		y = w->y + (w->h - hh) / 2;
		break;
	case ANIM_CLOSE:
		k = 1 - ease((float)((dt.now - w->anim_t0) / 0.18));
		op = (int)(255 * k);
		ww = w->w * (0.9f + 0.1f * k);
		hh = w->h * (0.9f + 0.1f * k);
		x = w->x + (w->w - ww) / 2;
		y = w->y + (w->h - hh) / 2;
		break;
	case ANIM_MINIMIZE:
	case ANIM_RESTORE: {
		float tx, ty, tsz, t = ease((float)((dt.now - w->anim_t0) / 0.35));
		float sc;

		if (w->anim == ANIM_RESTORE)
			t = 1 - t;
		dock_target(w, &tx, &ty, &tsz);
		sc = tsz / (w->w > w->h ? w->w : w->h);
		ww = w->w + (w->w * sc - w->w) * t;
		hh = w->h + (w->h * sc - w->h) * t;
		x = w->x + (tx - w->x) * t;
		y = w->y + (ty + tsz - w->h * sc - w->y) * t;
		op = (int)(255 - 60 * t);
		break;
	}
	default:
		break;
	}

	if (w->anim == ANIM_NONE) {
		ensure_shadow(w);
		if (w->shadow_w == w->w && w->shadow_h == w->h)
			blit(f, w->shadow, w->x - 28, w->y - 28 + 8, 255);
		else
			blit_scaled(f, w->shadow, w->x - 28, w->y - 20,
				    w->w + 56, w->h + 56, 255);
		blit(f, w->surf, w->x, w->y, 255);
	} else {
		ensure_shadow(w);
		blit_scaled(f, w->shadow, x - 28 * ww / w->w, y - 20 * hh / w->h,
			    (w->w + 56) * ww / w->w, (w->h + 56) * hh / w->h, op);
		blit_scaled(f, w->surf, x, y, ww, hh, op);
	}
}

void wm_render(void)
{
	surface_t *f = dt.frame;
	window_t *w;
	paint_t p;

	memcpy(f->px, dt.wall->px, (size_t)f->w * f->h * 4);

	/* desktop icon */
	if (disk_selected) {
		p = paint_solid(0x60000000);
		fill_rrect(f, disk_x - 4, disk_y - 4, DESK_ICON + 8, DESK_ICON + 8, 6, &p);
	}
	blit(f, disk_icon, disk_x, disk_y, 255);
	{
		const char *label = "Water HD";
		int tw = text_width(fonts[FONT_UI_BOLD], 13, label);
		int tx = disk_x + (DESK_ICON - tw) / 2, ty = disk_y + DESK_ICON + 14;

		if (disk_selected) {
			p = paint_solid(0xff2a5fc8);
			fill_rrect(f, tx - 4, ty - 12, tw + 8, 16, 8, &p);
		}
		draw_text_shadow(f, fonts[FONT_UI_BOLD], 13, tx, ty, 0xffffffff,
				 disk_selected ? 0 : 0xd0000000, label);
	}

	for (w = dt.windows; w; w = w->next) {
		if (w->minimized)
			continue;
		if (w->dirty || !w->surf)
			draw_window(w);
	}
	dock_build();
	dock_layout();
	if (dock_hover >= ndock)
		dock_hover = -1;
	for (w = dt.windows; w; w = w->next) {
		if (w->minimized)
			continue;
		draw_window_animated(f, w);
	}

	if (menubar_dirty)
		draw_menubar();
	p = paint_vgrad(MENUBAR_H, MENUBAR_H + 5, 0x38000000, 0x00000000);
	fill_rect_paint(f, 0, MENUBAR_H, f->w, 5, &p);
	blit(f, menubar_surf, 0, 0, 255);

	draw_dock(f);

	if (open_menu >= 0) {
		if (dropdown_dirty || !dropdown_surf)
			draw_dropdown();
		blit(f, dropdown_shadow, dd_x - 16, dd_y - 16 + 4, 255);
		blit(f, dropdown_surf, dd_x, dd_y, 255);
	}

	blit(f, cursor_surf, ptr_x - 3, ptr_y - 3, 255);
	dt.frames++;
}
