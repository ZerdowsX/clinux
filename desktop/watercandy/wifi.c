// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - Wi-Fi through NetworkManager (nmcli): the list of networks
 * in the menu bar, connecting, and turning the radio on and off.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wc.h"

#define MAX_NETS 16

static struct wifi_net nets[MAX_NETS];
/* What the open menu shows; clicks refer to this, not to a newer scan. */
static struct wifi_net shown[MAX_NETS];
static int nshown;
static int pending_secure, asked_password;
static int nnets, present, enabled = 1, scanning;
static char saved[2048];	/* names of saved connections, '\n' separated */
static double last_scan = -100;
static char pending_ssid[64];

int wifi_present(void)
{
	return present;
}

int wifi_enabled(void)
{
	return enabled;
}

int wifi_networks(const struct wifi_net **list)
{
	memcpy(shown, nets, sizeof(nets));
	nshown = nnets;
	*list = shown;
	return nshown;
}

/* Split one nmcli terse line on unescaped ':' (SSIDs may contain "\:"). */
static int split_terse(char *line, char **fields, int max)
{
	int n = 0;
	char *r = line, *w = line;

	fields[n++] = w;
	while (*r) {
		if (r[0] == '\\' && r[1]) {
			*w++ = r[1];
			r += 2;
		} else if (*r == ':' && n < max) {
			*w++ = 0;
			r++;
			fields[n++] = w;
		} else {
			*w++ = *r++;
		}
	}
	*w = 0;
	return n;
}

static void got_list(int status, const char *out, void *ctx)
{
	const char *p = out;

	(void)ctx;
	scanning = 0;
	if (status)
		return;
	nnets = 0;
	while (p && *p && nnets < MAX_NETS) {
		const char *nl = strchr(p, '\n');
		char line[256], *f[4];
		int i, dup = 0;

		snprintf(line, sizeof(line), "%.*s", (int)(nl ? nl - p : (int)strlen(p)), p);
		p = nl ? nl + 1 : NULL;
		if (split_terse(line, f, 4) < 4 || !f[1][0])
			continue;
		for (i = 0; i < nnets; i++)
			if (!strcmp(nets[i].ssid, f[1]))
				dup = 1;
		if (dup)
			continue;
		snprintf(nets[nnets].ssid, sizeof(nets[0].ssid), "%s", f[1]);
		nets[nnets].active = f[0][0] == '*';
		nets[nnets].signal = atoi(f[2]);
		nets[nnets].secure = f[3][0] && strcmp(f[3], "--");
		nnets++;
	}
	wm_menus_changed();
}

static void got_saved(int status, const char *out, void *ctx)
{
	(void)ctx;
	if (!status)
		snprintf(saved, sizeof(saved), "%s", out);
}

static void got_radio(int status, const char *out, void *ctx)
{
	const char *list[] = { "nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
			       "device", "wifi", "list", NULL };
	const char *conns[] = { "nmcli", "-t", "-f", "NAME", "connection", "show", NULL };

	(void)ctx;
	enabled = status || strncmp(out, "enabled", 7) == 0;
	proc_run(conns, 0, got_saved, NULL, NULL);
	if (enabled)
		proc_run(list, 0, got_list, NULL, NULL);
	else
		scanning = 0;
}

static void got_devices(int status, const char *out, void *ctx)
{
	const char *radio[] = { "nmcli", "radio", "wifi", NULL };

	(void)ctx;
	if (present != (!status && strstr(out, ":wifi:") != NULL))
		wm_menus_changed();
	present = !status && strstr(out, ":wifi:") != NULL;
	if (!present || proc_run(radio, 0, got_radio, NULL, NULL) < 0)
		scanning = 0;
}

void wifi_scan(void)
{
	const char *argv[] = { "nmcli", "-t", "-f", "DEVICE,TYPE,STATE", "device", NULL };

	if (scanning || access("/usr/bin/nmcli", X_OK))
		return;
	scanning = 1;
	last_scan = dt.now;
	if (proc_run(argv, 0, got_devices, NULL, NULL) < 0)
		scanning = 0;
}

void wifi_tick(void)
{
	if (!login_active() && dt.now - last_scan > 30)
		wifi_scan();
}

static void ask_password(void);

static void connect_done(int status, const char *out, void *ctx)
{
	char msg[160];

	(void)ctx;
	/* A saved network whose password is missing or wrong: ask for it. */
	if (status && pending_secure && !asked_password &&
	    (strstr(out, "ecrets") || strstr(out, "assword"))) {
		ask_password();
		return;
	}
	if (status) {
		const char *e = strstr(out, "Error:");

		snprintf(msg, sizeof(msg), "%.*s", (int)strcspn(e ? e : out, "\n"),
			 e ? e : out);
		notify(_("Could not join the network"), msg, NULL);
	} else {
		snprintf(msg, sizeof(msg), _("Connected to %s"), pending_ssid);
		notify(_("Wi-Fi"), msg, NULL);
	}
	last_scan = -100;	/* refresh soon */
}

static void connect_with(const char *password, void *ctx)
{
	const char *argv[] = { "nmcli", "device", "wifi", "connect", pending_ssid,
			       "password", password, NULL };

	(void)ctx;
	if (!password)
		return;
	proc_run(argv, 0, connect_done, NULL, NULL);
}

static int is_saved(const char *ssid)
{
	const char *p = saved;
	size_t n = strlen(ssid);

	while (p && *p) {
		if (!strncmp(p, ssid, n) && (p[n] == '\n' || !p[n]))
			return 1;
		p = strchr(p, '\n');
		if (p)
			p++;
	}
	return 0;
}

static void ask_password(void)
{
	char msg[160];

	asked_password = 1;
	snprintf(msg, sizeof(msg), _("The Wi-Fi network \"%s\" requires a password."),
		 pending_ssid);
	wc_prompt(_("Join Wi-Fi network"), msg, 1, "Join", connect_with, NULL);
}

void wifi_connect(int i)
{
	if (i < 0 || i >= nshown)
		return;
	snprintf(pending_ssid, sizeof(pending_ssid), "%s", shown[i].ssid);
	pending_secure = shown[i].secure;
	asked_password = 0;
	if (is_saved(pending_ssid)) {
		const char *argv[] = { "nmcli", "connection", "up", "id", pending_ssid, NULL };

		proc_run(argv, 0, connect_done, NULL, NULL);
	} else if (shown[i].secure) {
		ask_password();
	} else {
		const char *argv[] = { "nmcli", "device", "wifi", "connect", pending_ssid, NULL };

		proc_run(argv, 0, connect_done, NULL, NULL);
	}
}

void wifi_toggle(int on)
{
	const char *argv[] = { "nmcli", "radio", "wifi", on ? "on" : "off", NULL };

	enabled = on;
	if (!on)
		nnets = 0;
	proc_run(argv, 0, NULL, NULL, NULL);
	last_scan = dt.now - 25;	/* rescan in a few seconds */
}

/* Signal strength as 1..4 bars. */
int wifi_bars(int signal)
{
	return signal > 75 ? 4 : signal > 50 ? 3 : signal > 25 ? 2 : 1;
}
