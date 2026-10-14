// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - software rendering core.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

static const surface_t *clip_surf;
static int clip_x0, clip_y0, clip_x1, clip_y1;

void gfx_clip(const surface_t *s, int x, int y, int w, int h)
{
	clip_surf = s;
	clip_x0 = x;
	clip_y0 = y;
	clip_x1 = x + w;
	clip_y1 = y + h;
}

void gfx_noclip(void)
{
	clip_surf = NULL;
}

/* Clamp [x0,x1) x [y0,y1) to the surface and the clip rectangle. */
static int clamp_box(const surface_t *s, int *x0, int *y0, int *x1, int *y1)
{
	if (*x0 < 0)
		*x0 = 0;
	if (*y0 < 0)
		*y0 = 0;
	if (*x1 > s->w)
		*x1 = s->w;
	if (*y1 > s->h)
		*y1 = s->h;
	if (clip_surf == s) {
		if (*x0 < clip_x0)
			*x0 = clip_x0;
		if (*y0 < clip_y0)
			*y0 = clip_y0;
		if (*x1 > clip_x1)
			*x1 = clip_x1;
		if (*y1 > clip_y1)
			*y1 = clip_y1;
	}
	return *x0 < *x1 && *y0 < *y1;
}

surface_t *surf_new(int w, int h)
{
	surface_t *s = malloc(sizeof(*s));

	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	s->w = w;
	s->h = h;
	s->px = calloc((size_t)w * h, 4);
	return s;
}

void surf_free(surface_t *s)
{
	if (!s)
		return;
	free(s->px);
	free(s);
}

static inline uint32_t premul(uint32_t c)
{
	uint32_t a = c >> 24;

	if (a == 255)
		return c;
	return a << 24 | (((c >> 16) & 255) * a / 255) << 16 |
	       (((c >> 8) & 255) * a / 255) << 8 | ((c & 255) * a / 255);
}

void surf_clear(surface_t *s, uint32_t color)
{
	uint32_t p = premul(color);
	size_t i, n = (size_t)s->w * s->h;

	for (i = 0; i < n; i++)
		s->px[i] = p;
}

uint32_t color_lerp(uint32_t a, uint32_t b, float t)
{
	int i;
	uint32_t out = 0;

	if (t <= 0)
		return a;
	if (t >= 1)
		return b;
	for (i = 0; i < 32; i += 8) {
		float ca = (a >> i) & 255, cb = (b >> i) & 255;

		out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << i;
	}
	return out;
}

/* Blend straight color c with coverage cov (0..256) over premultiplied *d. */
static inline void put(uint32_t *d, uint32_t c, int cov)
{
	uint32_t a = ((c >> 24) * cov) >> 8, ia, dv;
	uint32_t r, g, b;

	if (!a)
		return;
	r = ((c >> 16) & 255) * a / 255;
	g = ((c >> 8) & 255) * a / 255;
	b = (c & 255) * a / 255;
	if (a >= 255) {
		*d = 0xff000000u | r << 16 | g << 8 | b;
		return;
	}
	ia = 255 - a;
	dv = *d;
	*d = (((dv >> 24) * ia / 255 + a) << 24) |
	     ((((dv >> 16) & 255) * ia / 255 + r) << 16) |
	     ((((dv >> 8) & 255) * ia / 255 + g) << 8) |
	     ((dv & 255) * ia / 255 + b);
}

/* Premultiplied source over premultiplied destination, scaled by op/255. */
static inline void over(uint32_t *d, uint32_t s, uint32_t op)
{
	uint32_t sa, ia, dv;

	if (op < 255) {
		s = (((s >> 24) * op / 255) << 24) |
		    ((((s >> 16) & 255) * op / 255) << 16) |
		    ((((s >> 8) & 255) * op / 255) << 8) |
		    ((s & 255) * op / 255);
	}
	sa = s >> 24;
	if (!sa)
		return;
	if (sa == 255) {
		*d = s;
		return;
	}
	ia = 255 - sa;
	dv = *d;
	*d = (((dv >> 24) * ia / 255 + sa) << 24) |
	     ((((dv >> 16) & 255) * ia / 255 + ((s >> 16) & 255)) << 16) |
	     ((((dv >> 8) & 255) * ia / 255 + ((s >> 8) & 255)) << 8) |
	     ((dv & 255) * ia / 255 + (s & 255));
}

paint_t paint_solid(uint32_t c)
{
	paint_t p = { .type = PAINT_SOLID, .nstops = 1 };

	p.col[0] = c;
	return p;
}

