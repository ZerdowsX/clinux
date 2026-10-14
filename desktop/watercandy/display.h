/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WC_DISPLAY_H
#define WC_DISPLAY_H

#include <stdint.h>

#include "gfx.h"

typedef struct display {
	int w, h;
	int fd;
	int is_drm;
	uint8_t *map;
	uint32_t pitch;
	size_t size;
	uint32_t fb_id, handle, crtc_id, conn_id;
	const char *name;
} display_t;

int display_open(display_t *d);
void display_present(display_t *d, const surface_t *frame, int x, int y,
		     int w, int h);
void display_close(display_t *d);

#endif
