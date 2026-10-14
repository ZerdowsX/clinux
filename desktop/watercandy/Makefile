# SPDX-License-Identifier: GPL-2.0
# Water Candy - Aqua-style desktop shell drawing straight to DRM/KMS.

CC	?= gcc
CFLAGS	?= -O2 -g
CFLAGS	+= -Wall -Wextra -Wno-unused-parameter -std=gnu11 -D_GNU_SOURCE
LDLIBS	+= -lm

SRCS	:= main.c gfx.c font.c display.c input.c wallpaper.c icons.c aqua.c \
	   wm.c apps.c terminal.c files.c user.c net.c xapp.c
OBJS	:= $(SRCS:.c=.o)

# The X11 bridge (Firefox and other X programs) needs libX11 and libXtst.
# Build with NO_X11=1 to leave it out, e.g. for a static initramfs build.
ifeq ($(NO_X11),)
X11_LIBS := $(shell pkg-config --libs x11 xtst 2>/dev/null)
ifneq ($(X11_LIBS),)
CFLAGS	+= -DHAVE_X11 $(shell pkg-config --cflags x11 xtst)
LDLIBS	+= $(X11_LIBS)
endif
endif

all: watercandy

watercandy: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

# Statically linked build for an initramfs without a C library.
static:
	$(MAKE) NO_X11=1 LDFLAGS="$(LDFLAGS) -static" watercandy

install: watercandy
	install -D -m 755 watercandy $(DESTDIR)/usr/local/bin/watercandy

$(OBJS): gfx.h font.h input.h display.h wc.h

clean:
	rm -f watercandy $(OBJS)

.PHONY: all static install clean
