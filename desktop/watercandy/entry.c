// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - single-line Aqua text field.
 */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "wc.h"

void entry_set(entry_t *e, const char *text)
{
	snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
	e->cursor = strlen(e->text);
}

static int prev_char(const entry_t *e, int i)
{
	while (i > 0 && (e->text[--i] & 0xc0) == 0x80)
		;
	return i;
}

static int next_char(const entry_t *e, int i)
{
	int n = strlen(e->text);

	if (i >= n)
		return n;
	i++;
	while (i < n && (e->text[i] & 0xc0) == 0x80)
		i++;
	return i;
}

int entry_key(entry_t *e, const event_t *ev)
{
	int n = strlen(e->text);

	switch (ev->key) {
	case KEY_LEFT:
		e->cursor = prev_char(e, e->cursor);
		return ENTRY_CHANGED;
	case KEY_RIGHT:
		e->cursor = next_char(e, e->cursor);
		return ENTRY_CHANGED;
	case KEY_HOME:
		e->cursor = 0;
		return ENTRY_CHANGED;
	case KEY_END:
		e->cursor = n;
		return ENTRY_CHANGED;
	case KEY_DELETE:
		if (e->cursor < n) {
			int j = next_char(e, e->cursor);

			memmove(e->text + e->cursor, e->text + j, n - j + 1);
		}
		return ENTRY_CHANGED;
	case KEY_BACKSPACE:
		if (e->cursor > 0) {
			int j = prev_char(e, e->cursor);

			memmove(e->text + j, e->text + e->cursor, n - e->cursor + 1);
			e->cursor = j;
		}
		return ENTRY_CHANGED;
	case KEY_ENTER:
	case KEY_KPENTER:
		return ENTRY_ENTER;
	case KEY_ESC:
		return ENTRY_ESCAPE;
	case KEY_TAB:
		return ENTRY_TAB;
	}
	if (ev->ch >= ' ' && !(ev->mods & (MOD_CTRL | MOD_ALT))) {
		char u[8];
		int l = utf8_encode(ev->ch, u);

		if (n + l < (int)sizeof(e->text)) {
			memmove(e->text + e->cursor + l, e->text + e->cursor,
				n - e->cursor + 1);
			memcpy(e->text + e->cursor, u, l);
			e->cursor += l;
		}
		return ENTRY_CHANGED;
	}
	if ((ev->mods & MOD_CTRL) && ev->ch == 21) {	/* Ctrl+U */
		e->text[0] = 0;
		e->cursor = 0;
		return ENTRY_CHANGED;
	}
	return ENTRY_IGNORED;
}

/* What is shown: the text, or bullets for a password. */
static void shown_text(const entry_t *e, int upto, char *out, size_t sz)
{
	if (!e->password) {
		snprintf(out, sz, "%.*s", upto, e->text);
		return;
	}
	out[0] = 0;
	{
		const char *p = e->text;
		size_t len = 0;

		while (*p && p - e->text < upto && len + 4 < sz) {
			utf8_next(&p);
			memcpy(out + len, "\xe2\x80\xa2", 3);
			len += 3;
		}
		out[len] = 0;
	}
}

void entry_draw(surface_t *s, entry_t *e, int x, int y, int w, int h)
{
	char shown[1600], before[1600];
	int tx, cx, base = y + h / 2 + 5;
	paint_t p;

	if (e->focused) {
		p = paint_solid(0x907db4f0);
		fill_rrect(s, x - 3, y - 3, w + 6, h + 6, 4, &p);
	}
	p = paint_solid(0xff8e8e8e);
	fill_rect_paint(s, x, y, w, h, &p);
	p = paint_vgrad(y, y + 4, 0xffe8e8e8, 0xffffffff);
	fill_rect_paint(s, x + 1, y + 1, w - 2, h - 2, &p);

	shown_text(e, strlen(e->text), shown, sizeof(shown));
	shown_text(e, e->cursor, before, sizeof(before));
	cx = text_width(fonts[FONT_UI], 13, before);
	/* keep the cursor visible */
	if (cx - e->scroll > w - 12)
		e->scroll = cx - (w - 12);
	if (cx - e->scroll < 0)
		e->scroll = cx;
	tx = x + 5 - e->scroll;

	gfx_push_clip(s, x + 2, y + 1, w - 4, h - 2);
	if (!e->text[0] && e->placeholder)
		draw_text(s, fonts[FONT_UI], 13, x + 5, base, 0xffa0a0a0,
			  _(e->placeholder));
	else
		draw_text(s, fonts[FONT_UI], 13, tx, base, 0xff000000, shown);
	if (e->focused && ((int)(dt.now * 2) & 1) == 0)
		fill_rect(s, tx + cx, y + 4, 1, h - 8, 0xff000000);
	gfx_pop_clip();
}