paint_t paint_vgrad(float y0, float y1, uint32_t top, uint32_t bottom)
{
	paint_t p = { .type = PAINT_VGRAD, .nstops = 2, .y0 = y0, .y1 = y1 };

	p.pos[0] = 0;
	p.col[0] = top;
	p.pos[1] = 1;
	p.col[1] = bottom;
	return p;
}

paint_t paint_vgrad3(float y0, float y1, uint32_t a, float pb, uint32_t b,
		     uint32_t c)
{
	paint_t p = paint_vgrad(y0, y1, a, c);

	p.nstops = 3;
	p.pos[1] = pb;
	p.col[1] = b;
	p.pos[2] = 1;
	p.col[2] = c;
	return p;
}

paint_t paint_radial(float cx, float cy, float r, uint32_t in, uint32_t out)
{
	paint_t p = { .type = PAINT_RADIAL, .nstops = 2, .x0 = cx, .y0 = cy,
		      .r = r };

	p.pos[0] = 0;
	p.col[0] = in;
	p.pos[1] = 1;
	p.col[1] = out;
	return p;
}

paint_t paint_stripes(uint32_t a, uint32_t b, int period)
{
	paint_t p = { .type = PAINT_STRIPES, .nstops = 2, .period = period };

	p.col[0] = a;
	p.col[1] = b;
	return p;
}

static uint32_t stops_at(const paint_t *p, float t)
{
	int i;

	if (t <= p->pos[0])
		return p->col[0];
	for (i = 1; i < p->nstops; i++) {
		if (t <= p->pos[i]) {
			float span = p->pos[i] - p->pos[i - 1];

			return color_lerp(p->col[i - 1], p->col[i],
					  span > 0 ? (t - p->pos[i - 1]) / span : 1);
		}
	}
	return p->col[p->nstops - 1];
}

static inline uint32_t paint_at(const paint_t *p, float x, float y)
{
	switch (p->type) {
	case PAINT_SOLID:
		return p->col[0];
	case PAINT_VGRAD:
		return stops_at(p, (y - p->y0) / (p->y1 - p->y0));
	case PAINT_RADIAL:
		return stops_at(p, hypotf(x - p->x0, y - p->y0) / p->r);
	case PAINT_STRIPES:
		return p->col[((int)y / (p->period / 2 ? p->period / 2 : 1)) & 1];
	}
	return 0;
}

void fill_sdf(surface_t *s, int x0, int y0, int x1, int y1, sdf_fn fn,
	      const void *ctx, const paint_t *p)
{
	int x, y;

	if (!clamp_box(s, &x0, &y0, &x1, &y1))
		return;
	for (y = y0; y < y1; y++) {
		uint32_t *row = s->px + (size_t)y * s->w;
		uint32_t rowc = 0;
		int row_const = p->type == PAINT_VGRAD || p->type == PAINT_STRIPES ||
				p->type == PAINT_SOLID;

		if (row_const)
			rowc = paint_at(p, 0, y + 0.5f);
		for (x = x0; x < x1; x++) {
			float d = fn(x + 0.5f, y + 0.5f, ctx);
			float cov = 0.5f - d;

			if (cov <= 0)
				continue;
			if (cov > 1)
				cov = 1;
			put(&row[x], row_const ? rowc : paint_at(p, x + 0.5f, y + 0.5f),
			    (int)(cov * 256));
		}
	}
}

void fill_rect_paint(surface_t *s, int x, int y, int w, int h,
		     const paint_t *p)
{
	int x0 = x, y0 = y, x1 = x + w, y1 = y + h, i, j;

	if (!clamp_box(s, &x0, &y0, &x1, &y1))
		return;
	for (j = y0; j < y1; j++) {
		uint32_t *row = s->px + (size_t)j * s->w;

		if (p->type == PAINT_RADIAL) {
			for (i = x0; i < x1; i++)
				put(&row[i], paint_at(p, i + 0.5f, j + 0.5f), 256);
		} else {
			uint32_t c = paint_at(p, 0, j + 0.5f);

			for (i = x0; i < x1; i++)
				put(&row[i], c, 256);
		}
	}
}

void fill_rect(surface_t *s, int x, int y, int w, int h, uint32_t c)
{
	paint_t p = paint_solid(c);

	fill_rect_paint(s, x, y, w, h, &p);
}

struct rr { float cx, cy, hw, hh, r, sw; };

static float sdf_rrect(float x, float y, const void *ctx)
{
	const struct rr *c = ctx;
	float qx = fabsf(x - c->cx) - c->hw + c->r;
	float qy = fabsf(y - c->cy) - c->hh + c->r;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float in = qx > qy ? qx : qy;

	return hypotf(ox, oy) + (in < 0 ? in : 0) - c->r;
}

