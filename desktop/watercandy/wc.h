/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Water Candy - a small Aqua-style desktop shell.
 */
#ifndef WC_H
#define WC_H

#include "font.h"
#include "gfx.h"
#include "input.h"

#include <stddef.h>

#define WC_NAME		"Water Candy"
#define WC_VERSION	"1.0"

#define MENUBAR_H	22
#define TITLE_H		22
#define GROW_BOX	15

enum app_id {
	APP_NONE,
	APP_FILES,
	APP_TERMINAL,
	APP_CLOCK,
	APP_PREFS,
	APP_ABOUT,
	APP_X11,		/* any X11 program shown through the X11 bridge */
	APP_FIREFOX,
	APP_ALERT,
	APP_STORE,
	APP_TEXTEDIT,
	APP_PREVIEW,
	APP_INSTALLER,
	APP_COUNT
};

enum anim_kind { ANIM_NONE, ANIM_OPEN, ANIM_CLOSE, ANIM_MINIMIZE, ANIM_RESTORE };

typedef struct window window_t;

struct window {
	int x, y, w, h;
	char title[128];
	enum app_id app;
	/* Menu bar name for X11 programs, from their WM_CLASS. */
	char app_name[32];

	surface_t *surf;
	int dirty;
	surface_t *shadow;
	int shadow_w, shadow_h, shadow_active;

	int minimized, zoomed, saved[4];
	int resizable, min_w, min_h;
	int hover_btns, pressed_btn;
	/* The content draws its own pointer (X11 programs). */
	int own_cursor;

	enum anim_kind anim;
	double anim_t0;

	/* Content callbacks; coordinates are relative to the content area. */
	void (*draw)(window_t *win, surface_t *s, int x, int y, int w, int h);
	void (*mouse)(window_t *win, const event_t *ev, int x, int y);
	void (*key)(window_t *win, const event_t *ev);
	/* If set, receives raw key presses and releases instead of key. */
	void (*rawkey)(window_t *win, const event_t *ev);
	void (*tick)(window_t *win);
	void (*resized)(window_t *win);
	void (*destroy)(window_t *win);
	/* Edit menu: copy the selection to the clipboard / paste it. */
	void (*copy)(window_t *win);
	void (*paste)(window_t *win);
	/* File > Save; edited puts the dot in the close button. */
	void (*save)(window_t *win);
	int edited;
	void *data;

	window_t *next;		/* stacking order, bottom first */
};

enum { LANG_EN, LANG_RU };

struct settings {
	int lang;
	int animate_wallpaper;
	int magnify_dock;
	int show_seconds;
	float wallpaper_speed;
};

struct desktop {
	int w, h;
	surface_t *frame;
	surface_t *wall;
	window_t *windows;
	window_t *focus;
	struct settings set;
	double now;
	int redraw;
	int quit;
	unsigned long frames;
	/* The person using the desktop; programs run as them, not as root. */
	int uid, gid;
	char user[64], home[256], shell[128];
};

extern struct desktop dt;

double now_s(void);

/* Shading helpers. */
uint32_t shade(uint32_t c, float f);

/* wallpaper.c */
void wallpaper_init(int w, int h);
void wallpaper_render(surface_t *dst, double t);

/* icons.c - all icons are drawn procedurally at any size. */
void icon_drop(surface_t *s, float x, float y, float size);
void icon_folder(surface_t *s, float x, float y, float size);
void icon_document(surface_t *s, float x, float y, float size);
void icon_terminal(surface_t *s, float x, float y, float size);
void icon_clock(surface_t *s, float x, float y, float size, int hh, int mm,
		int ss);
void icon_gear(surface_t *s, float x, float y, float size);
void icon_trash(surface_t *s, float x, float y, float size);
void icon_disk(surface_t *s, float x, float y, float size);
void icon_app(surface_t *s, float x, float y, float size);
void icon_globe(surface_t *s, float x, float y, float size);
void icon_network(surface_t *s, float x, float y, float size, int connected);
void icon_store(surface_t *s, float x, float y, float size);
void icon_installer(surface_t *s, float x, float y, float size);
void icon_flag(surface_t *s, float x, float y, float w, float h, int layout);

