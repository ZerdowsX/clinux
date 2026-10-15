// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - TrueType text rendering with a glyph cache.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#include "font.h"

struct font {
	stbtt_fontinfo info;
	unsigned char *data;
};

struct glyph {
	const font_t *font;
	unsigned cp;
	int size10;		/* size * 10, the cache key */
	int w, h, xoff, yoff;
	float advance;
	unsigned char *bm;
};

#define CACHE_SIZE 8192
static struct glyph cache[CACHE_SIZE];

font_t *fonts[FONT_COUNT];

static const char *const ui_paths[] = {
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	"/usr/share/fonts/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
	"/usr/share/fonts/truetype/freefont/FreeSans.ttf",
	NULL
};

static const char *const bold_paths[] = {
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
	"/usr/share/fonts/truetype/freefont/FreeSansBold.ttf",
	NULL
};

static const char *const serif_paths[] = {
	"/usr/share/fonts/truetype/freefont/FreeSerif.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
	"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
	"/usr/share/fonts/TTF/DejaVuSerif.ttf",
	NULL
};

static const char *const mono_paths[] = {
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
	"/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
	"/usr/share/fonts/truetype/freefont/FreeMono.ttf",
	NULL
};

font_t *font_load(const char *const *paths)
{
	for (; *paths; paths++) {
		FILE *f = fopen(*paths, "rb");
		font_t *font;
		long n;

		if (!f)
			continue;
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		font = calloc(1, sizeof(*font));
		font->data = malloc(n);
		if (fread(font->data, 1, n, f) != (size_t)n ||
		    !stbtt_InitFont(&font->info, font->data,
				    stbtt_GetFontOffsetForIndex(font->data, 0))) {
			free(font->data);
			free(font);
			fclose(f);
			continue;
		}
		fclose(f);
		return font;
	}
	return NULL;
}

int fonts_init(void)
{
	fonts[FONT_UI] = font_load(ui_paths);
	fonts[FONT_UI_BOLD] = font_load(bold_paths);
	fonts[FONT_SERIF] = font_load(serif_paths);
	fonts[FONT_MONO] = font_load(mono_paths);
	if (!fonts[FONT_UI]) {
		fprintf(stderr, "watercandy: no usable font found\n");
		return -1;
	}
	if (!fonts[FONT_UI_BOLD])
		fonts[FONT_UI_BOLD] = fonts[FONT_UI];
	if (!fonts[FONT_SERIF])
		fonts[FONT_SERIF] = fonts[FONT_UI];
	if (!fonts[FONT_MONO])
		fonts[FONT_MONO] = fonts[FONT_UI];
	return 0;
}

unsigned utf8_next(const char **sp)
{
	const unsigned char *s = (const unsigned char *)*sp;
	unsigned c = *s++;
	int extra = 0;

	if (c >= 0xf0) {
		c &= 7;
		extra = 3;
	} else if (c >= 0xe0) {
		c &= 15;
		extra = 2;
	} else if (c >= 0xc0) {
		c &= 31;
		extra = 1;
	}
	while (extra-- && (*s & 0xc0) == 0x80)
		c = c << 6 | (*s++ & 63);
	*sp = (const char *)s;
	return c;
}

int utf8_encode(unsigned c, char *o)
{
	int n;

	if (c < 0x80) {
		o[0] = c;
		n = 1;
	} else if (c < 0x800) {
		o[0] = 0xc0 | c >> 6;
		o[1] = 0x80 | (c & 63);
		n = 2;
	} else if (c < 0x10000) {
		o[0] = 0xe0 | c >> 12;
		o[1] = 0x80 | ((c >> 6) & 63);
		o[2] = 0x80 | (c & 63);
		n = 3;
	} else {
		o[0] = 0xf0 | c >> 18;
		o[1] = 0x80 | ((c >> 12) & 63);
		o[2] = 0x80 | ((c >> 6) & 63);
		o[3] = 0x80 | (c & 63);
		n = 4;
	}
	o[n] = 0;
	return n;
}

static struct glyph *get_glyph(font_t *f, float size, unsigned cp)
{
	int size10 = (int)(size * 10 + 0.5f);
	unsigned h = (cp * 2654435761u) ^ (unsigned)size10 * 40503u ^
		     (unsigned)(uintptr_t)f;
	int i;

