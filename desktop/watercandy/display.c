// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - output engine.
 *
 * Talks to the kernel directly: DRM/KMS dumb buffers when a DRM device is
 * present, the legacy fbdev interface otherwise. No X server involved.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/fb.h>
#include <linux/kd.h>

#include "display.h"

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r;
}

static int drm_open(display_t *d, const char *path)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t *conns, *encs, *crtcs;
	struct drm_mode_get_connector conn;
	struct drm_mode_modeinfo *modes = NULL, mode;
	struct drm_mode_get_encoder enc;
	struct drm_mode_create_dumb creq = { 0 };
	struct drm_mode_map_dumb mreq = { 0 };
	struct drm_mode_fb_cmd fbcmd = { 0 };
	struct drm_mode_crtc crtc = { 0 };
	unsigned i, j;
	int found = 0;

	d->fd = open(path, O_RDWR | O_CLOEXEC);
	if (d->fd < 0)
		return -1;
	xioctl(d->fd, DRM_IOCTL_SET_MASTER, NULL);

	if (xioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		goto fail;
	conns = calloc(res.count_connectors + 1, 4);
	encs = calloc(res.count_encoders + 1, 4);
	crtcs = calloc(res.count_crtcs + 1, 4);
	res.connector_id_ptr = (uintptr_t)conns;
	res.encoder_id_ptr = (uintptr_t)encs;
	res.crtc_id_ptr = (uintptr_t)crtcs;
	res.count_fbs = 0;
	if (xioctl(d->fd, DRM_IOCTL_MODE_GETRESOURCES, &res))
		goto fail;

	for (i = 0; i < res.count_connectors && !found; i++) {
		memset(&conn, 0, sizeof(conn));
		conn.connector_id = conns[i];
		if (xioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn))
			continue;
		if (conn.connection != 1 || !conn.count_modes)
			continue;
		free(modes);
		modes = calloc(conn.count_modes, sizeof(*modes));
		conn.modes_ptr = (uintptr_t)modes;
		conn.count_props = 0;
		conn.count_encoders = 0;
		if (xioctl(d->fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn))
			continue;

		mode = modes[0];
		for (j = 0; j < conn.count_modes; j++) {
			if (modes[j].type & DRM_MODE_TYPE_PREFERRED) {
				mode = modes[j];
				break;
			}
		}

		d->crtc_id = 0;
		if (conn.encoder_id) {
			memset(&enc, 0, sizeof(enc));
			enc.encoder_id = conn.encoder_id;
			if (!xioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &enc))
				d->crtc_id = enc.crtc_id;
		}
		for (j = 0; !d->crtc_id && j < res.count_encoders; j++) {
			unsigned k;

			memset(&enc, 0, sizeof(enc));
			enc.encoder_id = encs[j];
			if (xioctl(d->fd, DRM_IOCTL_MODE_GETENCODER, &enc))
				continue;
			for (k = 0; k < res.count_crtcs; k++) {
				if (enc.possible_crtcs & (1u << k)) {
					d->crtc_id = crtcs[k];
					break;
				}
			}
		}
		if (!d->crtc_id)
			continue;
		d->conn_id = conns[i];
		found = 1;
	}
	if (!found)
		goto fail;

	d->w = mode.hdisplay;
	d->h = mode.vdisplay;
	creq.width = d->w;
	creq.height = d->h;
	creq.bpp = 32;
	if (xioctl(d->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq))
		goto fail;
	d->handle = creq.handle;
	d->pitch = creq.pitch;
	d->size = creq.size;

	fbcmd.width = d->w;
	fbcmd.height = d->h;
	fbcmd.bpp = 32;
	fbcmd.depth = 24;
	fbcmd.pitch = d->pitch;
	fbcmd.handle = d->handle;
	if (xioctl(d->fd, DRM_IOCTL_MODE_ADDFB, &fbcmd))
		goto fail;
	d->fb_id = fbcmd.fb_id;

	mreq.handle = d->handle;
	if (xioctl(d->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq))
		goto fail;
	d->map = mmap(NULL, d->size, PROT_READ | PROT_WRITE, MAP_SHARED,
		      d->fd, mreq.offset);
	if (d->map == MAP_FAILED)
		goto fail;
	memset(d->map, 0, d->size);

	crtc.crtc_id = d->crtc_id;
	crtc.fb_id = d->fb_id;
	crtc.set_connectors_ptr = (uintptr_t)&d->conn_id;
	crtc.count_connectors = 1;
	crtc.mode = mode;
	crtc.mode_valid = 1;
	if (xioctl(d->fd, DRM_IOCTL_MODE_SETCRTC, &crtc))
		goto fail;

	d->is_drm = 1;
	d->name = "DRM/KMS";
	free(modes);
	return 0;
fail:
	free(modes);
	close(d->fd);
	d->fd = -1;
	return -1;
}

static int fbdev_open(display_t *d)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	d->fd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	if (d->fd < 0)
		return -1;
	if (xioctl(d->fd, FBIOGET_VSCREENINFO, &var) ||
	    xioctl(d->fd, FBIOGET_FSCREENINFO, &fix) || var.bits_per_pixel != 32) {
		close(d->fd);
		return -1;
	}
	d->w = var.xres;
	d->h = var.yres;
	d->pitch = fix.line_length;
	d->size = (size_t)fix.line_length * var.yres_virtual;
	d->map = mmap(NULL, d->size, PROT_READ | PROT_WRITE, MAP_SHARED, d->fd, 0);
	if (d->map == MAP_FAILED) {
		close(d->fd);
		return -1;
	}
	d->is_drm = 0;
	d->name = "fbdev";
	return 0;
}

static void console_graphics(int on)
{
	int fd = open("/dev/tty0", O_RDWR | O_CLOEXEC);

	if (fd < 0)
		return;
	ioctl(fd, KDSETMODE, on ? KD_GRAPHICS : KD_TEXT);
	close(fd);
}

int display_open(display_t *d)
{
	char path[32];
	int i;

	memset(d, 0, sizeof(*d));
	for (i = 0; i < 4; i++) {
		snprintf(path, sizeof(path), "/dev/dri/card%d", i);
		if (!drm_open(d, path))
			goto ok;
	}
	if (fbdev_open(d))
		return -1;
ok:
	console_graphics(1);
	return 0;
}

void display_present(display_t *d, const surface_t *frame, int x, int y,
		     int w, int h)
{
	struct drm_mode_fb_dirty_cmd dirty = { 0 };
	struct drm_clip_rect clip;
	int j;

	if (x < 0) {
		w += x;
		x = 0;
	}
	if (y < 0) {
		h += y;
		y = 0;
	}
	if (x + w > d->w)
		w = d->w - x;
	if (y + h > d->h)
		h = d->h - y;
	if (w <= 0 || h <= 0)
		return;

	/* Screen surfaces are opaque, so premultiplied ARGB == XRGB8888. */
	for (j = y; j < y + h; j++)
		memcpy(d->map + (size_t)j * d->pitch + (size_t)x * 4,
		       frame->px + (size_t)j * frame->w + x, (size_t)w * 4);

	if (!d->is_drm)
		return;
	clip.x1 = x;
	clip.y1 = y;
	clip.x2 = x + w;
	clip.y2 = y + h;
	dirty.fb_id = d->fb_id;
	dirty.num_clips = 1;
	dirty.clips_ptr = (uintptr_t)&clip;
	xioctl(d->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

void display_close(display_t *d)
{
	console_graphics(0);
	if (d->map && d->map != MAP_FAILED)
		munmap(d->map, d->size);
	if (d->fd >= 0)
		close(d->fd);
}
