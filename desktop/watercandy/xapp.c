// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - X11 bridge, in the spirit of X11.app on Mac OS X.
 *
 * Each X11 program gets its own virtual X server (Xvfb) whose framebuffer
 * lives in a memory-mapped file. Water Candy shows that framebuffer inside
 * an Aqua window, injects pointer and keyboard input with the XTEST
 * extension, and acts as a minimal window manager inside the X server so
 * the program's main window always fills the Aqua window.
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wc.h"

static const char *const firefox_candidates[] = {
	"/usr/bin/firefox", "/usr/bin/firefox-esr", "/usr/lib/firefox/firefox",
	"/opt/firefox/firefox", NULL
};

/* Ubuntu's "firefox" deb is a wrapper that needs snapd; skip it. */
static int is_snap_wrapper(const char *path)
{
	char buf[4096];
	FILE *f = fopen(path, "r");
	size_t n;
	struct stat st;

	if (!f)
		return 0;
	if (fstat(fileno(f), &st) || st.st_size > (off_t)sizeof(buf)) {
		fclose(f);
		return 0;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;
	return strstr(buf, "/snap/") != NULL;
}

const char *firefox_path(void)
{
	int i;

	for (i = 0; firefox_candidates[i]; i++)
		if (!access(firefox_candidates[i], X_OK) &&
		    !is_snap_wrapper(firefox_candidates[i]))
			return firefox_candidates[i];
	return NULL;
}

static void install_firefox(void)
{
	app_terminal_cmd("sudo apt update && sudo apt install -y firefox");
}

void app_firefox(void)
{
	const char *ff = firefox_path();

	if (!x11_available()) {
		wc_alert("X11 support is missing",
			 "Programs like Firefox need the X11 bridge. Install Xvfb "
			 "(sudo apt install xvfb) and a Water Candy built with "
			 "libx11-dev and libxtst-dev.", NULL, NULL);
		return;
	}
	if (!ff) {
		wc_alert("Firefox is not installed",
			 "Water Candy can download it with APT. Click Install to "
			 "open Terminal and run: sudo apt install firefox",
			 "Install", install_firefox);
		return;
	}
	app_x11(ff, "Firefox", APP_FIREFOX);
}

#ifdef HAVE_X11

#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>

#define XVFB "/usr/bin/Xvfb"

struct xapp {
	int disp;
	pid_t xvfb, app;
	Display *dpy;
	int w, h;
	char dir[64], cmd[512];
	int state;		/* 0: waiting for Xvfb, 1: running */
	double t0, last_frame;
	uint8_t *map;
	size_t map_size;
	size_t pix_off;
	uint32_t bpl;
	Window main_win;
};

int x11_available(void)
{
	return !access(XVFB, X_OK);
}

static int ignore_x_errors(Display *d, XErrorEvent *e)
{
	(void)d;
	(void)e;
	return 0;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

/* Xvfb -fbdir writes the screen as an XWD image; map it. */
static int map_framebuffer(struct xapp *x)
{
	char path[96];
	struct stat st;
	int fd;

	snprintf(path, sizeof(path), "%s/Xvfb_screen0", x->dir);
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) || st.st_size < 100) {
		close(fd);
		return -1;
	}
	x->map = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if (x->map == MAP_FAILED) {
		x->map = NULL;
		return -1;
	}
	x->map_size = st.st_size;
	x->bpl = be32(x->map + 48);
	x->pix_off = be32(x->map + 0) + (size_t)be32(x->map + 76) * 12;
	if (be32(x->map + 44) != 32 ||
	    x->pix_off + (size_t)x->bpl * x->h > x->map_size) {
		munmap(x->map, x->map_size);
		x->map = NULL;
		return -1;
	}
	return 0;
}