	for (i = 0; i < CACHE_SIZE; i++) {
		struct glyph *g = &cache[(h + i) % CACHE_SIZE];

		if (g->font == f && g->cp == cp && g->size10 == size10)
			return g;
		if (!g->font) {
			float scale = stbtt_ScaleForPixelHeight(&f->info, size);
			int adv, lsb, x0, y0, x1, y1;

			stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
			stbtt_GetCodepointBitmapBox(&f->info, cp, scale, scale,
						    &x0, &y0, &x1, &y1);
			g->font = f;
			g->cp = cp;
			g->size10 = size10;
			g->advance = adv * scale;
			g->w = x1 - x0;
			g->h = y1 - y0;
			g->xoff = x0;
			g->yoff = y0;
			if (g->w > 0 && g->h > 0) {
				g->bm = calloc((size_t)g->w * g->h, 1);
				stbtt_MakeCodepointBitmap(&f->info, g->bm, g->w,
							  g->h, g->w, scale,
							  scale, cp);
			}
			return g;
		}
	}
	/* Cache full: forget everything and start over. */
	for (i = 0; i < CACHE_SIZE; i++) {
		free(cache[i].bm);
		cache[i] = (struct glyph){ 0 };
	}
	return get_glyph(f, size, cp);
}

void font_vmetrics(font_t *f, float size, int *ascent, int *descent)
{
	float scale = stbtt_ScaleForPixelHeight(&f->info, size);
	int a, d, gap;

	stbtt_GetFontVMetrics(&f->info, &a, &d, &gap);
	if (ascent)
		*ascent = (int)ceilf(a * scale);
	if (descent)
		*descent = (int)ceilf(-d * scale);
}

int text_width(font_t *f, float size, const char *s)
{
	float scale = stbtt_ScaleForPixelHeight(&f->info, size);
	float x = 0;
	unsigned prev = 0;

	while (*s) {
		unsigned cp = utf8_next(&s);

		if (prev)
			x += scale * stbtt_GetCodepointKernAdvance(&f->info, prev, cp);
		x += get_glyph(f, size, cp)->advance;
		prev = cp;
	}
	return (int)ceilf(x);
}

static void put_mask(surface_t *dst, int x, int y, const struct glyph *g,
		     uint32_t color)
{
	blend_mask(dst, x + g->xoff, y + g->yoff, g->bm, g->w, g->h, color);
}

int draw_text(surface_t *dst, font_t *f, float size, int x, int y,
	      uint32_t color, const char *s)
{
	float scale = stbtt_ScaleForPixelHeight(&f->info, size);
	float fx = x;
	unsigned prev = 0;

	while (*s) {
		unsigned cp = utf8_next(&s);
		struct glyph *g;

		if (prev)
			fx += scale * stbtt_GetCodepointKernAdvance(&f->info, prev, cp);
		g = get_glyph(f, size, cp);
		if (g->bm)
			put_mask(dst, (int)(fx + 0.5f), y, g, color);
		fx += g->advance;
		prev = cp;
	}
	return (int)ceilf(fx);
}

void draw_text_center(surface_t *dst, font_t *f, float size, int x, int w,
		      int y, uint32_t color, const char *s)
{
	draw_text(dst, f, size, x + (w - text_width(f, size, s)) / 2, y, color, s);
}

void draw_text_shadow(surface_t *dst, font_t *f, float size, int x, int y,
		      uint32_t color, uint32_t shadow, const char *s)
{
	draw_text(dst, f, size, x + 1, y + 1, shadow, s);
	draw_text(dst, f, size, x, y + 2, ALPHA(shadow, (shadow >> 24) / 3), s);
	draw_text(dst, f, size, x, y, color, s);
}

surface_t *text_mask(font_t *f, float size, const char *s, int *baseline)
{
	int asc, desc;
	surface_t *m;

	font_vmetrics(f, size, &asc, &desc);
	m = surf_new(text_width(f, size, s) + 4, asc + desc + 4);
	draw_text(m, f, size, 2, asc + 2, 0xffffffff, s);
	if (baseline)
		*baseline = asc + 2;
	return m;
}
