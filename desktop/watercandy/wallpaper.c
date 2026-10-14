// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - animated procedural blue wallpaper.
 *
 * A low resolution field of soft light and shadow swooshes is computed for
 * every update and smoothly upscaled, which keeps animation cheap.
 */
#include <math.h>
#include <stdlib.h>

#include "wc.h"

#define SCALE 4

struct band {
	float y0, slope, amp, freq, phase, speed, width, strength;
	float r, g, b;		/* target color for light bands */
};

static const struct band bands[] = {
	/* broad glows */
	{ 0.22f, 0.30f, 0.10f, 2.1f, 0.0f, 0.23f, 0.20f, 0.30f, 0.55f, 0.76f, 1.00f },
	{ 0.80f, -0.20f, 0.08f, 1.4f, 1.3f, 0.17f, 0.22f, 0.18f, 0.45f, 0.68f, 0.98f },
	/* dark swooshes */
	{ 0.62f, -0.32f, 0.11f, 1.7f, 2.0f, 0.19f, 0.13f, -0.45f, 0, 0, 0 },
	{ 0.10f, 0.45f, 0.07f, 2.4f, 0.7f, 0.13f, 0.09f, -0.30f, 0, 0, 0 },
	/* thin bright streaks */
	{ 0.52f, -0.28f, 0.09f, 2.6f, 2.1f, 0.21f, 0.022f, 0.40f, 0.80f, 0.92f, 1.00f },
	{ 0.33f, 0.18f, 0.06f, 3.1f, 4.0f, 0.27f, 0.016f, 0.32f, 0.85f, 0.95f, 1.00f },
	{ 0.72f, -0.10f, 0.05f, 3.6f, 5.1f, 0.15f, 0.012f, 0.25f, 0.75f, 0.88f, 1.00f },
};

static int lw, lh;
static float *field;		/* lw*lh*3 floats */

void wallpaper_init(int w, int h)
{
	lw = w / SCALE + 2;
	lh = h / SCALE + 2;
	field = malloc(sizeof(float) * 3 * lw * lh);
}

static void compute(double t, float speed)
{
	float aspect = (float)lw / lh;
	float T = (float)(t * speed);
	int x, y;
	size_t i;

	for (y = 0; y < lh; y++) {
		float v = (float)y / (lh - 1);

		for (x = 0; x < lw; x++) {
			float u = (float)x / (lh - 1);
			float base = 0.55f * (u / aspect) + 0.45f * (1 - v);
			float r = 0.03f + 0.22f * base;
			float g = 0.16f + 0.36f * base;
			float b = 0.48f + 0.42f * base;
			float bx, by, d2, k, vig;

			for (i = 0; i < sizeof(bands) / sizeof(bands[0]); i++) {
				const struct band *bd = &bands[i];
				float yc = bd->y0 + bd->slope * u +
					   bd->amp * sinf(bd->freq * u + bd->phase +
							  bd->speed * T);
				float wv = bd->width *
					   (0.65f + 0.35f * sinf(1.3f * u + bd->phase * 0.7f +
								 0.11f * T));
				float d = (v - yc) / wv;

				k = expf(-d * d) * bd->strength;
				if (k > 0) {
					r += (bd->r - r) * k;
					g += (bd->g - g) * k;
					b += (bd->b - b) * k;
				} else {
					r *= 1 + k * 0.9f;
					g *= 1 + k * 0.8f;
					b *= 1 + k * 0.55f;
				}
			}

			/* A soft light drifting across the upper right. */
			bx = 0.72f * aspect + 0.12f * sinf(0.09f * T);
			by = 0.28f + 0.08f * cosf(0.07f * T);
			d2 = ((u - bx) * (u - bx) + (v - by) * (v - by)) / 0.16f;
			k = 0.22f * expf(-d2);
			r += (0.62f - r) * k;
			g += (0.80f - g) * k;
			b += (1.00f - b) * k;

			/* Vignette. */
			vig = 1 - 0.28f * ((u / aspect - 0.5f) * (u / aspect - 0.5f) +
					   (v - 0.5f) * (v - 0.5f));
			field[3 * (y * lw + x) + 0] = r * vig;
			field[3 * (y * lw + x) + 1] = g * vig;
			field[3 * (y * lw + x) + 2] = b * vig;
		}
	}
}

static const float bayer[4][4] = {
	{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
};

void wallpaper_render(surface_t *dst, double t)
{
	int x, y;

	compute(t, dt.set.wallpaper_speed);
	for (y = 0; y < dst->h; y++) {
		float fy = (y + 0.5f) / SCALE;
		int iy = (int)fy;
		float ty = fy - iy;
		uint32_t *row = dst->px + (size_t)y * dst->w;
		const float *r0 = field + 3 * (size_t)iy * lw;
		const float *r1 = field + 3 * (size_t)(iy + 1 < lh ? iy + 1 : iy) * lw;

		for (x = 0; x < dst->w; x++) {
			float fx = (x + 0.5f) / SCALE;
			int ix = (int)fx;
			int ix1 = ix + 1 < lw ? ix + 1 : ix;
			float tx = fx - ix;
			float dither = (bayer[y & 3][x & 3] / 16.0f - 0.5f) / 255.0f;
			uint32_t out = 0xff000000u;
			int c;

			for (c = 0; c < 3; c++) {
				float a = r0[3 * ix + c] + (r0[3 * ix1 + c] - r0[3 * ix + c]) * tx;
				float b = r1[3 * ix + c] + (r1[3 * ix1 + c] - r1[3 * ix + c]) * tx;
				float v = a + (b - a) * ty + dither;
				int iv = (int)(v * 255 + 0.5f);

				if (iv < 0)
					iv = 0;
				if (iv > 255)
					iv = 255;
				out |= (uint32_t)iv << (16 - 8 * c);
			}
			row[x] = out;
		}
	}
}
