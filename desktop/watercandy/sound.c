// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - volume: menu bar slider, volume keys and the on-screen
 * level display. Talks to PipeWire through wpctl, as the desktop user.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "wc.h"

#define MENU_W 44
#define MENU_H 160
#define TRACK_TOP 18
#define TRACK_BOT 142

static float volume = 0.5f;
static int muted, known, dragging;
static double osd_t0 = -10, last_poll = -100;

int sound_available(void)
{
	return !access("/usr/bin/wpctl", X_OK) && !login_active();
}

static void got_volume(int status, const char *out, void *ctx)
{
	float v;

	(void)ctx;
	if (!status && out && sscanf(out, "Volume: %f", &v) == 1) {
		volume = v > 1 ? 1 : v;
		muted = strstr(out, "MUTED") != NULL;
		known = 1;
		wm_menu_changed();
		dt.redraw = 1;
	}
}

static void poll_volume(void)
{
	const char *argv[] = { "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL };

	last_poll = dt.now;
	proc_run(argv, PROC_AS_USER, got_volume, NULL, NULL);
}

void sound_tick(void)
{
	if (sound_available() && dt.now - last_poll > 10)
		poll_volume();
}

static void apply_volume(void)
{
	char v[16];
	const char *argv[] = { "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", v, NULL };
	const char *unmute[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "0", NULL };

	snprintf(v, sizeof(v), "%.2f", volume);
	proc_run(argv, PROC_AS_USER, NULL, NULL, NULL);
	if (muted) {
		muted = 0;
		proc_run(unmute, PROC_AS_USER, NULL, NULL, NULL);
	}
	dt.redraw = 1;
}

/* Speaker with sound waves; level 0..3 waves, -1 = muted. */
void sound_draw_speaker(surface_t *s, float x, float y, float S, int level,
			uint32_t color)
{
	float body[] = { x + 0.10f * S, y + 0.36f * S, x + 0.28f * S, y + 0.36f * S,
			 x + 0.52f * S, y + 0.14f * S, x + 0.52f * S, y + 0.86f * S,
			 x + 0.28f * S, y + 0.64f * S, x + 0.10f * S, y + 0.64f * S };
	paint_t p = paint_solid(color);
	int i, k;

	fill_poly(s, body, 6, &p);
	for (i = 1; i <= level; i++) {
		float r = (0.10f + 0.13f * i) * S, cx = x + 0.52f * S, cy = y + 0.5f * S;

		for (k = 0; k < 8; k++) {
			float a0 = -0.85f + 1.7f * k / 8, a1 = -0.85f + 1.7f * (k + 1) / 8;

			draw_line(s, cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1),
				  cy + r * sinf(a1), S / 12 + 0.4f, &p);
		}
	}
	if (level < 0) {
		p = paint_solid(color);
		draw_line(s, x + 0.66f * S, y + 0.36f * S, x + 0.92f * S, y + 0.64f * S,
			  S / 11, &p);
		draw_line(s, x + 0.92f * S, y + 0.36f * S, x + 0.66f * S, y + 0.64f * S,
			  S / 11, &p);
	}
}

static int level_of(void)
{
	if (muted || volume < 0.01f)
		return -1;
	return volume < 0.34f ? 1 : volume < 0.67f ? 2 : 3;
}

void sound_menu_icon(surface_t *s, int x, int w, int on)
{
	sound_draw_speaker(s, x + (w - 18) / 2.0f, 2, 18, level_of(),
			   on ? 0xffffffff : 0xff000000);
}

static void menu_draw(surface_t *s, int w, int h)
{
	float ty = TRACK_BOT - volume * (TRACK_BOT - TRACK_TOP);
	paint_t p;

	(void)h;
	p = paint_vgrad(0, 1, 0xff8c8c8c, 0xffd6d6d6);
	fill_rrect(s, w / 2 - 3, TRACK_TOP, 6, TRACK_BOT - TRACK_TOP, 3, &p);
	p = paint_vgrad(ty, TRACK_BOT, 0xff8ec0f8, 0xff2a6ad8);
	fill_rrect(s, w / 2 - 3, ty, 6, TRACK_BOT - ty, 3, &p);
	p = paint_solid(0xff707070);
	stroke_rrect(s, w / 2 - 3, TRACK_TOP, 6, TRACK_BOT - TRACK_TOP, 3, 1, &p);
	aqua_gel_circle(s, w / 2.0f, ty, 8, 0xff3a8be6);
}

static int menu_mouse(const event_t *ev, int x, int y)
{
	(void)x;
	if (ev->type == EV_BUTTON && ev->button == 1)
		dragging = ev->pressed;
	if (dragging || (ev->type == EV_BUTTON && ev->pressed)) {
		float v = (float)(TRACK_BOT - y) / (TRACK_BOT - TRACK_TOP);

		volume = v < 0 ? 0 : v > 1 ? 1 : v;
		apply_volume();
	}
	return 0;
}

static void menu_opened(void)
{
	poll_volume();
}

const struct menu_custom volume_menu = {
	.w = MENU_W, .h = MENU_H, .draw = menu_draw, .mouse = menu_mouse,
	.opened = menu_opened,
};

/* Volume keys; returns 1 if the key was one of them. */
int sound_key(const event_t *ev)
{
	const char *mute[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle", NULL };

	switch (ev->key) {
	case KEY_VOLUMEUP:
		volume = volume + 0.0625f > 1 ? 1 : volume + 0.0625f;
		apply_volume();
		break;
	case KEY_VOLUMEDOWN:
		volume = volume - 0.0625f < 0 ? 0 : volume - 0.0625f;
		apply_volume();
		break;
	case KEY_MUTE:
		muted = !muted;
		proc_run(mute, PROC_AS_USER, NULL, NULL, NULL);
		break;
	default:
		return 0;
	}
	osd_t0 = dt.now;
	dt.redraw = 1;
	return 1;
}

int sound_osd_active(void)
{
	return dt.now - osd_t0 < 1.6;
}

/* The big translucent square in the lower middle of the screen. */
void sound_osd_render(surface_t *f)
{
	double age = dt.now - osd_t0;
	int a = age < 1.2 ? 255 : (int)(255 * (1.6 - age) / 0.4);
	int sz = 200, x = (f->w - sz) / 2, y = f->h * 2 / 3 - sz / 2, i;
	surface_t *s;
	paint_t p;

	if (age >= 1.6)
		return;
	s = surf_new(sz, sz);
	p = paint_solid(0x70000000);
	fill_rrect(s, 0, 0, sz, sz, 22, &p);
	sound_draw_speaker(s, 50, 30, 100, level_of(), 0xe0ffffff);
	for (i = 0; i < 16; i++) {
		int on = !muted && volume * 16 > i + 0.5f;

		fill_rect(s, 20 + i * 10, 160, 8, 10, on ? 0xf0ffffff : 0x50ffffff);
	}
	blit(f, s, x, y, a);
	surf_free(s);
}
