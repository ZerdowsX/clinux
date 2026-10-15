// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - the installer, shown when running from the live ISO. It
 * picks a disk and runs wc-install, showing its progress.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wc.h"

#define INST_W 560
#define INST_H 440
#define LIST_Y 120
#define DISK_H 40
#define MAX_DISKS 8

enum { ST_CHOOSE, ST_RUNNING, ST_DONE, ST_FAILED };

struct disk {
	char dev[64], model[96];
	unsigned long long bytes;
	int removable;
};

static struct {
	window_t *win;
	struct disk disks[MAX_DISKS];
	int ndisks, sel, agree, state, pressed;
	float progress;
	char step[160], error[256];
} I;

int live_system(void)
{
	return !access("/etc/clinux-live", F_OK);
}

static void read_line(const char *path, char *out, size_t sz)
{
	FILE *f = fopen(path, "r");

	out[0] = 0;
	if (!f)
		return;
	if (fgets(out, sz, f))
		out[strcspn(out, "\n")] = 0;
	fclose(f);
	while (*out && out[strlen(out) - 1] == ' ')
		out[strlen(out) - 1] = 0;
}

/* The disk the live system itself was started from. */
static int is_live_medium(const char *name)
{
	FILE *f = fopen("/proc/mounts", "r");
	char dev[128], mnt[256];
	int found = 0;

	if (!f)
		return 0;
	while (fscanf(f, "%127s %255s %*[^\n]", dev, mnt) == 2)
		if (!strcmp(mnt, "/live/medium") && strstr(dev, name))
			found = 1;
	fclose(f);
	return found;
}

static void scan_disks(void)
{
	DIR *d = opendir("/sys/block");
	struct dirent *de;

	I.ndisks = 0;
	while (d && (de = readdir(d)) && I.ndisks < MAX_DISKS) {
		char path[512], buf[128];
		struct disk *k = &I.disks[I.ndisks];

		if (de->d_name[0] == '.' || !strncmp(de->d_name, "loop", 4) ||
		    !strncmp(de->d_name, "ram", 3) || !strncmp(de->d_name, "sr", 2) ||
		    !strncmp(de->d_name, "zram", 4) || !strncmp(de->d_name, "dm-", 3) ||
		    is_live_medium(de->d_name))
			continue;
		snprintf(path, sizeof(path), "/sys/block/%s/size", de->d_name);
		read_line(path, buf, sizeof(buf));
		k->bytes = strtoull(buf, NULL, 10) * 512;
		if (k->bytes < (4ULL << 30))
			continue;	/* too small for the system */
		snprintf(k->dev, sizeof(k->dev), "/dev/%.58s", de->d_name);
		snprintf(path, sizeof(path), "/sys/block/%s/device/model", de->d_name);
		read_line(path, k->model, sizeof(k->model));
		snprintf(path, sizeof(path), "/sys/block/%s/removable", de->d_name);
		read_line(path, buf, sizeof(buf));
		k->removable = buf[0] == '1';
		I.ndisks++;
	}
	if (d)
		closedir(d);
	I.sel = I.ndisks == 1 ? 0 : -1;
}

static const char *translate_step(const char *s)
{
	static char buf[160];

	if (!strncmp(s, "Partitioning ", 13)) {
		snprintf(buf, sizeof(buf), "%s %s", _("Partitioning"), s + 13);
		return buf;
	}
	return _(s);
}

static void inst_status(const char *line, void *ctx)
{
	int pct;
	char text[160];

	(void)ctx;
	if (sscanf(line, "STEP %d %159[^\n]", &pct, text) == 2) {
		I.progress = pct / 100.0f;
		snprintf(I.step, sizeof(I.step), "%s", translate_step(text));
	} else if (!strncmp(line, "ERROR ", 6)) {
		snprintf(I.error, sizeof(I.error), "%s", line + 6);
	}
	if (I.win)
		wm_damage(I.win);
}

static void inst_done(int status, const char *out, void *ctx)
{
	(void)out;
	(void)ctx;
	I.state = status ? ST_FAILED : ST_DONE;
	if (status && !I.error[0])
		snprintf(I.error, sizeof(I.error), "wc-install exited with %d", status);
	if (I.win)
		wm_damage(I.win);
}