static float sdf_rrect_ring(float x, float y, const void *ctx)
{
	const struct rr *c = ctx;

	return fabsf(sdf_rrect(x, y, ctx) + c->sw / 2) - c->sw / 2;
}

void fill_rrect(surface_t *s, float x, float y, float w, float h, float r,
		const paint_t *p)
{
	struct rr c = { x + w / 2, y + h / 2, w / 2, h / 2, r, 0 };

	if (r > w / 2)
		c.r = w / 2;
	if (c.r > h / 2)
		c.r = h / 2;
	fill_sdf(s, (int)floorf(x), (int)floorf(y), (int)ceilf(x + w),
		 (int)ceilf(y + h), sdf_rrect, &c, p);
}

void stroke_rrect(surface_t *s, float x, float y, float w, float h, float r,
		  float width, const paint_t *p)
{
	struct rr c = { x + w / 2, y + h / 2, w / 2, h / 2, r, width };

	fill_sdf(s, (int)floorf(x) - 1, (int)floorf(y) - 1, (int)ceilf(x + w) + 1,
		 (int)ceilf(y + h) + 1, sdf_rrect_ring, &c, p);
}

struct circ { float cx, cy, r, w; };

static float sdf_circle(float x, float y, const void *ctx)
{
	const struct circ *c = ctx;

	return hypotf(x - c->cx, y - c->cy) - c->r;
}

static float sdf_ring(float x, float y, const void *ctx)
{
	const struct circ *c = ctx;

	return fabsf(hypotf(x - c->cx, y - c->cy) - c->r) - c->w / 2;
}

void fill_circle(surface_t *s, float cx, float cy, float r, const paint_t *p)
{
	struct circ c = { cx, cy, r, 0 };

	fill_sdf(s, (int)(cx - r - 1), (int)(cy - r - 1), (int)(cx + r + 2),
		 (int)(cy + r + 2), sdf_circle, &c, p);
}

void stroke_circle(surface_t *s, float cx, float cy, float r, float width,
		   const paint_t *p)
{
	struct circ c = { cx, cy, r, width };
	float e = r + width;

	fill_sdf(s, (int)(cx - e - 1), (int)(cy - e - 1), (int)(cx + e + 2),
		 (int)(cy + e + 2), sdf_ring, &c, p);
}

struct ell { float cx, cy, rx, ry; };

static float sdf_ellipse(float x, float y, const void *ctx)
{
	const struct ell *c = ctx;
	float k = hypotf((x - c->cx) / c->rx, (y - c->cy) / c->ry);

	return (k - 1) * (c->rx < c->ry ? c->rx : c->ry);
}

void fill_ellipse(surface_t *s, float cx, float cy, float rx, float ry,
		  const paint_t *p)
{
	struct ell c = { cx, cy, rx, ry };

	fill_sdf(s, (int)(cx - rx - 1), (int)(cy - ry - 1), (int)(cx + rx + 2),
		 (int)(cy + ry + 2), sdf_ellipse, &c, p);
}

struct seg { float ax, ay, bx, by, r; };

static float sdf_segment(float x, float y, const void *ctx)
{
	const struct seg *c = ctx;
	float px = x - c->ax, py = y - c->ay;
	float dx = c->bx - c->ax, dy = c->by - c->ay;
	float l = dx * dx + dy * dy;
	float t = l > 0 ? (px * dx + py * dy) / l : 0;

	if (t < 0)
		t = 0;
	if (t > 1)
		t = 1;
	return hypotf(px - dx * t, py - dy * t) - c->r;
}

void draw_line(surface_t *s, float x0, float y0, float x1, float y1,
	       float width, const paint_t *p)
{
	struct seg c = { x0, y0, x1, y1, width / 2 };
	float e = width / 2 + 2;

	fill_sdf(s, (int)(fminf(x0, x1) - e), (int)(fminf(y0, y1) - e),
		 (int)(fmaxf(x0, x1) + e), (int)(fmaxf(y0, y1) + e), sdf_segment,
		 &c, p);
}

struct poly { const float *xy; int n; float w; };

