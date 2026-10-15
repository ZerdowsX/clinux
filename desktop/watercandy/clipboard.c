// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - the clipboard shared by the desktop's own applications and
 * every X11 program (each in its own X server; see xapp.c).
 */
#include <stdlib.h>
#include <string.h>

#include "wc.h"

static char *clip;
static int clip_source;
static unsigned clip_serial;

void clip_set(const char *text, int source)
{
	if (!text)
		return;
	if (clip && !strcmp(clip, text))
		return;
	free(clip);
	clip = strdup(text);
	clip_source = source;
	clip_serial++;
}

const char *clip_get(void)
{
	return clip ? clip : "";
}

unsigned clip_version(void)
{
	return clip_serial;
}

int clip_owner(void)
{
	return clip_source;
}