static void update_app_name(window_t *win, struct xapp *x)
{
	XClassHint hint = { 0 };

	if (!x->main_win || !XGetClassHint(x->dpy, x->main_win, &hint))
		return;
	if (hint.res_class && hint.res_class[0]) {
		snprintf(win->app_name, sizeof(win->app_name), "%s", hint.res_class);
		win->app_name[0] = (char)toupper((unsigned char)win->app_name[0]);
		wm_set_title(win, win->title);	/* refreshes the menu bar */
	}
	if (hint.res_name)
		XFree(hint.res_name);
	if (hint.res_class)
		XFree(hint.res_class);
}

static void update_title(window_t *win, struct xapp *x)
{
	Atom net_name = XInternAtom(x->dpy, "_NET_WM_NAME", False);
	Atom utf8 = XInternAtom(x->dpy, "UTF8_STRING", False);
	Atom type;
	int fmt;
	unsigned long n, after;
	unsigned char *prop = NULL;
	char *name = NULL;

	if (!x->main_win)
		return;
	if (XGetWindowProperty(x->dpy, x->main_win, net_name, 0, 256, False, utf8,
			       &type, &fmt, &n, &after, &prop) == Success && prop &&
	    n) {
		wm_set_title(win, (char *)prop);
	} else if (XFetchName(x->dpy, x->main_win, &name) && name) {
		wm_set_title(win, name);
	}
	if (prop)
		XFree(prop);
	if (name)
		XFree(name);
}

static void manage(struct xapp *x, Window w, int map)
{
	Window transient = 0;
	XWindowAttributes wa;

	if (!XGetWindowAttributes(x->dpy, w, &wa) || wa.override_redirect)
		return;
	if (XGetTransientForHint(x->dpy, w, &transient) && transient) {
		/* Dialogs keep their size and are centered. */
		XMoveWindow(x->dpy, w, (x->w - wa.width) / 2, (x->h - wa.height) / 3);
	} else {
		XMoveResizeWindow(x->dpy, w, 0, 0, x->w, x->h);
		XSetWindowBorderWidth(x->dpy, w, 0);
		x->main_win = w;
		XSelectInput(x->dpy, w, PropertyChangeMask | StructureNotifyMask);
	}
	if (map)
		XMapRaised(x->dpy, w);
	XSetInputFocus(x->dpy, w, RevertToPointerRoot, CurrentTime);
}

static void start_program(struct xapp *x)
{
	char display[16];

	snprintf(display, sizeof(display), ":%d", x->disp);
	x->app = fork();
	if (x->app == 0) {
		int fd;

		setsid();
		user_become();
		setenv("DISPLAY", display, 1);
		unsetenv("WAYLAND_DISPLAY");
		/* No GPU in the virtual X server: let toolkits use software paths. */
		setenv("MOZ_ENABLE_WAYLAND", "0", 1);
		setenv("GDK_BACKEND", "x11", 1);
		setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
		}
		execl("/bin/sh", "sh", "-c", x->cmd, (char *)NULL);
		_exit(127);
	}
}

static void xapp_draw(window_t *win, surface_t *s, int x0, int y0, int w, int h)
{
	struct xapp *x = win->data;
	int y, i;

	if (!x->map) {
		fill_rect(s, x0, y0, w, h, 0xffffffff);
		draw_text_center(s, fonts[FONT_UI], 13, x0, w, y0 + h / 2,
				 0xff707070, x->state ? "Starting\xe2\x80\xa6" :
				 "Starting the X server\xe2\x80\xa6");
		return;
	}
	for (y = 0; y < h && y < x->h; y++) {
		const uint32_t *src = (const uint32_t *)(x->map + x->pix_off +
							 (size_t)y * x->bpl);
		uint32_t *dst = s->px + (size_t)(y0 + y) * s->w + x0;

		for (i = 0; i < w && i < x->w; i++)
			dst[i] = src[i] | 0xff000000u;
	}
}

static void xapp_fail(window_t *win, const char *why)
{
	win->tick = NULL;
	wm_close(win);
	wc_alert("Could not start the program", why, NULL, NULL);
}

