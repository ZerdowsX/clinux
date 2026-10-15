// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - an Aqua-style desktop shell that draws straight to the
 * kernel's DRM/KMS (or fbdev) display, with no X server.
 *
 * Usage:
 *   watercandy                 run on the console
 *   watercandy --demo          also open "About This Computer" at startup
 *   watercandy --open CMD...   show an X11 program in a Water Candy window
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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "display.h"
#include "wc.h"

static display_t disp;
static int have_display;
static int ctl_fd = -1;

#define CTL_SOCK "/run/watercandy.sock"

/* Local control socket: "watercandy --open firefox" from a terminal. */
static void ctl_open(void)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };

	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", CTL_SOCK);
	ctl_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (ctl_fd < 0)
		return;
	unlink(CTL_SOCK);
	if (bind(ctl_fd, (struct sockaddr *)&sa, sizeof(sa))) {
		close(ctl_fd);
		ctl_fd = -1;
		return;
	}
	/* Only the desktop user (and root) may ask for programs. */
	if (chown(CTL_SOCK, dt.uid, dt.gid) || chmod(CTL_SOCK, 0600))
		fprintf(stderr, "watercandy: cannot set control socket owner\n");
}

static void ctl_read(void)
{
	char buf[1024], title[64];
	ssize_t n;

	while ((n = recv(ctl_fd, buf, sizeof(buf) - 1, 0)) > 0) {
		const char *base;

		buf[n] = 0;
		if (!strncmp(buf, "open ", 5) && buf[5]) {
			snprintf(title, sizeof(title), "%.*s",
				 (int)strcspn(buf + 5, " "), buf + 5);
			base = strrchr(title, '/') ? strrchr(title, '/') + 1 : title;
			app_x11(buf + 5, base, APP_X11);
			dt.redraw = 1;
		}
	}
}

static int ctl_send_open(int argc, char **argv)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	char msg[1024] = "open";
	int fd, i;

	for (i = 2; i < argc; i++) {
		strncat(msg, " ", sizeof(msg) - strlen(msg) - 1);
		strncat(msg, argv[i], sizeof(msg) - strlen(msg) - 1);
	}
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", CTL_SOCK);
	fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0 || sendto(fd, msg, strlen(msg), 0, (struct sockaddr *)&sa,
			     sizeof(sa)) < 0) {
		perror("watercandy: is the desktop running?");
		return 1;
	}
	close(fd);
	return 0;
}

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

void session_start(void)
{
	/* Look for software updates a minute after logging in. */
	updates_schedule(60);
	dt.redraw = 1;
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
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
		proc_child_exited(pid, status);
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
		else if (!strcmp(argv[i], "firefox"))
			app_firefox();
		else if (!strncmp(argv[i], "x11:", 4))
			app_x11(argv[i] + 4, argv[i] + 4, APP_X11);
		else if (!strcmp(argv[i], "terminal"))
			app_terminal();
		else if (!strcmp(argv[i], "prefs"))
			app_prefs();
		else if (!strcmp(argv[i], "clock"))
			app_clock();
		else if (!strcmp(argv[i], "store"))
			app_store(0);
		else if (!strcmp(argv[i], "notify"))
			notify("Обновление ПО", "Доступно 3 обновления", NULL);
		else if (!strncmp(argv[i], "edit:", 5))
			app_textedit(argv[i] + 5);
		else if (!strncmp(argv[i], "view:", 5))
			app_preview(argv[i] + 5);
		else if (!strncmp(argv[i], "spot:", 5)) {
			int k;

			event_t ev = { .type = EV_KEYPRESS, .key = KEY_SPACE, .mods = MOD_ALT };
			wm_event(&ev);
			for (k = 5; argv[i][k]; ) {
				const char *p = argv[i] + k;
				event_t c = { .type = EV_KEYPRESS };

				c.ch = utf8_next(&p);
				k = p - argv[i];
				wm_event(&c);
			}
		}
		else if (!strcmp(argv[i], "login"))
			login_start();
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
	while (now_s() - t0 < (getenv("WC_SETTLE") ? atof(getenv("WC_SETTLE")) : 0.6)) {
		dt.now = now_s();
		wm_tick();
		reap_children();
		proc_tick();
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
	if (argc > 2 && !strcmp(argv[1], "--open"))
		return ctl_send_open(argc, argv);
	if (!getenv("HOME"))
		setenv("HOME", "/root", 1);
	user_init();
	i18n_init();
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
	if (user_can_login() && !user_autologin()) {
		login_start();
	} else {
		if (user_autologin())
			user_select(user_autologin());
		session_start();
	}
	if (argc > 1 && !strcmp(argv[1], "--demo"))
		app_about();
	ctl_open();

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
		if ((wm_find_app(APP_TERMINAL) || wm_find_app(APP_X11) ||
		     wm_find_app(APP_FIREFOX)) && timeout > 30)
			timeout = 30;

		npfd = input_pollfds(pfds, 15);
		if (ctl_fd >= 0) {
			pfds[npfd].fd = ctl_fd;
			pfds[npfd].events = POLLIN;
			npfd++;
		}
		if (poll(pfds, npfd, timeout) < 0 && errno != EINTR)
			break;
		input_dispatch(wm_event);
		if (ctl_fd >= 0)
			ctl_read();

		dt.now = now_s();
		if (dt.set.animate_wallpaper && dt.now - last_wall >= wall_interval) {
			wallpaper_render(dt.wall, dt.now - t0);
			last_wall = dt.now;
			dt.redraw = 1;
		}
		wm_tick();
		reap_children();
		proc_tick();
		updates_tick();
		sound_tick();
		wifi_tick();

		if (dt.redraw || wm_animating()) {
			wm_render();
			display_present(&disp, dt.frame, 0, 0, dt.w, dt.h);
			dt.redraw = 0;
		}
	}
	display_close(&disp);
	return 0;
}