static float sdf_poly(float x, float y, const void *ctx)
{
	const struct poly *c = ctx;
	const float *v = c->xy;
	float d = 1e30f;
	int i, j, inside = 0;

	for (i = 0, j = c->n - 1; i < c->n; j = i++) {
		float ex = v[2 * j] - v[2 * i], ey = v[2 * j + 1] - v[2 * i + 1];
		float wx = x - v[2 * i], wy = y - v[2 * i + 1];
		float l = ex * ex + ey * ey;
		float t = l > 0 ? (wx * ex + wy * ey) / l : 0;
		float bx, by, dd;

		if (t < 0)
			t = 0;
		if (t > 1)
			t = 1;
		bx = wx - ex * t;
		by = wy - ey * t;
		dd = bx * bx + by * by;
		if (dd < d)
			d = dd;
		if ((v[2 * i + 1] > y) != (v[2 * j + 1] > y) &&
		    x < (v[2 * j] - v[2 * i]) * (y - v[2 * i + 1]) /
				    (v[2 * j + 1] - v[2 * i + 1]) + v[2 * i])
			inside = !inside;
	}
	d = sqrtf(d);
	return inside ? -d : d;
}

static float sdf_poly_ring(float x, float y, const void *ctx)
{
	const struct poly *c = ctx;

	return fabsf(sdf_poly(x, y, ctx)) - c->w / 2;
}

static void poly_bounds(const float *xy, int n, float e, int *b)
{
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	int i;

	for (i = 0; i < n; i++) {
		x0 = fminf(x0, xy[2 * i]);
		x1 = fmaxf(x1, xy[2 * i]);
		y0 = fminf(y0, xy[2 * i + 1]);
		y1 = fmaxf(y1, xy[2 * i + 1]);
	}
	b[0] = (int)(x0 - e);
	b[1] = (int)(y0 - e);
	b[2] = (int)(x1 + e + 1);
	b[3] = (int)(y1 + e + 1);
}

void fill_poly(surface_t *s, const float *xy, int n, const paint_t *p)
{
	struct poly c = { xy, n, 0 };
	int b[4];

	poly_bounds(xy, n, 2, b);
	fill_sdf(s, b[0], b[1], b[2], b[3], sdf_poly, &c, p);
}

void stroke_poly(surface_t *s, const float *xy, int n, float width,
		 const paint_t *p)
{
	struct poly c = { xy, n, width };
	int b[4];

	poly_bounds(xy, n, width + 2, b);
	fill_sdf(s, b[0], b[1], b[2], b[3], sdf_poly_ring, &c, p);
}

void blend_mask(surface_t *dst, int x, int y, const uint8_t *mask, int mw,
		int mh, uint32_t color)
{
	int x0 = x, y0 = y, x1 = x + mw, y1 = y + mh, i, j;

	if (!clamp_box(dst, &x0, &y0, &x1, &y1))
		return;
	for (j = y0; j < y1; j++) {
		uint32_t *row = dst->px + (size_t)j * dst->w;
		const uint8_t *m = mask + (size_t)(j - y) * mw - x;

		for (i = x0; i < x1; i++)
			if (m[i])
				put(&row[i], color, m[i] + (m[i] >> 7));
	}
}

void fill_mask(surface_t *dst, int x, int y, const surface_t *mask,
	       const paint_t *p)
{
	int x0 = x, y0 = y, x1 = x + mask->w, y1 = y + mask->h, i, j;

	if (!clamp_box(dst, &x0, &y0, &x1, &y1))
		return;
	for (j = y0; j < y1; j++) {
		uint32_t *row = dst->px + (size_t)j * dst->w;
		const uint32_t *m = mask->px + (size_t)(j - y) * mask->w - x;

		for (i = x0; i < x1; i++) {
			uint32_t a = m[i] >> 24;

			if (a)
				put(&row[i], paint_at(p, i - x + 0.5f, j - y + 0.5f),
				    a + (a >> 7));
		}
	}
}

void blit_part(surface_t *dst, const surface_t *src, int dx, int dy, int sx,
	       int sy, int w, int h, int opacity)
{
	int x0 = dx, y0 = dy, x1 = dx + w, y1 = dy + h, x, y;

	if (opacity <= 0)
		return;
	if (opacity > 255)
		opacity = 255;
	if (!clamp_box(dst, &x0, &y0, &x1, &y1))
		return;
	for (y = y0; y < y1; y++) {
		int syy = sy + y - dy;
		uint32_t *d, *srow;

		if (syy < 0 || syy >= src->h)
			continue;
		d = dst->px + (size_t)y * dst->w;
		srow = src->px + (size_t)syy * src->w;
		for (x = x0; x < x1; x++) {
			int sxx = sx + x - dx;

			if (sxx >= 0 && sxx < src->w)
				over(&d[x], srow[sxx], opacity);
		}
	}
}

void blit(surface_t *dst, const surface_t *src, int dx, int dy, int opacity)
{
	blit_part(dst, src, dx, dy, 0, 0, src->w, src->h, opacity);
}

