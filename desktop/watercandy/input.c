// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - evdev input: mice, tablets and keyboards.
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
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
	int i;

	for (i = 0; i < ndevs && i < max; i++) {
		pfds[i].fd = devs[i].fd;
		pfds[i].events = POLLIN;
	}
	return i;
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
		if (value)
			mods |= bit;
		else
			mods &= ~bit;
		return;
	}

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
	if (code <= KEY_SPACE)
		ev.ch = (unsigned char)keymap[(mods & MOD_SHIFT) ? 1 : 0][code];
	if (code == KEY_KPENTER)
		ev.ch = '\r';
	if ((mods & MOD_CTRL) && ev.ch >= 'a' && ev.ch <= 'z')
		ev.ch = ev.ch - 'a' + 1;
	else if ((mods & MOD_CTRL) && ev.ch >= 'A' && ev.ch <= 'Z')
		ev.ch = ev.ch - 'A' + 1;
	cb(&ev);
}

void input_dispatch(void (*cb)(const event_t *ev))
{
	int i;

	for (i = 0; i < ndevs; i++) {
		struct input_event ie[64];
		ssize_t n;
		int moved = 0, k;

		while ((n = read(devs[i].fd, ie, sizeof(ie))) > 0) {
			for (k = 0; k < (int)(n / sizeof(ie[0])); k++) {
				struct input_event *e = &ie[k];
				struct dev *d = &devs[i];

				if (e->type == EV_REL && e->code == REL_X) {
					ptr_x += e->value;
					moved = 1;
				} else if (e->type == EV_REL && e->code == REL_Y) {
					ptr_y += e->value;
					moved = 1;
				} else if (e->type == EV_REL && e->code == REL_WHEEL) {
					event_t ev = { .type = EV_SCROLL, .x = ptr_x,
						       .y = ptr_y, .dz = e->value };
					cb(&ev);
				} else if (e->type == EV_ABS &&
					   (e->code == ABS_X || e->code == ABS_Y)) {
					int a = e->code == ABS_Y;
					int range = d->abs_max[a] - d->abs_min[a];
					int span = a ? scr_h : scr_w;

					if (range > 0) {
						int v = (int)((long long)(e->value - d->abs_min[a]) *
							      (span - 1) / range);
						if (a)
							ptr_y = v;
						else
							ptr_x = v;
						moved = 1;
					}
				} else if (e->type == EV_KEY) {
					if (moved) {
						event_t ev = { .type = EV_MOTION };

						clamp_ptr();
						ev.x = ptr_x;
						ev.y = ptr_y;
						cb(&ev);
						moved = 0;
					}
					key_event(e->code, e->value, cb);
				} else if (e->type == EV_SYN && moved) {
					event_t ev = { .type = EV_MOTION };

					clamp_ptr();
					ev.x = ptr_x;
					ev.y = ptr_y;
					cb(&ev);
					moved = 0;
				}
			}
		}
	}
}
