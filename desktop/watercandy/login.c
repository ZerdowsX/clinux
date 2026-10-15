// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - login window, in the style of the Mac OS X login panel.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define PANEL_W 460
#define PANEL_H 330

enum { BTN_NONE, BTN_RESTART, BTN_SHUTDOWN, BTN_LOGIN };

static struct {
	int active;
	entry_t name, pass;
	double shake_t0;
	char error[128];
	int pressed;
	surface_t *panel, *shadow, *logo;
	int px, py;		/* panel position */
} L;

int login_active(void)
{
	return L.active;
}

void login_start(void)
{
	L.active = 1;
	L.name.placeholder = "Name";
	L.pass.placeholder = "Password";
	L.pass.password = 1;
	entry_set(&L.name, dt.uid ? dt.user : "");
	entry_set(&L.pass, "");
	L.name.focused = !L.name.text[0];
	L.pass.focused = !L.name.focused;
	L.error[0] = 0;
	dt.redraw = 1;
}

static void button_rect(int which, int *x, int *y, int *w)
{
	*y = PANEL_H - 44;
	*w = 120;
	if (which == BTN_RESTART)
		*x = 20;
	else if (which == BTN_SHUTDOWN)
		*x = 150;
	else
		*x = PANEL_W - 110, *w = 90;
}

static void draw_panel(void)
{
	surface_t *s;
	paint_t p;
	int bx, by, bw, b;

	if (!L.panel) {
		L.panel = surf_new(PANEL_W, PANEL_H);
		L.shadow = make_shadow(PANEL_W, PANEL_H, 8, 40, 36, 0xb0000000);
		L.logo = surf_new(72, 72);
		icon_drop(L.logo, 0, 0, 72);
	}
	s = L.panel;
	surf_clear(s, 0);
	p = paint_stripes(0xffeeeeee, 0xfff7f7f7, 2);
	fill_rrect(s, 0, 0, PANEL_W, PANEL_H, 8, &p);
	p = paint_vgrad(0, 120, 0xa0ffffff, 0x00ffffff);
	fill_rrect(s, 0, 0, PANEL_W, 120, 8, &p);
	p = paint_solid(0x60000000);
	stroke_rrect(s, 0, 0, PANEL_W, PANEL_H, 8, 1, &p);

	{
		int tw = text_width(fonts[FONT_SERIF], 40, WC_NAME);
		int lx = (PANEL_W - (72 + 14 + tw)) / 2;

		blit(s, L.logo, lx, 22, 255);
		draw_text(s, fonts[FONT_SERIF], 40, lx + 86, 74, 0xff101010, WC_NAME);
	}
	fill_rect(s, 20, 118, PANEL_W - 40, 1, 0x30000000);
	fill_rect(s, 20, 119, PANEL_W - 40, 1, 0x80ffffff);

	draw_text(s, fonts[FONT_UI], 13, 140 - text_width(fonts[FONT_UI], 13, _("Name:")),
		  157, 0xff000000, _("Name:"));
	entry_draw(s, &L.name, 150, 141, 240, 22);
	draw_text(s, fonts[FONT_UI], 13, 140 - text_width(fonts[FONT_UI], 13,
							   _("Password:")),
		  197, 0xff000000, _("Password:"));
	entry_draw(s, &L.pass, 150, 181, 240, 22);
	if (L.error[0])
		draw_text_center(s, fonts[FONT_UI], 12, 0, PANEL_W, 236, 0xffc02020,
				 L.error);

	for (b = BTN_RESTART; b <= BTN_LOGIN; b++) {
		static const char *const labels[] = { NULL, "Restart", "Shut Down",
						      "Log In" };

		button_rect(b, &bx, &by, &bw);
		aqua_button(s, bx, by, bw, 24, labels[b], b == BTN_LOGIN,
			    L.pressed == b);
	}
}

void login_render(surface_t *f)
{
	double t = dt.now - L.shake_t0;
	int dx = 0;

	if (t < 0.5)
		dx = (int)(sin(t * 45) * 14 * (1 - t / 0.5));
	L.px = (f->w - PANEL_W) / 2 + dx;
	L.py = (f->h - PANEL_H) / 2 - 20;
	draw_panel();
	blit(f, L.shadow, L.px - 40, L.py - 40 + 12, 255);
	blit(f, L.panel, L.px, L.py, 255);
	{
		char buf[64];
		time_t now = time(NULL);
		struct tm tm;
		int tw;

		localtime_r(&now, &tm);
		format_clock(buf, sizeof(buf), &tm, 0);
		tw = text_width(fonts[FONT_UI_BOLD], 14, buf);
		draw_text_shadow(f, fonts[FONT_UI_BOLD], 14, f->w - tw - 20, 24,
				 0xffffffff, 0xa0000000, buf);
		icon_flag(f, f->w - tw - 52, 12, 22, 15, input_layout());
	}
}

static void try_login(void)
{
	if (!L.name.text[0]) {
		L.name.focused = 1;
		L.pass.focused = 0;
		return;
	}
	if (user_check_password(L.name.text, L.pass.text) &&
	    !user_select(L.name.text)) {
		L.active = 0;
		entry_set(&L.pass, "");
		session_start();
	} else {
		snprintf(L.error, sizeof(L.error), "%s",
			 _("The name or password you entered is incorrect."));
		entry_set(&L.pass, "");
		L.shake_t0 = dt.now;
		L.pass.focused = 1;
		L.name.focused = 0;
	}
	dt.redraw = 1;
}

static int button_at(int x, int y)
{
	int b, bx, by, bw;

	x -= L.px;
	y -= L.py;
	for (b = BTN_RESTART; b <= BTN_LOGIN; b++) {
		button_rect(b, &bx, &by, &bw);
		if (x >= bx && x < bx + bw && y >= by && y < by + 24)
			return b;
	}
	return BTN_NONE;
}

void login_event(const event_t *ev)
{
	switch (ev->type) {
	case EV_BUTTON:
		if (ev->button != 1)
			break;
		if (ev->pressed) {
			int lx = ev->x - L.px, ly = ev->y - L.py;

			L.pressed = button_at(ev->x, ev->y);
			if (lx >= 150 && lx < 390 && ly >= 141 && ly < 163) {
				L.name.focused = 1;
				L.pass.focused = 0;
			} else if (lx >= 150 && lx < 390 && ly >= 181 && ly < 203) {
				L.name.focused = 0;
				L.pass.focused = 1;
			}
		} else {
			int b = button_at(ev->x, ev->y);

			if (b && b == L.pressed) {
				if (b == BTN_RESTART)
					wc_shutdown(1);
				else if (b == BTN_SHUTDOWN)
					wc_shutdown(0);
				else
					try_login();
			}
			L.pressed = BTN_NONE;
		}
		break;
	case EV_KEYPRESS: {
		entry_t *e = L.name.focused ? &L.name : &L.pass;
		int r = entry_key(e, ev);

		if (r == ENTRY_TAB || (r == ENTRY_ENTER && e == &L.name)) {
			L.name.focused = !L.name.focused;
			L.pass.focused = !L.pass.focused;
		} else if (r == ENTRY_ENTER) {
			try_login();
		}
		if (r == ENTRY_CHANGED)
			L.error[0] = 0;
		break;
	}
	default:
		break;
	}
	dt.redraw = 1;
}
