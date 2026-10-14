// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - procedural icons.
 */
#include <math.h>

#include "wc.h"

#define PX(f) (x + (f) * S)
#define PY(f) (y + (f) * S)

uint32_t shade(uint32_t c, float f)
{
	int r = (int)(((c >> 16) & 255) * f), g = (int)(((c >> 8) & 255) * f);
	int b = (int)((c & 255) * f);

	if (r > 255)
		r = 255;
	if (g > 255)
		g = 255;
	if (b > 255)
		b = 255;
	return (c & 0xff000000u) | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

void icon_drop(surface_t *s, float x, float y, float S)
{
	float cx = PX(0.5f), cy = PY(0.63f), R = 0.31f * S, tipy = PY(0.05f);
	float D = cy - tipy, phi = acosf(R / D), pts[2 * 50];
	paint_t p;
	int i, n = 0;

	pts[n++] = cx;
	pts[n++] = tipy;
	for (i = 0; i < 48; i++) {
		float a = -(float)M_PI / 2 + phi +
			  (2 * (float)M_PI - 2 * phi) * i / 47.0f;

		pts[n++] = cx + R * cosf(a);
		pts[n++] = cy + R * sinf(a);
	}

	p = paint_solid(0x50001a40);
	{
		float sh[2 * 49];

		for (i = 0; i < 49; i++) {
			sh[2 * i] = pts[2 * i];
			sh[2 * i + 1] = pts[2 * i + 1] + 0.03f * S;
		}
		fill_poly(s, sh, 49, &p);
	}
	p = paint_vgrad3(tipy, cy + R, 0xff1450b8, 0.55f, 0xff2f86e6, 0xff9fdcff);
	fill_poly(s, pts, 49, &p);
	p = paint_solid(0xc00a3478);
	stroke_poly(s, pts, 49, 0.025f * S > 1 ? 0.025f * S : 1, &p);

	/* gloss */
	p = paint_vgrad(cy - 0.32f * S, cy + 0.02f * S, 0xf0ffffff, 0x10ffffff);
	fill_ellipse(s, cx - 0.05f * S, cy - 0.13f * S, 0.17f * S, 0.15f * S, &p);
	p = paint_vgrad(PY(0.18f), PY(0.40f), 0xa0ffffff, 0x00ffffff);
	fill_ellipse(s, cx - 0.02f * S, PY(0.30f), 0.05f * S, 0.10f * S, &p);
	/* bottom caustic */
	p = paint_vgrad(cy + 0.08f * S, cy + R, 0x00ffffff, 0x90e0f6ff);
	fill_ellipse(s, cx + 0.02f * S, cy + 0.19f * S, 0.20f * S, 0.10f * S, &p);
}

void icon_folder(surface_t *s, float x, float y, float S)
{
	float lw = S / 48 > 0.8f ? S / 48 : 0.8f;
	paint_t p;

	p = paint_vgrad(PY(0.14f), PY(0.5f), 0xff7ab8ee, 0xff4b8bd2);
	fill_rrect(s, PX(0.08f), PY(0.15f), 0.36f * S, 0.16f * S, 0.04f * S, &p);
	fill_rrect(s, PX(0.08f), PY(0.21f), 0.84f * S, 0.62f * S, 0.05f * S, &p);
	p = paint_solid(0xff2f6cb4);
	stroke_rrect(s, PX(0.08f), PY(0.21f), 0.84f * S, 0.62f * S, 0.05f * S, lw, &p);

	p = paint_vgrad(PY(0.32f), PY(0.86f), 0xffb4dcfc, 0xff5c9fe4);
	fill_rrect(s, PX(0.05f), PY(0.32f), 0.90f * S, 0.54f * S, 0.05f * S, &p);
	p = paint_solid(0xff2a64ae);
	stroke_rrect(s, PX(0.05f), PY(0.32f), 0.90f * S, 0.54f * S, 0.05f * S, lw, &p);
	p = paint_vgrad(PY(0.33f), PY(0.5f), 0xb0ffffff, 0x00ffffff);
	fill_rrect(s, PX(0.07f), PY(0.335f), 0.86f * S, 0.15f * S, 0.04f * S, &p);
}

void icon_document(surface_t *s, float x, float y, float S)
{
	float fold = 0.2f * S, lw = S / 48 > 0.8f ? S / 48 : 0.8f;
	float pts[] = { PX(0.2f), PY(0.08f), PX(0.8f) - fold, PY(0.08f), PX(0.8f),
			PY(0.08f) + fold, PX(0.8f), PY(0.92f), PX(0.2f), PY(0.92f) };
	float corner[] = { PX(0.8f) - fold, PY(0.08f), PX(0.8f) - fold,
			   PY(0.08f) + fold, PX(0.8f), PY(0.08f) + fold };
	paint_t p;
	int i;

	p = paint_vgrad(PY(0.08f), PY(0.92f), 0xffffffff, 0xffe9edf2);
	fill_poly(s, pts, 5, &p);
	p = paint_solid(0xff8a929c);
	stroke_poly(s, pts, 5, lw, &p);
	p = paint_solid(0xffd0d6de);
	fill_poly(s, corner, 3, &p);
	p = paint_solid(0xff8a929c);
	stroke_poly(s, corner, 3, lw, &p);
	p = paint_solid(0xffb4bcc6);
	for (i = 0; i < 6; i++)
		fill_rect(s, (int)PX(0.3f), (int)PY(0.38f + i * 0.08f),
			  (int)(0.4f * S * (i == 5 ? 0.6f : 1)), (int)(lw + 0.5f),
			  0xffb4bcc6);
}

void icon_terminal(surface_t *s, float x, float y, float S)
{
	float base[] = { PX(0.30f), PY(0.92f), PX(0.70f), PY(0.92f), PX(0.62f),
			 PY(0.80f), PX(0.38f), PY(0.80f) };
	float glare[] = { PX(0.16f), PY(0.18f), PX(0.62f), PY(0.18f), PX(0.16f),
			  PY(0.52f) };
	float lw = S / 48 > 0.8f ? S / 48 : 0.8f;
	paint_t p;

	p = paint_vgrad(PY(0.80f), PY(0.92f), 0xffd4d7dc, 0xff8d939b);
	fill_poly(s, base, 4, &p);
	p = paint_vgrad(PY(0.08f), PY(0.82f), 0xfff2f3f5, 0xffa6abb3);
	fill_rrect(s, PX(0.06f), PY(0.08f), 0.88f * S, 0.74f * S, 0.08f * S, &p);
	p = paint_solid(0xff5d636b);
	stroke_rrect(s, PX(0.06f), PY(0.08f), 0.88f * S, 0.74f * S, 0.08f * S, lw, &p);
	p = paint_vgrad(PY(0.16f), PY(0.72f), 0xff2a313b, 0xff07090c);
	fill_rrect(s, PX(0.13f), PY(0.15f), 0.74f * S, 0.58f * S, 0.04f * S, &p);
	draw_text(s, fonts[FONT_MONO], 0.26f * S, (int)PX(0.19f), (int)PY(0.42f),
		  0xff5df07a, ">_");
	p = paint_solid(0x16ffffff);
	fill_poly(s, glare, 3, &p);
}

void icon_clock(surface_t *s, float x, float y, float S, int hh, int mm, int ss)
{
	float cx = PX(0.5f), cy = PY(0.5f), i;
	paint_t p;
	float a;

	p = paint_solid(0x40000000);
	fill_circle(s, cx, cy + 0.02f * S, 0.45f * S, &p);
	p = paint_vgrad(cy - 0.45f * S, cy + 0.45f * S, 0xff5aa2ec, 0xff0c45a3);
	fill_circle(s, cx, cy, 0.44f * S, &p);
	p = paint_radial(cx, cy - 0.1f * S, 0.42f * S, 0xffffffff, 0xffdfe3e8);
	fill_circle(s, cx, cy, 0.36f * S, &p);

	p = paint_solid(0xff3a3f46);
	for (i = 0; i < 12; i++) {
		float an = i * (float)M_PI / 6;
		float r0 = (fmodf(i, 3) == 0 ? 0.26f : 0.29f) * S, r1 = 0.33f * S;

		draw_line(s, cx + r0 * sinf(an), cy - r0 * cosf(an), cx + r1 * sinf(an),
			  cy - r1 * cosf(an), fmodf(i, 3) == 0 ? 0.035f * S : 0.02f * S,
			  &p);
	}
	a = ((hh % 12) + mm / 60.0f) * (float)M_PI / 6;
	draw_line(s, cx, cy, cx + 0.18f * S * sinf(a), cy - 0.18f * S * cosf(a),
		  0.05f * S, &p);
	a = (mm + ss / 60.0f) * (float)M_PI / 30;
	draw_line(s, cx, cy, cx + 0.28f * S * sinf(a), cy - 0.28f * S * cosf(a),
		  0.035f * S, &p);
	if (ss >= 0) {
		p = paint_solid(0xffd8302a);
		a = ss * (float)M_PI / 30;
		draw_line(s, cx - 0.06f * S * sinf(a), cy + 0.06f * S * cosf(a),
			  cx + 0.31f * S * sinf(a), cy - 0.31f * S * cosf(a),
			  0.014f * S + 0.5f, &p);
	}
	p = paint_solid(0xff2a2e33);
	fill_circle(s, cx, cy, 0.03f * S, &p);

	p = paint_vgrad(cy - 0.44f * S, cy - 0.05f * S, 0xb0ffffff, 0x00ffffff);
	fill_ellipse(s, cx, cy - 0.22f * S, 0.34f * S, 0.20f * S, &p);
}

void icon_gear(surface_t *s, float x, float y, float S)
{
	float cx = PX(0.5f), cy = PY(0.5f), pts[2 * 48];
	int teeth = 12, i, n = 0;
	paint_t p;

	for (i = 0; i < teeth; i++) {
		float a0 = i * 2 * (float)M_PI / teeth, step = 2 * (float)M_PI / teeth;
		float ro = 0.44f * S, ri = 0.35f * S;
		float as[4] = { a0, a0 + step * 0.18f, a0 + step * 0.45f, a0 + step * 0.63f };
		float rs[4] = { ri, ro, ro, ri };
		int k;

		for (k = 0; k < 4; k++) {
			pts[n++] = cx + rs[k] * cosf(as[k]);
			pts[n++] = cy + rs[k] * sinf(as[k]);
		}
	}
	p = paint_solid(0x50000000);
	fill_circle(s, cx, cy + 0.03f * S, 0.42f * S, &p);
	p = paint_vgrad(cy - 0.44f * S, cy + 0.44f * S, 0xffe9ecf0, 0xff737c88);
	fill_poly(s, pts, teeth * 4, &p);
	p = paint_solid(0xff4b525c);
	stroke_poly(s, pts, teeth * 4, S / 64 + 0.6f, &p);

	p = paint_vgrad(cy - 0.25f * S, cy + 0.25f * S, 0xff8b939e, 0xffdfe3e8);
	fill_circle(s, cx, cy, 0.24f * S, &p);
	aqua_gel_circle(s, cx, cy, 0.15f * S, 0xff2f7de0);
}

void icon_trash(surface_t *s, float x, float y, float S)
{
	float body[] = { PX(0.18f), PY(0.24f), PX(0.82f), PY(0.24f), PX(0.74f),
			 PY(0.90f), PX(0.26f), PY(0.90f) };
	float lw = S / 64 + 0.6f;
	paint_t p;
	int i;

	p = paint_solid(0x40000000);
	fill_ellipse(s, PX(0.5f), PY(0.91f), 0.27f * S, 0.04f * S, &p);
	p = paint_vgrad(PY(0.24f), PY(0.9f), 0x70e4e8ec, 0xb08c939c);
	fill_poly(s, body, 4, &p);
	p = paint_solid(0x406a717a);
	for (i = 1; i < 6; i++) {
		float t = i / 6.0f;

		draw_line(s, PX(0.18f) + t * 0.08f * S, PY(0.24f) + t * 0.66f * S,
			  PX(0.82f) - t * 0.08f * S, PY(0.24f) + t * 0.66f * S, lw, &p);
	}
	p = paint_solid(0xc05a616a);
	for (i = 0; i <= 10; i++) {
		float t = i / 10.0f;

		draw_line(s, PX(0.18f + 0.64f * t), PY(0.26f),
			  PX(0.26f + 0.48f * t), PY(0.90f), lw, &p);
	}
	p = paint_solid(0xff5a616a);
	stroke_poly(s, body, 4, lw, &p);
	p = paint_solid(0xc0384048);
	fill_ellipse(s, PX(0.5f), PY(0.24f), 0.30f * S, 0.055f * S, &p);
	p = paint_vgrad(PY(0.17f), PY(0.31f), 0xfff4f6f8, 0xff8a919a);
	{
		struct { float cx, cy, rx, ry; } r = { PX(0.5f), PY(0.24f), 0.33f * S,
						       0.075f * S };
		/* rim: outer ellipse minus inner */
		fill_ellipse(s, r.cx, r.cy, r.rx, r.ry, &p);
		p = paint_vgrad(PY(0.2f), PY(0.28f), 0xff3a4149, 0xff6b737d);
		fill_ellipse(s, r.cx, r.cy + 0.005f * S, r.rx - 0.045f * S,
			     r.ry - 0.03f * S, &p);
	}
}

void icon_disk(surface_t *s, float x, float y, float S)
{
	float top[] = { PX(0.14f), PY(0.30f), PX(0.86f), PY(0.30f), PX(0.94f),
			PY(0.44f), PX(0.06f), PY(0.44f) };
	float lw = S / 64 + 0.6f;
	paint_t p;
	int i;

	p = paint_solid(0x50000000);
	fill_rrect(s, PX(0.07f), PY(0.47f), 0.88f * S, 0.30f * S, 0.05f * S, &p);
	p = paint_vgrad(PY(0.30f), PY(0.44f), 0xfffbfcfd, 0xffd5d9de);
	fill_poly(s, top, 4, &p);
	p = paint_solid(0xff6c727a);
	stroke_poly(s, top, 4, lw, &p);
	p = paint_vgrad(PY(0.44f), PY(0.74f), 0xffc9ced5, 0xff7b828c);
	fill_rrect(s, PX(0.06f), PY(0.44f), 0.88f * S, 0.30f * S, 0.04f * S, &p);
	p = paint_solid(0xff5d636b);
	stroke_rrect(s, PX(0.06f), PY(0.44f), 0.88f * S, 0.30f * S, 0.04f * S, lw, &p);
	p = paint_solid(0x90ffffff);
	fill_rect(s, (int)PX(0.08f), (int)PY(0.455f), (int)(0.84f * S), (int)(lw + 0.5f),
		  0x90ffffff);
	for (i = 0; i < 6; i++)
		fill_rect(s, (int)PX(0.14f + i * 0.05f), (int)PY(0.53f), (int)(lw * 1.6f + 0.5f),
			  (int)(0.14f * S), 0x705a6069);
	aqua_gel_circle(s, PX(0.80f), PY(0.59f), 0.045f * S, 0xff36c24a);
}

void icon_app(surface_t *s, float x, float y, float S)
{
	paint_t p = paint_vgrad(PY(0.1f), PY(0.9f), 0xffa8b2c0, 0xff5b6677);
	float pts[] = { PX(0.5f), PY(0.08f), PX(0.92f), PY(0.5f), PX(0.5f),
			PY(0.92f), PX(0.08f), PY(0.5f) };

	fill_poly(s, pts, 4, &p);
	p = paint_solid(0xff3f4855);
	stroke_poly(s, pts, 4, S / 64 + 0.6f, &p);
	aqua_gel_circle(s, PX(0.5f), PY(0.5f), 0.18f * S, 0xff2f7de0);
}