static void xapp_tick(window_t *win)
{
	struct xapp *x = win->data;
	char sock[64];

	if (x->state == 0) {
		snprintf(sock, sizeof(sock), "/tmp/.X11-unix/X%d", x->disp);
		if (access(sock, F_OK)) {
			if (dt.now - x->t0 > 20)
				xapp_fail(win, "The virtual X server (Xvfb) did not start.");
			return;
		}
		snprintf(sock, sizeof(sock), ":%d", x->disp);
		x->dpy = XOpenDisplay(sock);
		if (!x->dpy)
			return;
		XSetErrorHandler(ignore_x_errors);
		/* Become the window manager of this X server. */
		XSelectInput(x->dpy, DefaultRootWindow(x->dpy),
			     SubstructureRedirectMask | SubstructureNotifyMask);
		/* An arrow instead of the X server's default "X" pointer. */
		XDefineCursor(x->dpy, DefaultRootWindow(x->dpy),
			      XCreateFontCursor(x->dpy, XC_left_ptr));
		XSync(x->dpy, False);
		map_framebuffer(x);
		start_program(x);
		x->state = 1;
		wm_damage(win);
		return;
	}

	while (XPending(x->dpy)) {
		XEvent ev;

		XNextEvent(x->dpy, &ev);
		switch (ev.type) {
		case MapRequest:
			manage(x, ev.xmaprequest.window, 1);
			update_title(win, x);
			update_app_name(win, x);
			break;
		case ConfigureRequest: {
			XConfigureRequestEvent *r = &ev.xconfigurerequest;
			XWindowChanges wc = {
				.x = r->x, .y = r->y, .width = r->width,
				.height = r->height, .border_width = r->border_width,
				.sibling = r->above, .stack_mode = r->detail
			};

			if (r->window == x->main_win) {
				wc.x = 0;
				wc.y = 0;
				wc.width = x->w;
				wc.height = x->h;
				r->value_mask |= CWX | CWY | CWWidth | CWHeight;
			}
			XConfigureWindow(x->dpy, r->window, r->value_mask, &wc);
			break;
		}
		case PropertyNotify:
			if (ev.xproperty.window == x->main_win)
				update_title(win, x);
			break;
		case DestroyNotify:
			if (ev.xdestroywindow.window == x->main_win)
				x->main_win = 0;
			break;
		}
	}
	XFlush(x->dpy);

	if (!x->map)
		map_framebuffer(x);
	if (x->app > 0 && kill(x->app, 0) && errno == ESRCH) {
		/* The program quit; give it a moment in case it re-executes. */
		x->app = 0;
		win->tick = NULL;
		wm_close(win);
		return;
	}
	if (dt.now - x->last_frame > 0.05) {
		x->last_frame = dt.now;
		wm_damage(win);
	}
}

static void xapp_mouse(window_t *win, const event_t *ev, int mx, int my)
{
	struct xapp *x = win->data;
	static const unsigned buttons[] = { 0, 1, 3, 2 };

	if (!x->dpy || x->state != 1)
		return;
	if (mx < 0)
		mx = 0;
	if (my < 0)
		my = 0;
	if (mx >= x->w)
		mx = x->w - 1;
	if (my >= x->h)
		my = x->h - 1;
	XTestFakeMotionEvent(x->dpy, -1, mx, my, CurrentTime);
	if (ev->type == EV_BUTTON && ev->button >= 1 && ev->button <= 3) {
		XTestFakeButtonEvent(x->dpy, buttons[ev->button], ev->pressed,
				     CurrentTime);
	} else if (ev->type == EV_SCROLL && ev->dz) {
		unsigned b = ev->dz > 0 ? 4 : 5;
		int n = ev->dz > 0 ? ev->dz : -ev->dz;

		while (n--) {
			XTestFakeButtonEvent(x->dpy, b, True, CurrentTime);
			XTestFakeButtonEvent(x->dpy, b, False, CurrentTime);
		}
	}
	XFlush(x->dpy);
}