/* entry.c - single-line text field. */
typedef struct entry {
	char text[512];
	int cursor;		/* byte offset */
	int scroll;
	int password;
	int focused;
	const char *placeholder;
} entry_t;
enum { ENTRY_IGNORED, ENTRY_CHANGED, ENTRY_ENTER, ENTRY_ESCAPE, ENTRY_TAB };
void entry_set(entry_t *e, const char *text);
int entry_key(entry_t *e, const event_t *ev);
void entry_draw(surface_t *s, entry_t *e, int x, int y, int w, int h);

/* aqua.c - Aqua widgets. */
void aqua_gel_circle(surface_t *s, float cx, float cy, float r, uint32_t base);
void aqua_traffic(surface_t *s, float cx, float cy, float r, int which,
		  int active, int show_symbol);
void aqua_button(surface_t *s, int x, int y, int w, int h, const char *label,
		 int primary, int pressed);
void aqua_checkbox(surface_t *s, int x, int y, const char *label, int checked);
void aqua_slider(surface_t *s, int x, int y, int w, float value);
void aqua_highlight(surface_t *s, int x, int y, int w, int h, float r);
surface_t *glass_text(font_t *f, float size, const char *text);

/* wm.c - windows, menu bar, dock, desktop. */
void wm_init(void);
window_t *wm_create(const char *title, enum app_id app, int w, int h);
void wm_close(window_t *win);
void wm_focus(window_t *win);
void wm_damage(window_t *win);
void wm_set_title(window_t *win, const char *title);
void wm_minimize(window_t *win);
void wm_restore(window_t *win);
void wm_zoom(window_t *win);
window_t *wm_find_app(enum app_id app);
void wm_event(const event_t *ev);
void wm_tick(void);
int wm_animating(void);
void wm_render(void);
void wm_dock_bounce(enum app_id app);

/* Applications. */
void app_launch(enum app_id app);
void app_about(void);
void app_terminal(void);
/* Terminal running cmd in the user's shell, then staying interactive. */
void app_terminal_cmd(const char *cmd);
/* Show an X11 program in an Aqua window; returns 0 on success. */
int app_x11(const char *cmd, const char *title, enum app_id app);
int x11_available(void);
/* Tell every X11 program which keyboard layout is active. */
void x11_set_layout(int layout);
void app_firefox(void);
const char *firefox_path(void);
/* A small dialog; button2/act2 may be NULL. */
void wc_alert(const char *title, const char *message, const char *button2,
	      void (*act2)(void));
void app_files(const char *path, const char *title);
void app_prefs(void);
void app_clock(void);

void wc_shutdown(int reboot_instead);

/*
 * clipboard.c - shared clipboard. source is CLIP_DESKTOP for the desktop's
 * own apps, or the X display number of the X11 program it came from.
 */
#define CLIP_DESKTOP 0
void clip_set(const char *text, int source);
const char *clip_get(void);
unsigned clip_version(void);
int clip_owner(void);

/* proc.c - background commands. */
#include <sys/types.h>
#define PROC_AS_USER	1	/* run as the desktop user instead of root */
#define PROC_STATUS_FD	2	/* give the child an APT status pipe on fd 3 */
#define PROC_CHECK	4	/* proc_capture: NULL if the command fails */
typedef void (*proc_done_fn)(int status, const char *output, void *ctx);
typedef void (*proc_status_fn)(const char *line, void *ctx);
int proc_run(const char *const argv[], int flags, proc_done_fn done,
	     proc_status_fn on_status, void *ctx);
int proc_child_exited(pid_t pid, int status);
void proc_tick(void);
int proc_busy(void);
char *proc_capture(const char *const argv[], int flags);
char *proc_capture_len(const char *const argv[], int flags, size_t *len);
char *user_read_file_len(const char *path, size_t *len);

/* notify.c - Growl-style notification banners. */
void notify(const char *title, const char *text, void (*click)(void));
void notify_render(surface_t *f);
int notify_click(int x, int y);
int notify_animating(void);

