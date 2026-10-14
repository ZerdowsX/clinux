# SPDX-License-Identifier: GPL-2.0
# Water Candy - Aqua-style desktop shell drawing straight to DRM/KMS.

CC	?= gcc
CFLAGS	?= -O2 -g
CFLAGS	+= -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_GNU_SOURCE
LDLIBS	+= -lm

SRCS	:= main.c gfx.c font.c display.c input.c wallpaper.c icons.c aqua.c \
	   wm.c apps.c terminal.c files.c
OBJS	:= $(SRCS:.c=.o)

all: watercandy

watercandy: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Statically linked build for an initramfs without a C library.
static:
	$(MAKE) LDFLAGS="$(LDFLAGS) -static" watercandy

$(OBJS): gfx.h font.h input.h display.h wc.h

clean:
	rm -f watercandy $(OBJS)

.PHONY: all static clean