static void xapp_rawkey(window_t *win, const event_t *ev)
{
	struct xapp *x = win->data;

	if (!x->dpy || x->state != 1)
		return;
	/* Xvfb uses the evdev keymap: X keycode = Linux keycode + 8. */
	if (ev->pressed == 2)
		XTestFakeKeyEvent(x->dpy, ev->key + 8, True, CurrentTime);
	else
		XTestFakeKeyEvent(x->dpy, ev->key + 8, ev->pressed, CurrentTime);
	XFlush(x->dpy);
}

static void xapp_destroy(window_t *win)
{
	struct xapp *x = win->data;
	char path[96];

	if (x->app > 0)
		kill(-x->app, SIGTERM);
	if (x->dpy)
		XCloseDisplay(x->dpy);
	if (x->xvfb > 0)
		kill(x->xvfb, SIGTERM);
	if (x->map)
		munmap(x->map, x->map_size);
	snprintf(path, sizeof(path), "%s/Xvfb_screen0", x->dir);
	unlink(path);
	rmdir(x->dir);
	free(x);
}

static int free_display(void)
{
	char path[64];
	int n;

	for (n = 10; n < 100; n++) {
		snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", n);
		if (!access(path, F_OK))
			continue;
		snprintf(path, sizeof(path), "/tmp/.X%d-lock", n);
		if (!access(path, F_OK))
			continue;
		return n;
	}
	return -1;
}

int app_x11(const char *cmd, const char *title, enum app_id app)
{
	struct xapp *x;
	window_t *win;
	char display[16], screen[32];
	int cw, ch;

	if (!x11_available()) {
		wc_alert("X11 support is missing",
			 "Install the virtual X server with: sudo apt install xvfb",
			 NULL, NULL);
		return -1;
	}
	x = calloc(1, sizeof(*x));
	x->disp = free_display();
	if (x->disp < 0) {
		free(x);
		return -1;
	}
	snprintf(x->cmd, sizeof(x->cmd), "exec %s", cmd);
	/* Leave room for the Dock and the window cascade. */
	cw = dt.w - 120 > 1100 ? 1100 : dt.w - 120;
	ch = dt.h - MENUBAR_H - 230 > 760 ? 760 : dt.h - MENUBAR_H - 230;
	x->w = cw & ~7;
	x->h = ch;
	snprintf(x->dir, sizeof(x->dir), "/tmp/watercandy-x%d", x->disp);
	mkdir(x->dir, 0700);
	mkdir("/tmp/.X11-unix", 01777);
	chmod("/tmp/.X11-unix", 01777);

	snprintf(display, sizeof(display), ":%d", x->disp);
	snprintf(screen, sizeof(screen), "%dx%dx24", x->w, x->h);
	x->xvfb = fork();
	if (x->xvfb == 0) {
		int fd = open("/dev/null", O_RDWR);

		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
		}
		setsid();
		execl(XVFB, "Xvfb", display, "-screen", "0", screen, "-fbdir",
		      x->dir, "-nolisten", "tcp", "-ac", "-noreset",
		      (char *)NULL);
		_exit(127);
	}
	x->t0 = now_s();

	win = wm_create(title, app, x->w + 2, x->h + TITLE_H + 1);
	win->data = x;
	win->draw = xapp_draw;
	win->tick = xapp_tick;
	win->mouse = xapp_mouse;
	win->rawkey = xapp_rawkey;
	win->own_cursor = 1;
	win->destroy = xapp_destroy;
	return 0;
}

#else /* !HAVE_X11 */

int x11_available(void)
{
	return 0;
}

int app_x11(const char *cmd, const char *title, enum app_id app)
{
	(void)cmd;
	(void)title;
	(void)app;
	wc_alert("X11 support is missing",
		 "This Water Candy was built without the X11 bridge.", NULL, NULL);
	return -1;
}

#endif