/* store.c - App Store over APT, and update checks. */
void app_store(int updates_tab);
void updates_check_start(void);
void updates_tick(void);
void updates_schedule(double delay);

/* wifi.c - Wi-Fi through NetworkManager. */
struct wifi_net {
	char ssid[64];
	int signal, secure, active;
};
int wifi_present(void);
int wifi_enabled(void);
int wifi_networks(const struct wifi_net **list);
void wifi_scan(void);
void wifi_tick(void);
void wifi_connect(int i);
void wifi_toggle(int on);
int wifi_bars(int signal);
void icon_wifi(surface_t *s, float x, float y, float size, int bars, uint32_t color);

/* image.c */
surface_t *image_load(const char *path);
surface_t *image_load_mem(const unsigned char *data, size_t len);
int image_save_png(const char *path, const surface_t *s);

/* Documents: TextEdit, Preview, and opening files by their kind. */
void app_textedit(const char *path);
void app_preview(const char *path);
int is_image_file(const char *name);
void wc_open_file(const char *path);
void take_screenshot(void);

/* installer.c - installing from the live ISO. */
int live_system(void);
void app_installer(void);
/* Run next() once the user has confirmed with their password. */
void wc_with_admin(void (*next)(void));

/* spotlight.c */
extern struct menu_custom spotlight_menu;
void spotlight_menu_icon(surface_t *s, int x, int w, int on);

/* sound.c - volume. */
int sound_available(void);
void sound_tick(void);
int sound_key(const event_t *ev);
int sound_osd_active(void);
void sound_osd_render(surface_t *f);
void sound_menu_icon(surface_t *s, int x, int w, int on);
void sound_draw_speaker(surface_t *s, float x, float y, float S, int level,
			uint32_t color);
extern const struct menu_custom volume_menu;

/* A dialog asking for one line of text; cb gets NULL on Cancel. */
void wc_prompt(const char *title, const char *message, int password,
	       const char *ok_label, void (*cb)(const char *text, void *ctx),
	       void *ctx);

/* i18n.c - interface language. */
void i18n_init(void);
const char *tr(const char *s);
#define _(s) tr(s)
const char *plural(int n, const char *one, const char *few, const char *many);
void format_items(char *buf, size_t sz, int n);
struct tm;
void format_clock(char *buf, size_t sz, const struct tm *tm, int seconds);
void format_date(char *buf, size_t sz, const struct tm *tm);

/*
 * A menu-bar dropdown with its own content (volume slider, Spotlight).
 * mouse() and key() return 1 when the menu should close.
 */
struct menu_custom {
	int w, h;
	void (*draw)(surface_t *s, int w, int h);
	int (*mouse)(const event_t *ev, int x, int y);
	int (*key)(const event_t *ev);
	void (*opened)(void);
};
void wm_menu_changed(void);
void wm_menu_close(void);
void wm_menus_changed(void);
void wm_relayout_language(void);

/* user.c - run programs as the desktop user. */
void user_init(void);
/* Call in a forked child before exec: drop to the user, set HOME etc. */
void user_become(void);
/* Switch the desktop to this account; 0 on success. */
int user_select(const char *name);
/* Check a password with PAM; returns 1 if it is right. */
int user_check_password(const char *name, const char *password);
/* Whether a login window is possible (built with PAM, running as root). */
int user_can_login(void);
/* Name from /etc/watercandy.conf "autologin=", or NULL. */
const char *user_autologin(void);

/* login.c - the login window. */
int login_active(void);
void login_start(void);
void login_render(surface_t *f);
void login_event(const event_t *ev);
/* Called when someone logs in (or at startup with autologin). */
void session_start(void);

/* net.c - network status for the menu bar and preferences. */
struct net_status {
	int connected;		/* has an IPv4 address and a default route */
	char ifname[32];
	char addr[64];
	char gateway[64];
	char dns[64];
	char kind[32];		/* "Ethernet", "Wi-Fi", ... */
};
void net_poll(struct net_status *st);

#endif
