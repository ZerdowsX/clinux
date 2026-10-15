/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WC_INPUT_H
#define WC_INPUT_H

/*
 * EV_KEYRAW reports every key press (pressed=1), repeat (2) and release (0),
 * including modifiers; EV_KEYPRESS is the translated, press-only form.
 */
enum ev_type { EV_MOTION, EV_BUTTON, EV_KEYPRESS, EV_KEYRAW, EV_SCROLL,
	       EV_LAYOUT };

/* Keyboard layouts; Alt+Shift switches between them. */
enum { LAYOUT_US, LAYOUT_RU, LAYOUT_COUNT };

#define MOD_SHIFT 1
#define MOD_CTRL  2
#define MOD_ALT   4

typedef struct event {
	enum ev_type type;
	int x, y;		/* pointer position */
	int button;		/* 1 = left, 2 = right, 3 = middle */
	int pressed;
	int key;		/* Linux KEY_* code */
	unsigned ch;		/* translated character, 0 if none */
	int mods;
	int dz;			/* wheel */
} event_t;

int input_open(int w, int h);
/* Start reading devices in the background (after input_open). */
void input_start(void);
/* Fill pollfd array; returns count. */
int input_pollfds(void *pfds, int max);
/* Read pending input; invokes cb for each event. */
void input_dispatch(void (*cb)(const event_t *ev));
void input_get_pointer(int *x, int *y);
void input_set_pointer(int x, int y);
int input_layout(void);
void input_set_layout(int layout);

#endif