static void start_install(void)
{
	const char *argv[] = { "/usr/local/sbin/wc-install", I.disks[I.sel].dev, NULL };

	I.state = ST_RUNNING;
	I.progress = 0;
	I.error[0] = 0;
	snprintf(I.step, sizeof(I.step), "%s", _("Starting\xe2\x80\xa6"));
	if (proc_run(argv, PROC_STATUS_FD, inst_done, inst_status, NULL) < 0) {
		I.state = ST_FAILED;
		snprintf(I.error, sizeof(I.error), "%s", _("Could not start the installer"));
	}
}

static void progress_bar(surface_t *s, int x, int y, int w, float v)
{
	paint_t p = paint_vgrad(y, y + 14, 0xffc8c8c8, 0xffeeeeee);
	int fw = (int)((w - 2) * (v < 0 ? 0 : v > 1 ? 1 : v));

	fill_rrect(s, x, y, w, 14, 7, &p);
	if (fw > 12) {
		p = paint_vgrad(y, y + 14, 0xff8ec0f8, 0xff2a6ad8);
		fill_rrect(s, x + 1, y + 1, fw, 12, 6, &p);
		p = paint_vgrad(y + 1, y + 7, 0xa0ffffff, 0x10ffffff);
		fill_rrect(s, x + 2, y + 1.5f, fw - 2, 5, 2.5f, &p);
	}
	p = paint_solid(0xff7a7a7a);
	stroke_rrect(s, x, y, w, 14, 7, 1, &p);
}

static void format_size(char *buf, size_t sz, unsigned long long b)
{
	double gb = b / 1e9;

	snprintf(buf, sz, gb >= 1000 ? "%.1f %s" : "%.0f %s", gb >= 1000 ? gb / 1000 : gb,
		 gb >= 1000 ? _("TB") : _("GB"));
}

static void inst_draw(window_t *win, surface_t *s, int x, int y, int w, int h)
{
	surface_t *ic = surf_new(64, 64);
	int i;

	(void)win;
	icon_drop(ic, 0, 0, 64);
	blit(s, ic, x + 24, y + 20, 255);
	surf_free(ic);
	draw_text(s, fonts[FONT_UI_BOLD], 17, x + 104, y + 44, 0xff000000,
		  _("Install Water Candy"));

	if (I.state == ST_CHOOSE) {
		draw_text(s, fonts[FONT_UI], 12, x + 104, y + 66, 0xff404040,
			  _("Choose the disk to install Water Candy on."));
		draw_text(s, fonts[FONT_UI], 12, x + 104, y + 84, 0xffb02020,
			  _("Everything on that disk will be erased."));
		fill_rect(s, x + 24, y + LIST_Y - 1, w - 48, I.ndisks * DISK_H + 2, 0xff9a9a9a);
		for (i = 0; i < I.ndisks; i++) {
			int ry = y + LIST_Y + i * DISK_H;
			char line[256], size[32];
			uint32_t fg = i == I.sel ? 0xffffffff : 0xff000000;
			surface_t *di = surf_new(30, 30);

			if (i == I.sel)
				aqua_highlight(s, x + 25, ry, w - 50, DISK_H, 0);
			else
				fill_rect(s, x + 25, ry, w - 50, DISK_H, i & 1 ? 0xfff3f6fa : 0xffffffff);
			icon_disk(di, 0, 0, 30);
			blit(s, di, x + 34, ry + 5, 255);
			surf_free(di);
			format_size(size, sizeof(size), I.disks[i].bytes);
			snprintf(line, sizeof(line), "%s  \xe2\x80\x94  %s", I.disks[i].dev, size);
			draw_text(s, fonts[FONT_UI_BOLD], 13, x + 74, ry + 18, fg, line);
			snprintf(line, sizeof(line), "%s%s", I.disks[i].model[0] ?
				 I.disks[i].model : _("Disk"),
				 I.disks[i].removable ? _(" (removable)") : "");
			draw_text(s, fonts[FONT_UI], 11, x + 74, ry + 33,
				  i == I.sel ? 0xffd8e6ff : 0xff707070, line);
		}
		if (!I.ndisks)
			draw_text(s, fonts[FONT_UI], 13, x + 40, y + LIST_Y + 24, 0xff808080,
				  _("No suitable disk was found (at least 4 GB)."));
		aqua_checkbox(s, x + 24, y + h - 84, "Erase the selected disk and install",
			      I.agree);
		aqua_button(s, x + w - 230, y + h - 44, 100, 24, "Cancel", 0, I.pressed == 1);
		aqua_button(s, x + w - 120, y + h - 44, 100, 24, "Install",
			    I.agree && I.sel >= 0, I.pressed == 2);
		return;
	}
	if (I.state == ST_RUNNING) {
		draw_text(s, fonts[FONT_UI], 13, x + 104, y + 70, 0xff404040,
			  _("Installing. This can take a while."));
		progress_bar(s, x + 40, y + 160, w - 80, I.progress);
		draw_text(s, fonts[FONT_UI], 12, x + 40, y + 196, 0xff303030, I.step);
		return;
	}
	if (I.state == ST_DONE) {
		draw_text(s, fonts[FONT_UI], 13, x + 104, y + 70, 0xff206020,
			  _("Water Candy is installed."));
		draw_text(s, fonts[FONT_UI], 12, x + 104, y + 92, 0xff404040,
			  _("Remove the USB stick and restart the computer."));
		aqua_button(s, x + w - 140, y + h - 44, 120, 24, "Restart", 1, I.pressed == 3);
		return;
	}
	draw_text(s, fonts[FONT_UI], 13, x + 104, y + 70, 0xffb02020,
		  _("The installation failed."));
	draw_text(s, fonts[FONT_UI], 12, x + 104, y + 92, 0xff404040, I.error);
	aqua_button(s, x + w - 120, y + h - 44, 100, 24, "OK", 1, I.pressed == 4);
}

