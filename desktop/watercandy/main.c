// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - an Aqua-style desktop shell that draws straight to the
 * kernel's DRM/KMS (or fbdev) display, with no X server.
 *
 * Usage:
 *   watercandy                 run on the console
 *   watercandy --demo          also open "About This Computer" at startup
 *   watercandy --render FILE [WxH] [about|files|terminal|prefs|menu]...
 *                              render one frame offscreen to a PPM file
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

#include "display.h"
#include "wc.h"

static display_t disp;
static int have_display;

void wc_shutdown(int reboot_instead)
{
	if (have_display) {
		surf_clear(dt.frame, 0xff000000);
		display_present(&disp, dt.frame, 0, 0, dt.w, dt.h);
	}
	sync();
	if (getpid() == 1 || !geteuid())
		reboot(reboot_instead ? RB_AUTOBOOT : RB_POWER_OFF);
	dt.quit = 1;
}

static void init_desktop(int w, int h)
{
	dt.w = w;
	dt.h = h;
	dt.frame = surf_new(w, h);
	dt.wall = surf_new(w, h);
	dt.set.animate_wallpaper = 1;
	dt.set.magnify_dock = 1;
	dt.set.show_seconds = 0;
	dt.set.wallpaper_speed = 1.0f;
	dt.now = now_s();
	wallpaper_init(w, h);
	wallpaper_render(dt.wall, 0);
	wm_init();
	dt.redraw = 1;
}

static int write_ppm(const char *path, const surface_t *s)
{
	FILE *f = fopen(path, "wb");
	int i, n = s->w * s->h;

	if (!f)
		return -1;
	fprintf(f, "P6\n%d %d\n255\n", s->w, s->h);
	for (i = 0; i < n; i++) {
		unsigned char px[3] = { s->px[i] >> 16, s->px[i] >> 8, s->px[i] };

		fwrite(px, 1, 3, f);
	}
	return fclose(f);
}

static void reap_children(void)
{
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
}

/* Offscreen rendering, used to preview the desktop without hardware. */
static int render_offscreen(int argc, char **argv)
{
	const char *out = argv[2];
	int w = 1280, h = 800, i;
	double t0;

	if (argc > 3 && sscanf(argv[3], "%dx%d", &w, &h) == 2)
		i = 4;
	else
		i = 3;
	init_desktop(w, h);
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "about"))
			app_about();
		else if (!strcmp(argv[i], "files"))
			app_files("/", "Water HD");
		else if (!strcmp(argv[i], "terminal"))
			app_terminal();
		else if (!strcmp(argv[i], "prefs"))
			app_prefs();
		else if (!strcmp(argv[i], "clock"))
			app_clock();
		else if (!strcmp(argv[i], "menu")) {
			event_t ev = { .type = EV_BUTTON, .x = 15, .y = 10, .button = 1,
				       .pressed = 1 };

			wm_event(&ev);
			ev.pressed = 0;
			wm_event(&ev);
			ev.type = EV_MOTION;
			ev.y = 32;
			wm_event(&ev);
		}
	}
	/* let open animations and shells settle */
	t0 = now_s();
	while (now_s() - t0 < 0.6) {
		dt.now = now_s();
		wm_tick();
		usleep(20000);
	}
	dt.now = now_s() + 1;
	wm_tick();
	wm_render();
	return write_ppm(out, dt.frame) ? 1 : 0;
}

int main(int argc, char **argv)
{
	struct pollfd pfds[16];
	double t0, last_wall = 0;
	int npfd;

	signal(SIGPIPE, SIG_IGN);
	if (!getenv("HOME"))
		setenv("HOME", "/root", 1);
	if (fonts_init())
		return 1;
	if (argc > 2 && !strcmp(argv[1], "--render"))
		return render_offscreen(argc, argv);

	if (display_open(&disp)) {
		fprintf(stderr, "watercandy: no display (need /dev/dri/card0 or /dev/fb0)\n");
		return 1;
	}
	have_display = 1;
	fprintf(stderr, "watercandy: %dx%d via %s\n", disp.w, disp.h, disp.name);
	if (input_open(disp.w, disp.h))
		fprintf(stderr, "watercandy: warning: no input devices\n");
	init_desktop(disp.w, disp.h);
	if (argc > 1 && !strcmp(argv[1], "--demo"))
		app_about();

	t0 = now_s();
	while (!dt.quit) {
		int timeout = 250;
		double wall_interval = 0.12;

		dt.now = now_s();
		if (dt.set.animate_wallpaper) {
			double left = wall_interval - (dt.now - last_wall);

			timeout = left > 0 ? (int)(left * 1000) + 1 : 0;
		}
		if (wm_animating() && timeout > 15)
			timeout = 15;
		if (wm_find_app(APP_TERMINAL) && timeout > 30)
			timeout = 30;

		npfd = input_pollfds(pfds, 16);
		if (poll(pfds, npfd, timeout) < 0 && errno != EINTR)
			break;
		input_dispatch(wm_event);

		dt.now = now_s();
		if (dt.set.animate_wallpaper && dt.now - last_wall >= wall_interval) {
			wallpaper_render(dt.wall, dt.now - t0);
			last_wall = dt.now;
			dt.redraw = 1;
		}
		wm_tick();
		reap_children();

		if (dt.redraw || wm_animating()) {
			wm_render();
			display_present(&disp, dt.frame, 0, 0, dt.w, dt.h);
			dt.redraw = 0;
		}
	}
	display_close(&disp);
	return 0;
}
