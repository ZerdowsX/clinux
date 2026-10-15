// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - the desktop runs as root (it owns the display and input
 * devices) but starts programs as the person using it.
 *
 * /etc/passwd and /etc/group are parsed directly so this also works in a
 * statically linked build without NSS modules.
 */
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_PAM
#include <security/pam_appl.h>
#endif

#include "wc.h"

static void set_user(const struct passwd *pw)
{
	dt.uid = pw->pw_uid;
	dt.gid = pw->pw_gid;
	snprintf(dt.user, sizeof(dt.user), "%s", pw->pw_name);
	snprintf(dt.home, sizeof(dt.home), "%s", pw->pw_dir);
	snprintf(dt.shell, sizeof(dt.shell), "%s",
		 pw->pw_shell && *pw->pw_shell ? pw->pw_shell : "/bin/sh");
}

int user_select(const char *name)
{
	FILE *f = fopen("/etc/passwd", "r");
	struct passwd *pw;
	int found = 0;

	if (!f)
		return -1;
	while ((pw = fgetpwent(f)))
		if (!strcmp(pw->pw_name, name)) {
			set_user(pw);
			found = 1;
			break;
		}
	fclose(f);
	return found ? 0 : -1;
}

const char *user_autologin(void)
{
	static char name[64];
	char line[256];
	FILE *f = getenv("WC_AUTOLOGIN") ? NULL : fopen("/etc/watercandy.conf", "r");

	if (getenv("WC_AUTOLOGIN")) {
		snprintf(name, sizeof(name), "%s", getenv("WC_AUTOLOGIN"));
		return name[0] ? name : NULL;
	}
	if (!f)
		return NULL;
	name[0] = 0;
	while (fgets(line, sizeof(line), f))
		if (sscanf(line, " autologin = %63s", name) == 1 ||
		    sscanf(line, " autologin=%63s", name) == 1)
			break;
	fclose(f);
	return name[0] ? name : NULL;
}

#ifdef HAVE_PAM
static int pam_conv_fn(int n, const struct pam_message **msg,
		       struct pam_response **resp, void *data)
{
	struct pam_response *r = calloc(n, sizeof(*r));
	int i;

	if (!r)
		return PAM_BUF_ERR;
	for (i = 0; i < n; i++)
		if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
		    msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
			r[i].resp = strdup(data);
	*resp = r;
	return PAM_SUCCESS;
}

/* No artificial delay on failure: the panel shakes instead. */
static void no_delay(int retval, unsigned usec, void *data)
{
	(void)retval;
	(void)usec;
	(void)data;
}

int user_check_password(const char *name, const char *password)
{
	struct pam_conv conv = { pam_conv_fn, (void *)password };
	pam_handle_t *h;
	int r;

	if (pam_start("watercandy", name, &conv, &h) != PAM_SUCCESS)
		return 0;
	pam_set_item(h, PAM_FAIL_DELAY, (const void *)no_delay);
	r = pam_authenticate(h, 0);
	if (r == PAM_SUCCESS)
		r = pam_acct_mgmt(h, 0);
	pam_end(h, r);
	return r == PAM_SUCCESS;
}

int user_can_login(void)
{
	return getuid() == 0;
}
#else
int user_check_password(const char *name, const char *password)
{
	(void)name;
	(void)password;
	return 0;
}

int user_can_login(void)
{
	return 0;
}
#endif

void user_init(void)
{
	const char *want = getenv("WC_USER");
	FILE *f = fopen("/etc/passwd", "r");
	struct passwd *pw;
	int found = 0;

	dt.uid = getuid();
	dt.gid = getgid();
	snprintf(dt.user, sizeof(dt.user), "%s", dt.uid ? "user" : "root");
	snprintf(dt.home, sizeof(dt.home), "%s",
		 getenv("HOME") ? getenv("HOME") : "/root");
	snprintf(dt.shell, sizeof(dt.shell), "/bin/sh");
	if (!f)
		return;

	/* WC_USER, else the first regular account when running as root. */
	while (!found && (pw = fgetpwent(f))) {
		if (want && *want) {
			if (!strcmp(pw->pw_name, want))
				found = 1;
		} else if (getuid() == 0) {
			if (pw->pw_uid >= 1000 && pw->pw_uid < 60000)
				found = 1;
		} else if (pw->pw_uid == getuid()) {
			found = 1;
		}
		if (found)
			set_user(pw);
	}
	if (!found && getuid() == 0) {
		rewind(f);
		while ((pw = fgetpwent(f)))
			if (pw->pw_uid == 0) {
				set_user(pw);
				break;
			}
	}
	fclose(f);
}

static int user_groups(gid_t *groups, int max)
{
	FILE *f = fopen("/etc/group", "r");
	struct group *gr;
	int n = 0;

	groups[n++] = dt.gid;
	if (!f)
		return n;
	while (n < max && (gr = fgetgrent(f))) {
		char **m;

		for (m = gr->gr_mem; m && *m; m++) {
			if (!strcmp(*m, dt.user) && gr->gr_gid != (gid_t)dt.gid) {
				groups[n++] = gr->gr_gid;
				break;
			}
		}
	}
	fclose(f);
	return n;
}

void user_become(void)
{
	if (getuid() == 0 && dt.uid != 0) {
		gid_t groups[64];
		int n = user_groups(groups, 64);

		if (setgroups(n, groups) || setgid(dt.gid) || setuid(dt.uid))
			_exit(126);
	}
	setenv("HOME", dt.home, 1);
	setenv("USER", dt.user, 1);
	setenv("LOGNAME", dt.user, 1);
	setenv("SHELL", dt.shell, 1);
	if (!getenv("PATH"))
		setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin", 1);
	if (!getenv("LANG"))
		setenv("LANG", dt.set.lang == LANG_RU ? "ru_RU.UTF-8" : "C.UTF-8", 1);
	{
		/* The user's systemd instance (sound, D-Bus) when it is running. */
		char run[64], bus[96];

		snprintf(run, sizeof(run), "/run/user/%d", dt.uid);
		if (!access(run, F_OK)) {
			setenv("XDG_RUNTIME_DIR", run, 1);
			snprintf(bus, sizeof(bus), "unix:path=%s/bus", run);
			setenv("DBUS_SESSION_BUS_ADDRESS", bus, 1);
		}
	}
	if (chdir(dt.home) && chdir("/"))
		_exit(1);
}
