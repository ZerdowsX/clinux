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
		setenv("LANG", "C.UTF-8", 1);
	if (chdir(dt.home) && chdir("/"))
		_exit(1);
}
