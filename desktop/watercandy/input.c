// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - evdev input: mice, tablets and keyboards.
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>

#include "input.h"

#define MAX_DEV 16
#define BITS_LONG (8 * sizeof(long))
#define TEST_BIT(b, a) (((a)[(b) / BITS_LONG] >> ((b) % BITS_LONG)) & 1)

struct dev {
	int fd;
	int abs_min[2], abs_max[2];
};

static struct dev devs[MAX_DEV];
static int ndevs;
static int scr_w, scr_h, ptr_x, ptr_y, mods;
static int wake[2] = { -1, -1 };	/* reader thread -> main loop */

/* US layout: plain and shifted characters for KEY_* codes. */
static const char keymap[2][KEY_SPACE + 1] = {
	{ 0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
	  '\b', '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[',
	  ']', '\r', 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
	  '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.',
	  '/', 0, '*', 0, ' ' },
	{ 0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',
	  '\b', '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{',
	  '}', '\r', 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
	  '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>',
	  '?', 0, '*', 0, ' ' },
};

/* Russian (JCUKEN) layout, as Unicode code points; 0 = same as US. */
static const unsigned short keymap_ru[2][KEY_SPACE + 1] = {
	[0] = {
		[KEY_GRAVE] = 0x451,
		[KEY_Q] = 0x439, [KEY_W] = 0x446, [KEY_E] = 0x443, [KEY_R] = 0x43a,
		[KEY_T] = 0x435, [KEY_Y] = 0x43d, [KEY_U] = 0x433, [KEY_I] = 0x448,
		[KEY_O] = 0x449, [KEY_P] = 0x437, [KEY_LEFTBRACE] = 0x445,
		[KEY_RIGHTBRACE] = 0x44a,
		[KEY_A] = 0x444, [KEY_S] = 0x44b, [KEY_D] = 0x432, [KEY_F] = 0x430,
		[KEY_G] = 0x43f, [KEY_H] = 0x440, [KEY_J] = 0x43e, [KEY_K] = 0x43b,
		[KEY_L] = 0x434, [KEY_SEMICOLON] = 0x436, [KEY_APOSTROPHE] = 0x44d,
		[KEY_Z] = 0x44f, [KEY_X] = 0x447, [KEY_C] = 0x441, [KEY_V] = 0x43c,
		[KEY_B] = 0x438, [KEY_N] = 0x442, [KEY_M] = 0x44c,
		[KEY_COMMA] = 0x431, [KEY_DOT] = 0x44e, [KEY_SLASH] = '.',
	},
	[1] = {
		[KEY_GRAVE] = 0x401, [KEY_2] = '"', [KEY_3] = 0x2116, [KEY_4] = ';',
		[KEY_6] = ':', [KEY_7] = '?',
		[KEY_Q] = 0x419, [KEY_W] = 0x426, [KEY_E] = 0x423, [KEY_R] = 0x41a,
		[KEY_T] = 0x415, [KEY_Y] = 0x41d, [KEY_U] = 0x413, [KEY_I] = 0x428,
		[KEY_O] = 0x429, [KEY_P] = 0x417, [KEY_LEFTBRACE] = 0x425,
		[KEY_RIGHTBRACE] = 0x42a,
		[KEY_A] = 0x424, [KEY_S] = 0x42b, [KEY_D] = 0x412, [KEY_F] = 0x410,
		[KEY_G] = 0x41f, [KEY_H] = 0x420, [KEY_J] = 0x41e, [KEY_K] = 0x41b,
		[KEY_L] = 0x414, [KEY_SEMICOLON] = 0x416, [KEY_APOSTROPHE] = 0x42d,
		[KEY_BACKSLASH] = '/',
		[KEY_Z] = 0x42f, [KEY_X] = 0x427, [KEY_C] = 0x421, [KEY_V] = 0x41c,
		[KEY_B] = 0x418, [KEY_N] = 0x422, [KEY_M] = 0x42c,
		[KEY_COMMA] = 0x411, [KEY_DOT] = 0x42e, [KEY_SLASH] = ',',
	},
};

static int layout;
/* Alt+Shift pressed together and released with no other key between. */
static int switch_armed;

int input_layout(void)
{
	return layout;
}

void input_set_layout(int l)
{
	layout = l % LAYOUT_COUNT;
}

