// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - reading images (stb_image) and writing PNG screenshots.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "stb_image.h"
#pragma GCC diagnostic pop

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "wc.h"

surface_t *image_load(const char *path)
{
	int w, h, n, i;
	unsigned char *px = stbi_load(path, &w, &h, &n, 4);
	surface_t *s;

	if (!px)
		return NULL;
	s = surf_new(w, h);
	for (i = 0; i < w * h; i++) {
		unsigned r = px[4 * i], g = px[4 * i + 1], b = px[4 * i + 2];
		unsigned a = px[4 * i + 3];

		s->px[i] = a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
	}
	stbi_image_free(px);
	return s;
}

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const unsigned char *buf, size_t len)
{
	size_t i;

	if (!crc_table[1]) {
		for (i = 0; i < 256; i++) {
			uint32_t v = i;
			int k;

			for (k = 0; k < 8; k++)
				v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	}
	c ^= 0xffffffffu;
	for (i = 0; i < len; i++)
		c = crc_table[(c ^ buf[i]) & 255] ^ (c >> 8);
	return c ^ 0xffffffffu;
}

static void be32(unsigned char *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
	unsigned char hdr[8], tail[4];
	uint32_t c;

	be32(hdr, len);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	fwrite(data, 1, len, f);
	c = crc(0, hdr + 4, 4);
	c = crc(c, data, len);
	be32(tail, c);
	fwrite(tail, 1, 4, f);
}

#ifndef HAVE_ZLIB
/* zlib stream made of stored (uncompressed) deflate blocks. */
static unsigned char *store_deflate(const unsigned char *raw, size_t n, size_t *out_len)
{
	size_t blocks = n / 65535 + 1, i, o = 0;
	unsigned char *out = malloc(n + blocks * 5 + 6);
	uint32_t a = 1, b = 0;

	out[o++] = 0x78;
	out[o++] = 0x01;
	for (i = 0; i < n || i == 0; i += 65535) {
		size_t len = n - i > 65535 ? 65535 : n - i;

		out[o++] = i + len >= n;
		out[o++] = len & 255;
		out[o++] = len >> 8;
		out[o++] = ~len & 255;
		out[o++] = (~len >> 8) & 255;
		memcpy(out + o, raw + i, len);
		o += len;
		if (!n)
			break;
	}
	for (i = 0; i < n; i++) {
		a = (a + raw[i]) % 65521;
		b = (b + a) % 65521;
	}
	be32(out + o, b << 16 | a);
	*out_len = o + 4;
	return out;
}
#endif

surface_t *image_load_mem(const unsigned char *data, size_t len)
{
	int w, h, n, i;
	unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
	surface_t *s;

	if (!px)
		return NULL;
	s = surf_new(w, h);
	for (i = 0; i < w * h; i++) {
		unsigned r = px[4 * i], g = px[4 * i + 1], b = px[4 * i + 2];
		unsigned a = px[4 * i + 3];

		s->px[i] = a << 24 | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
	}
	stbi_image_free(px);
	return s;
}

int image_save_png(const char *path, const surface_t *s)
{
	static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	size_t rowlen = (size_t)s->w * 3 + 1, n = rowlen * s->h, zlen;
	unsigned char *raw = malloc(n), *z, ihdr[13];
	FILE *f;
	int x, y;

	if (!raw)
		return -1;
	for (y = 0; y < s->h; y++) {
		unsigned char *r = raw + y * rowlen;

		r[0] = 0;	/* filter: none */
		for (x = 0; x < s->w; x++) {
			uint32_t p = s->px[(size_t)y * s->w + x];

			r[1 + 3 * x] = p >> 16;
			r[2 + 3 * x] = p >> 8;
			r[3 + 3 * x] = p;
		}
	}
#ifdef HAVE_ZLIB
	{
		uLongf zl = compressBound(n);

		z = malloc(zl);
		if (compress2(z, &zl, raw, n, 6) != Z_OK) {
			free(z);
			free(raw);
			return -1;
		}
		zlen = zl;
	}
#else
	z = store_deflate(raw, n, &zlen);
#endif
	free(raw);
	f = fopen(path, "wb");
	if (!f) {
		free(z);
		return -1;
	}
	be32(ihdr, s->w);
	be32(ihdr + 4, s->h);
	ihdr[8] = 8;		/* bit depth */
	ihdr[9] = 2;		/* RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	fwrite(sig, 1, 8, f);
	chunk(f, "IHDR", ihdr, 13);
	chunk(f, "IDAT", z, zlen);
	chunk(f, "IEND", NULL, 0);
	free(z);
	return fclose(f);
}
