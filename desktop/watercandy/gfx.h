/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Water Candy - software rendering core.
 *
 * All surfaces hold premultiplied ARGB8888 pixels. Colors passed to the
 * API are straight (non-premultiplied) 0xAARRGGBB values.
 */
#ifndef WC_GFX_H
#define WC_GFX_H

#include <stdint.h>

typedef struct surface {
	int w, h;
	uint32_t *px;
} surface_t;

#define RGBA(r, g, b, a) \
	((uint32_t)(a) << 24 | (uint32_t)(r) << 16 | (uint32_t)(g) << 8 | (uint32_t)(b))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define HEX(v) (0xff000000u | (uint32_t)(v))
#define ALPHA(c, a) (((c) & 0x00ffffffu) | (uint32_t)(a) << 24)

surface_t *surf_new(int w, int h);
void surf_free(surface_t *s);
void surf_clear(surface_t *s, uint32_t color);

uint32_t color_lerp(uint32_t a, uint32_t b, float t);

/* Paint: what fills a shape. */
enum paint_type { PAINT_SOLID, PAINT_VGRAD, PAINT_RADIAL, PAINT_STRIPES };

typedef struct paint {
	enum paint_type type;
	int nstops;
	float pos[5];
	uint32_t col[5];
	float x0, y0, x1, y1;	/* gradient geometry */
	float r;		/* radial radius */
	int period;		/* stripes */
} paint_t;

paint_t paint_solid(uint32_t c);
paint_t paint_vgrad(float y0, float y1, uint32_t top, uint32_t bottom);
paint_t paint_vgrad3(float y0, float y1, uint32_t a, float pb, uint32_t b,
		     uint32_t c);
paint_t paint_radial(float cx, float cy, float r, uint32_t in, uint32_t out);
paint_t paint_stripes(uint32_t a, uint32_t b, int period);

/* Signed distance shapes, antialiased by distance. */
typedef float (*sdf_fn)(float x, float y, const void *ctx);

void fill_sdf(surface_t *s, int x0, int y0, int x1, int y1, sdf_fn fn,
	      const void *ctx, const paint_t *p);

void fill_rect(surface_t *s, int x, int y, int w, int h, uint32_t c);
void fill_rect_paint(surface_t *s, int x, int y, int w, int h,
		     const paint_t *p);
void fill_rrect(surface_t *s, float x, float y, float w, float h, float r,
		const paint_t *p);
void stroke_rrect(surface_t *s, float x, float y, float w, float h, float r,
		  float width, const paint_t *p);
void fill_circle(surface_t *s, float cx, float cy, float r, const paint_t *p);
void stroke_circle(surface_t *s, float cx, float cy, float r, float width,
		   const paint_t *p);
void fill_ellipse(surface_t *s, float cx, float cy, float rx, float ry,
		  const paint_t *p);
void draw_line(surface_t *s, float x0, float y0, float x1, float y1,
	       float width, const paint_t *p);
void fill_poly(surface_t *s, const float *xy, int n, const paint_t *p);
void stroke_poly(surface_t *s, const float *xy, int n, float width,
		 const paint_t *p);

/* Blend an 8-bit coverage mask tinted with color. */
void blend_mask(surface_t *dst, int x, int y, const uint8_t *mask, int mw,
		int mh, uint32_t color);

/* Paint through the alpha of mask; paint coordinates are mask-local. */
void fill_mask(surface_t *dst, int x, int y, const surface_t *mask,
	       const paint_t *p);

/* Compositing. */
void blit(surface_t *dst, const surface_t *src, int dx, int dy, int opacity);
void blit_part(surface_t *dst, const surface_t *src, int dx, int dy, int sx,
	       int sy, int w, int h, int opacity);
void blit_scaled(surface_t *dst, const surface_t *src, float dx, float dy,
		 float dw, float dh, int opacity);
void blit_opaque(surface_t *dst, const surface_t *src, int dx, int dy);

/* Soft drop shadow of a rounded rectangle; result is (w+2*pad, h+2*pad). */
surface_t *make_shadow(int w, int h, float r, int pad, int blur, uint32_t c);

/* Box blur of a whole surface (premultiplied), radius in pixels. */
void blur_surface(surface_t *s, int radius);

/* Clip rectangle applied to drawing calls that target surface s. */
void gfx_clip(const surface_t *s, int x, int y, int w, int h);
void gfx_noclip(void);

#endif
