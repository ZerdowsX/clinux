/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WC_FONT_H
#define WC_FONT_H

#include "gfx.h"

typedef struct font font_t;

enum { FONT_UI, FONT_UI_BOLD, FONT_SERIF, FONT_MONO, FONT_COUNT };

extern font_t *fonts[FONT_COUNT];

int fonts_init(void);
font_t *font_load(const char *const *paths);

/* Width in pixels of a UTF-8 string. */
int text_width(font_t *f, float size, const char *s);
/* Draw a UTF-8 string with its baseline at y; returns the end x. */
int draw_text(surface_t *dst, font_t *f, float size, int x, int y,
	      uint32_t color, const char *s);
/* Draw centered horizontally inside [x, x+w). */
void draw_text_center(surface_t *dst, font_t *f, float size, int x, int w,
		      int y, uint32_t color, const char *s);
/* Text with a soft 1px shadow underneath (desktop / dock labels). */
void draw_text_shadow(surface_t *dst, font_t *f, float size, int x, int y,
		      uint32_t color, uint32_t shadow, const char *s);
/* Render text into an alpha-only (white) surface; used for glass effects. */
surface_t *text_mask(font_t *f, float size, const char *s, int *baseline);
/* Font metrics in pixels for a given size. */
void font_vmetrics(font_t *f, float size, int *ascent, int *descent);

/* Decode one UTF-8 code point and advance *s. */
unsigned utf8_next(const char **s);

#endif