static int inst_hit(window_t *win, int x, int y)
{
	int w = win->w - 2, h = win->h - TITLE_H - 1;

	if (y >= h - 44 && y < h - 20) {
		if (I.state == ST_CHOOSE && x >= w - 230 && x < w - 130)
			return 1;
		if (I.state == ST_CHOOSE && x >= w - 120 && x < w - 20)
			return 2;
		if (I.state == ST_DONE && x >= w - 140 && x < w - 20)
			return 3;
		if (I.state == ST_FAILED && x >= w - 120 && x < w - 20)
			return 4;
	}
	return 0;
}

static void inst_mouse(window_t *win, const event_t *ev, int x, int y)
{
	int h = win->h - TITLE_H - 1;

	if (ev->type != EV_BUTTON || ev->button != 1)
		return;
	if (ev->pressed) {
		I.pressed = inst_hit(win, x, y);
		if (I.state == ST_CHOOSE && y >= LIST_Y && y < LIST_Y + I.ndisks * DISK_H)
			I.sel = (y - LIST_Y) / DISK_H;
		if (I.state == ST_CHOOSE && y >= h - 88 && y < h - 64 && x < 420)
			I.agree = !I.agree;
	} else {
		int b = inst_hit(win, x, y);

		if (b && b == I.pressed) {
			if (b == 1 || b == 4)
				wm_close(win);
			else if (b == 2 && I.agree && I.sel >= 0)
				wc_with_admin(start_install);
			else if (b == 3)
				wc_shutdown(1);
		}
		I.pressed = 0;
	}
	wm_damage(win);
}

static void inst_tick(window_t *win)
{
	static double last;

	if (I.state == ST_RUNNING && dt.now - last > 0.5) {
		last = dt.now;
		wm_damage(win);
	}
}

static void inst_destroy(window_t *win)
{
	(void)win;
	if (I.state != ST_RUNNING)
		I.state = ST_CHOOSE;
	I.win = NULL;
}

void app_installer(void)
{
	if (I.win) {
		wm_focus(I.win);
		return;
	}
	if (I.state != ST_RUNNING) {
		I.state = ST_CHOOSE;
		I.agree = 0;
		scan_disks();
	}
	I.win = wm_create("Install Water Candy", APP_INSTALLER, INST_W, INST_H);
	I.win->draw = inst_draw;
	I.win->mouse = inst_mouse;
	I.win->tick = inst_tick;
	I.win->destroy = inst_destroy;
}