void blit_opaque(surface_t *dst, const surface_t *src, int dx, int dy)
{
	int x0 = dx, y0 = dy, x1 = dx + src->w, y1 = dy + src->h, y;

	if (!clamp_box(dst, &x0, &y0, &x1, &y1))
		return;
	for (y = y0; y < y1; y++)
		memcpy(dst->px + (size_t)y * dst->w + x0,
		       src->px + (size_t)(y - dy) * src->w + (x0 - dx),
		       (size_t)(x1 - x0) * 4);
}

static inline uint32_t texel(const surface_t *s, int x, int y)
{
	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
		return 0;
	return s->px[(size_t)y * s->w + x];
}

void blit_scaled(surface_t *dst, const surface_t *src, float dx, float dy,
		 float dw, float dh, int opacity)
{
	int x0 = (int)floorf(dx), y0 = (int)floorf(dy);
	int x1 = (int)ceilf(dx + dw), y1 = (int)ceilf(dy + dh), x, y;
	float sx = src->w / dw, sy = src->h / dh;

	if (dw < 1 || dh < 1 || opacity <= 0)
		return;
	if (!clamp_box(dst, &x0, &y0, &x1, &y1))
		return;
	for (y = y0; y < y1; y++) {
		float v = (y + 0.5f - dy) * sy - 0.5f;
		int iv = (int)floorf(v);
		uint32_t fv = (uint32_t)((v - iv) * 256);
		uint32_t *d = dst->px + (size_t)y * dst->w;

		for (x = x0; x < x1; x++) {
			float u = (x + 0.5f - dx) * sx - 0.5f;
			int iu = (int)floorf(u);
			uint32_t fu = (uint32_t)((u - iu) * 256);
			uint32_t a = texel(src, iu, iv), b = texel(src, iu + 1, iv);
			uint32_t c = texel(src, iu, iv + 1), e = texel(src, iu + 1, iv + 1);
			uint32_t out = 0;
			int sh;

			for (sh = 0; sh < 32; sh += 8) {
				uint32_t ca = (a >> sh) & 255, cb = (b >> sh) & 255;
				uint32_t cc = (c >> sh) & 255, ce = (e >> sh) & 255;
				uint32_t top = ca * (256 - fu) + cb * fu;
				uint32_t bot = cc * (256 - fu) + ce * fu;

				out |= ((top * (256 - fv) + bot * fv) >> 16) << sh;
			}
			over(&d[x], out, opacity);
		}
	}
}

static void box_blur_pass(uint32_t *buf, uint32_t *tmp, int w, int h,
			  int r, int horiz)
{
	int n = horiz ? w : h, lines = horiz ? h : w;
	int step = horiz ? 1 : w, lstep = horiz ? w : 1;
	int l, i, c;

	for (l = 0; l < lines; l++) {
		uint32_t *line = buf + (size_t)l * lstep;
		uint32_t sum[4] = { 0 };
		int div = 2 * r + 1;

		for (i = -r; i <= r; i++) {
			uint32_t v = (i >= 0 && i < n) ? line[(size_t)i * step] : 0;

			for (c = 0; c < 4; c++)
				sum[c] += (v >> (c * 8)) & 255;
		}
		for (i = 0; i < n; i++) {
			uint32_t add, sub;

			tmp[i] = (sum[0] / div) | (sum[1] / div) << 8 |
				 (sum[2] / div) << 16 | (sum[3] / div) << 24;
			add = (i + r + 1 < n) ? line[(size_t)(i + r + 1) * step] : 0;
			sub = (i - r >= 0) ? line[(size_t)(i - r) * step] : 0;
			for (c = 0; c < 4; c++)
				sum[c] += ((add >> (c * 8)) & 255) -
					  ((sub >> (c * 8)) & 255);
		}
		for (i = 0; i < n; i++)
			line[(size_t)i * step] = tmp[i];
	}
}

void blur_surface(surface_t *s, int radius)
{
	uint32_t *tmp;
	int pass;

	if (radius < 1)
		return;
	tmp = malloc((size_t)(s->w > s->h ? s->w : s->h) * 4);
	for (pass = 0; pass < 3; pass++) {
		box_blur_pass(s->px, tmp, s->w, s->h, radius, 1);
		box_blur_pass(s->px, tmp, s->w, s->h, radius, 0);
	}
	free(tmp);
}

surface_t *make_shadow(int w, int h, float r, int pad, int blur, uint32_t c)
{
	surface_t *s = surf_new(w + 2 * pad, h + 2 * pad);
	paint_t p = paint_solid(c);

	fill_rrect(s, pad, pad, w, h, r, &p);
	blur_surface(s, blur / 3 > 0 ? blur / 3 : 1);
	return s;
}