int input_open(int w, int h)
{
	DIR *dir = opendir("/dev/input");
	struct dirent *de;

	scr_w = w;
	scr_h = h;
	ptr_x = w / 2;
	ptr_y = h / 2;
	if (!dir)
		return -1;
	while ((de = readdir(dir)) && ndevs < MAX_DEV) {
		unsigned long evbits[(EV_MAX + BITS_LONG) / BITS_LONG] = { 0 };
		struct dev *d = &devs[ndevs];
		char path[300];
		int a;

		if (strncmp(de->d_name, "event", 5))
			continue;
		snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
		d->fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (d->fd < 0)
			continue;
		ioctl(d->fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
		if (!TEST_BIT(EV_KEY, evbits) && !TEST_BIT(EV_REL, evbits) &&
		    !TEST_BIT(EV_ABS, evbits)) {
			close(d->fd);
			continue;
		}
		for (a = 0; a < 2; a++) {
			struct input_absinfo ai = { 0 };

			if (TEST_BIT(EV_ABS, evbits) &&
			    !ioctl(d->fd, EVIOCGABS(a ? ABS_Y : ABS_X), &ai)) {
				d->abs_min[a] = ai.minimum;
				d->abs_max[a] = ai.maximum;
			}
		}
		ndevs++;
	}
	closedir(dir);
	return ndevs ? 0 : -1;
}

int input_pollfds(void *p, int max)
{
	struct pollfd *pfds = p;

	if (wake[0] < 0 || max < 1)
		return 0;
	pfds[0].fd = wake[0];
	pfds[0].events = POLLIN;
	return 1;
}

void input_get_pointer(int *x, int *y)
{
	*x = ptr_x;
	*y = ptr_y;
}

void input_set_pointer(int x, int y)
{
	ptr_x = x;
	ptr_y = y;
}

static void clamp_ptr(void)
{
	if (ptr_x < 0)
		ptr_x = 0;
	if (ptr_y < 0)
		ptr_y = 0;
	if (ptr_x >= scr_w)
		ptr_x = scr_w - 1;
	if (ptr_y >= scr_h)
		ptr_y = scr_h - 1;
}

static void key_event(int code, int value, void (*cb)(const event_t *))
{
	event_t ev = { .x = ptr_x, .y = ptr_y };
	int bit = 0;

	if (code < BTN_MISC || code >= KEY_OK) {
		event_t raw = { .type = EV_KEYRAW, .x = ptr_x, .y = ptr_y,
				.key = code, .pressed = value, .mods = mods };

		cb(&raw);
	}

	switch (code) {
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
		bit = MOD_SHIFT;
		break;
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
		bit = MOD_CTRL;
		break;
	case KEY_LEFTALT: case KEY_RIGHTALT: case KEY_LEFTMETA: case KEY_RIGHTMETA:
		bit = MOD_ALT;
		break;
	}
	if (bit) {
		if (value == 1) {
			mods |= bit;
			if ((mods & (MOD_ALT | MOD_SHIFT)) == (MOD_ALT | MOD_SHIFT) &&
			    !(mods & MOD_CTRL))
				switch_armed = 1;
		} else if (value == 0) {
			if (switch_armed && (bit == MOD_ALT || bit == MOD_SHIFT)) {
				event_t sw = { .type = EV_LAYOUT, .x = ptr_x, .y = ptr_y };

				layout = (layout + 1) % LAYOUT_COUNT;
				cb(&sw);
			}
			switch_armed = 0;
			mods &= ~bit;
		}
		return;
	}
	if (value == 1)
		switch_armed = 0;

	if (code == BTN_LEFT || code == BTN_RIGHT || code == BTN_MIDDLE ||
	    code == BTN_TOUCH) {
		if (value == 2)
			return;
		ev.type = EV_BUTTON;
		ev.button = code == BTN_RIGHT ? 2 : code == BTN_MIDDLE ? 3 : 1;
		ev.pressed = value;
		ev.mods = mods;
		cb(&ev);
		return;
	}
	if (!value)
		return;		/* key releases are not needed */
	ev.type = EV_KEYPRESS;
	ev.key = code;
	ev.mods = mods;
	if (code <= KEY_SPACE) {
		int shift = (mods & MOD_SHIFT) ? 1 : 0;

		ev.ch = (unsigned char)keymap[shift][code];
		/* Shortcuts (Ctrl, Alt) always use the Latin letters. */
		if (layout == LAYOUT_RU && !(mods & (MOD_CTRL | MOD_ALT)) &&
		    keymap_ru[shift][code])
			ev.ch = keymap_ru[shift][code];
	}
	if (code == KEY_KPENTER)
		ev.ch = '\r';
	if ((mods & MOD_CTRL) && ev.ch >= 'a' && ev.ch <= 'z')
		ev.ch = ev.ch - 'a' + 1;
	else if ((mods & MOD_CTRL) && ev.ch >= 'A' && ev.ch <= 'Z')
		ev.ch = ev.ch - 'A' + 1;
	cb(&ev);
}

/*
 * A reader thread drains the devices into a large queue as soon as events
 * arrive, so nothing is lost while the desktop is busy drawing (the
 * kernel's per-device buffer only holds a few dozen events).
 */
#define QUEUE_LEN 8192

struct queued {
	int dev;
	struct input_event e;
};

static struct queued queue[QUEUE_LEN];
static unsigned q_head, q_tail;
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

static void *reader(void *arg)
{
	struct pollfd pfds[MAX_DEV];
	int i;

	(void)arg;
	for (i = 0; i < ndevs; i++) {
		pfds[i].fd = devs[i].fd;
		pfds[i].events = POLLIN;
	}
	for (;;) {
		if (poll(pfds, ndevs, -1) < 0)
			continue;
		for (i = 0; i < ndevs; i++) {
			struct input_event ie[64];
			ssize_t n;
			int k;

			if (!(pfds[i].revents & POLLIN))
				continue;
			while ((n = read(devs[i].fd, ie, sizeof(ie))) > 0) {
				pthread_mutex_lock(&q_lock);
				for (k = 0; k < (int)(n / sizeof(ie[0])); k++) {
					if (q_tail - q_head == QUEUE_LEN)
						q_head++;	/* drop the oldest */
					queue[q_tail % QUEUE_LEN].dev = i;
					queue[q_tail % QUEUE_LEN].e = ie[k];
					q_tail++;
				}
				pthread_mutex_unlock(&q_lock);
			}
		}
		if (write(wake[1], "", 1) < 0) {
			/* the pipe is full: the main loop is awake anyway */
		}
	}
	return NULL;
}

void input_start(void)
{
	pthread_t t;

	if (pipe2(wake, O_CLOEXEC | O_NONBLOCK))
		return;
	pthread_create(&t, NULL, reader, NULL);
	pthread_detach(t);
}

static void handle(struct dev *d, const struct input_event *e, int *moved,
		   void (*cb)(const event_t *ev))
{
	if (e->type == EV_REL && e->code == REL_X) {
		ptr_x += e->value;
		*moved = 1;
	} else if (e->type == EV_REL && e->code == REL_Y) {
		ptr_y += e->value;
		*moved = 1;
	} else if (e->type == EV_REL && e->code == REL_WHEEL) {
		event_t ev = { .type = EV_SCROLL, .x = ptr_x, .y = ptr_y, .dz = e->value };

		cb(&ev);
	} else if (e->type == EV_ABS && (e->code == ABS_X || e->code == ABS_Y)) {
		int a = e->code == ABS_Y;
		int range = d->abs_max[a] - d->abs_min[a];
		int span = a ? scr_h : scr_w;

		if (range > 0) {
			int v = (int)((long long)(e->value - d->abs_min[a]) * (span - 1) / range);

			if (a)
				ptr_y = v;
			else
				ptr_x = v;
			*moved = 1;
		}
	} else if (e->type == EV_KEY || (e->type == EV_SYN && *moved)) {
		if (*moved) {
			event_t ev = { .type = EV_MOTION };

			clamp_ptr();
			ev.x = ptr_x;
			ev.y = ptr_y;
			cb(&ev);
			*moved = 0;
		}
		if (e->type == EV_KEY)
			key_event(e->code, e->value, cb);
	}
}

void input_dispatch(void (*cb)(const event_t *ev))
{
	static struct queued batch[QUEUE_LEN];
	char drain[64];
	unsigned n = 0, i;
	int moved = 0;

	while (read(wake[0], drain, sizeof(drain)) > 0)
		;
	pthread_mutex_lock(&q_lock);
	while (q_head != q_tail)
		batch[n++] = queue[q_head++ % QUEUE_LEN];
	pthread_mutex_unlock(&q_lock);
	for (i = 0; i < n; i++)
		handle(&devs[batch[i].dev], &batch[i].e, &moved, cb);
}
