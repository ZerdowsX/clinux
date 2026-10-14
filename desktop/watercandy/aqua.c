// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Aqua-style widgets: gel buttons, checkboxes, sliders.
 */
#include <math.h>

#include "wc.h"

static uint32_t lighten(uint32_t c, float t)
{
	return color_lerp(c, (c & 0xff000000u) | 0xffffff, t);
}

void aqua_gel_circle(surface_t *s, float cx, float cy, float r, uint32_t base)
{
	paint_t p;

	p = paint_solid(shade(base, 0.45f));
	fill_circle(s, cx, cy, r + 0.6f, &p);
	p = paint_radial(cx, cy + r * 0.6f, r * 1.3f, lighten(base, 0.45f),
			 shade(base, 0.78f));
	fill_circle(s, cx, cy, r - 0.3f, &p);
	p = paint_vgrad(cy - r * 0.85f, cy, 0xf0ffffff, 0x18ffffff);
	fill_ellipse(s, cx, cy - r * 0.42f, r * 0.62f, r * 0.40f, &p);
}

void aqua_traffic(surface_t *s, float cx, float cy, float r, int which,
		  int active, int show_symbol)
{
	static const uint32_t colors[3] = { 0xffe2463c, 0xffeaa526, 0xff5ab53e };
	uint32_t base = active ? colors[which] : 0xffb9bdc3;
	paint_t p;
	float k = r * 0.42f;

	aqua_gel_circle(s, cx, cy, r, base);
	if (!show_symbol)
		return;
	p = paint_solid(ALPHA(shade(base, 0.32f), 220));
	if (which == 0) {
		draw_line(s, cx - k, cy - k, cx + k, cy + k, 1.4f, &p);
		draw_line(s, cx - k, cy + k, cx + k, cy - k, 1.4f, &p);
	} else {
		draw_line(s, cx - k - 0.6f, cy, cx + k + 0.6f, cy, 1.6f, &p);
		if (which == 2)
			draw_line(s, cx, cy - k - 0.6f, cx, cy + k + 0.6f, 1.6f, &p);
	}
}

void aqua_button(surface_t *s, int x, int y, int w, int h, const char *label,
		 int primary, int pressed)
{
	uint32_t base = primary ? 0xff3a8be6 : 0xfff0f0f0;
	float r = h / 2.0f;
	paint_t p;

	if (pressed)
		base = shade(base, 0.82f);
	p = paint_solid(0x30000000);
	fill_rrect(s, x, y + 1, w, h, r, &p);
	p = paint_solid(primary ? 0xff1c4f9c : 0xff8a8a8a);
	fill_rrect(s, x, y, w, h, r, &p);
	p = paint_vgrad(y, y + h, shade(base, primary ? 0.82f : 0.93f),
			lighten(base, primary ? 0.55f : 1.0f));
	fill_rrect(s, x + 1, y + 1, w - 2, h - 2, r - 1, &p);
	p = paint_vgrad(y + 1, y + h * 0.55f, 0xf0ffffff, 0x30ffffff);
	fill_rrect(s, x + 3, y + 2, w - 6, h * 0.45f, r * 0.6f, &p);
	draw_text_center(s, fonts[FONT_UI], 13, x, w, y + h / 2 + 5,
			 primary ? 0xff0b2246 : 0xff000000, label);
}

void aqua_checkbox(surface_t *s, int x, int y, const char *label, int checked)
{
	uint32_t base = checked ? 0xff3a8be6 : 0xfff4f4f4;
	paint_t p;

	p = paint_solid(checked ? 0xff1c4f9c : 0xff8a8a8a);
	fill_rrect(s, x, y, 15, 15, 3.5f, &p);
	p = paint_vgrad(y, y + 15, shade(base, 0.9f), lighten(base, 0.6f));
	fill_rrect(s, x + 1, y + 1, 13, 13, 2.5f, &p);
	p = paint_vgrad(y + 1, y + 8, 0xe0ffffff, 0x20ffffff);
	fill_rrect(s, x + 2, y + 1.5f, 11, 6, 2, &p);
	if (checked) {
		p = paint_solid(0xff10233f);
		draw_line(s, x + 3.5f, y + 7.5f, x + 6.5f, y + 11, 2, &p);
		draw_line(s, x + 6.5f, y + 11, x + 12.5f, y + 2.5f, 2, &p);
	}
	draw_text(s, fonts[FONT_UI], 13, x + 22, y + 12, 0xff000000, label);
}

void aqua_slider(surface_t *s, int x, int y, int w, float value)
{
	float tx;
	paint_t p;
	int i;

	p = paint_vgrad(y - 3, y + 3, 0xff8c8c8c, 0xffd6d6d6);
	fill_rrect(s, x, y - 3, w, 6, 3, &p);
	p = paint_solid(0xff707070);
	stroke_rrect(s, x, y - 3, w, 6, 3, 1, &p);
	p = paint_solid(0xff707070);
	for (i = 0; i <= 10; i++)
		fill_rect(s, x + 2 + i * (w - 4) / 10, y + 8, 1, 4, 0xff808080);
	if (value < 0)
		value = 0;
	if (value > 1)
		value = 1;
	tx = x + 2 + value * (w - 4);
	aqua_gel_circle(s, tx, y, 8, 0xff3a8be6);
}

void aqua_highlight(surface_t *s, int x, int y, int w, int h, float r)
{
	paint_t p = paint_vgrad(y, y + h, 0xff5d9cf0, 0xff1f5fd0);

	fill_rrect(s, x, y, w, h, r, &p);
}

surface_t *glass_text(font_t *f, float size, const char *text)
{
	int base, dx, dy;
	surface_t *m = text_mask(f, size, text, &base);
	int pad = (int)(size / 30) + 3;
	surface_t *out = surf_new(m->w + 2 * pad, m->h + 2 * pad);
	int ow = (int)(size / 60) + 1;
	paint_t p;

	/* drop shadow */
	{
		surface_t *sh = surf_new(out->w, out->h);

		p = paint_solid(0x90001a50);
		fill_mask(sh, pad, pad + ow + 1, m, &p);
		blur_surface(sh, ow + 1);
		blit(out, sh, 0, 0, 255);
		surf_free(sh);
	}
	/* outline */
	p = paint_solid(0xff0b3f95);
	for (dy = -ow; dy <= ow; dy++)
		for (dx = -ow; dx <= ow; dx++)
			if (dx * dx + dy * dy <= ow * ow + 1)
				fill_mask(out, pad + dx, pad + dy, m, &p);
	/* glass body: dark top, bright refracted bottom */
	p = paint_vgrad3(m->h * 0.18f, m->h * 0.98f, 0xff1d62c9, 0.55f, 0xff3d91ea,
			 0xffb7e6ff);
	fill_mask(out, pad, pad, m, &p);
	/* specular gloss on the upper half */
	p = paint_vgrad(m->h * 0.15f, m->h * 0.52f, 0xd8ffffff, 0x00ffffff);
	fill_mask(out, pad, pad, m, &p);
	surf_free(m);
	return out;
}
