// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - notification banners in the top right corner, in the style
 * of Growl's "Smoke" display on Mac OS X.
 */
#include <stdio.h>
#include <string.h>

#include "wc.h"

#define NOTE_W 320
#define NOTE_H 74
#define NOTE_LIFE 7.0
#define MAX_NOTES 4

struct note {
	char title[96], text[192];
	void (*click)(void);
	double t0;
	int x, y;
};

static struct note notes[MAX_NOTES];
static int nnotes;
static surface_t *icon;

void notify(const char *title, const char *text, void (*click)(void))
{
	struct note *n;

	if (nnotes == MAX_NOTES) {
		memmove(notes, notes + 1, sizeof(notes[0]) * (MAX_NOTES - 1));
		nnotes--;
	}
	n = &notes[nnotes++];
	snprintf(n->title, sizeof(n->title), "%s", title);
	snprintf(n->text, sizeof(n->text), "%s", text);
	n->click = click;
	n->t0 = dt.now;
	dt.redraw = 1;
}

static int alpha_of(const struct note *n)
{
	double age = dt.now - n->t0;

	if (age < 0.3)
		return (int)(255 * age / 0.3);
	if (age > NOTE_LIFE - 0.8)
		return (int)(255 * (NOTE_LIFE - age) / 0.8);
	return 255;
}

int notify_animating(void)
{
	return nnotes > 0;
}

void notify_render(surface_t *f)
{
	int i, y = MENUBAR_H + 12;

	/* drop expired banners */
	for (i = 0; i < nnotes;) {
		if (dt.now - notes[i].t0 > NOTE_LIFE) {
			memmove(notes + i, notes + i + 1, sizeof(notes[0]) * (nnotes - i - 1));
			nnotes--;
		} else {
			i++;
		}
	}
	if (!icon) {
		icon = surf_new(44, 44);
		icon_drop(icon, 0, 0, 44);
	}
	for (i = 0; i < nnotes; i++) {
		struct note *n = &notes[i];
		int a = alpha_of(n), x = f->w - NOTE_W - 14;
		surface_t *s = surf_new(NOTE_W, NOTE_H);
		paint_t p = paint_solid(0xd8202020);

		fill_rrect(s, 0, 0, NOTE_W, NOTE_H, 10, &p);
		p = paint_vgrad(0, NOTE_H / 2, 0x30ffffff, 0x00ffffff);
		fill_rrect(s, 1, 1, NOTE_W - 2, NOTE_H / 2, 9, &p);
		blit(s, icon, 12, (NOTE_H - 44) / 2, 255);
		gfx_push_clip(s, 0, 0, NOTE_W - 10, NOTE_H);
		draw_text(s, fonts[FONT_UI_BOLD], 13, 66, 28, 0xffffffff, n->title);
		draw_text(s, fonts[FONT_UI], 12, 66, 48, 0xffd8d8d8, n->text);
		gfx_pop_clip();
		blit(f, s, x, y, a);
		surf_free(s);
		n->x = x;
		n->y = y;
		y += NOTE_H + 8;
	}
}

int notify_click(int x, int y)
{
	int i;

	for (i = 0; i < nnotes; i++) {
		struct note *n = &notes[i];

		if (x >= n->x && x < n->x + NOTE_W && y >= n->y && y < n->y + NOTE_H) {
			void (*click)(void) = n->click;

			memmove(notes + i, notes + i + 1, sizeof(notes[0]) * (nnotes - i - 1));
			nnotes--;
			if (click)
				click();
			dt.redraw = 1;
			return 1;
		}
	}
	return 0;
}
