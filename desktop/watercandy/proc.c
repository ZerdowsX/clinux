// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - background commands whose output is collected without
 * blocking the desktop (APT, package queries, nmcli, wpctl...).
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "wc.h"

#define MAX_PROCS 16

struct proc {
	pid_t pid;
	int out_fd, status_fd;
	char *out;
	size_t len, cap;
	char status_line[256];	/* last APT status line, if any */
	int exited, status;
	proc_done_fn done;
	void *ctx;
	proc_status_fn on_status;
};

static struct proc procs[MAX_PROCS];

static void append(struct proc *p, const char *buf, size_t n)
{
	if (p->len + n + 1 > p->cap) {
		p->cap = (p->len + n + 1) * 2;
		p->out = realloc(p->out, p->cap);
	}
	memcpy(p->out + p->len, buf, n);
	p->len += n;
	p->out[p->len] = 0;
}

int proc_run(const char *const argv[], int flags, proc_done_fn done,
	     proc_status_fn on_status, void *ctx)
{
	struct proc *p = NULL;
	int out[2], st[2] = { -1, -1 }, i;

	for (i = 0; i < MAX_PROCS; i++)
		if (!procs[i].pid) {
			p = &procs[i];
			break;
		}
	if (!p || pipe2(out, O_CLOEXEC))
		return -1;
	if ((flags & PROC_STATUS_FD) && pipe2(st, O_CLOEXEC)) {
		close(out[0]);
		close(out[1]);
		return -1;
	}
	memset(p, 0, sizeof(*p));
	p->pid = fork();
	if (p->pid < 0) {
		p->pid = 0;
		return -1;
	}
	if (p->pid == 0) {
		int null = open("/dev/null", O_RDONLY);

		setsid();
		dup2(null, 0);
		dup2(out[1], 1);
		dup2(out[1], 2);
		if (st[1] >= 0)
			dup2(st[1], 3);	/* APT::Status-Fd=3 */
		signal(SIGPIPE, SIG_DFL);
		if (flags & PROC_AS_USER)
			user_become();
		setenv("DEBIAN_FRONTEND", "noninteractive", 1);
		setenv("LC_ALL", "C.UTF-8", 1);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(out[1]);
	if (st[1] >= 0)
		close(st[1]);
	p->out_fd = out[0];
	p->status_fd = st[0];
	fcntl(p->out_fd, F_SETFL, O_NONBLOCK);
	if (p->status_fd >= 0)
		fcntl(p->status_fd, F_SETFL, O_NONBLOCK);
	p->done = done;
	p->on_status = on_status;
	p->ctx = ctx;
	append(p, "", 0);
	return p->pid;
}

/* The main loop reaps children; it tells us about the ones we started. */
int proc_child_exited(pid_t pid, int status)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++)
		if (procs[i].pid == pid) {
			procs[i].exited = 1;
			procs[i].status = status;
			return 1;
		}
	return 0;
}

static void drain(struct proc *p)
{
	char buf[4096];
	ssize_t n;

	while ((n = read(p->out_fd, buf, sizeof(buf))) > 0)
		append(p, buf, n);
	while (p->status_fd >= 0 && (n = read(p->status_fd, buf, sizeof(buf) - 1)) > 0) {
		char *line, *save = NULL;

		buf[n] = 0;
		for (line = strtok_r(buf, "\n", &save); line;
		     line = strtok_r(NULL, "\n", &save)) {
			snprintf(p->status_line, sizeof(p->status_line), "%s", line);
			if (p->on_status)
				p->on_status(line, p->ctx);
		}
	}
}

void proc_tick(void)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++) {
		struct proc *p = &procs[i];

		if (!p->pid)
			continue;
		drain(p);
		if (!p->exited)
			continue;
		drain(p);
		close(p->out_fd);
		if (p->status_fd >= 0)
			close(p->status_fd);
		p->pid = 0;
		if (p->done)
			p->done(WIFEXITED(p->status) ? WEXITSTATUS(p->status) : -1,
				p->out, p->ctx);
		free(p->out);
		p->out = NULL;
		dt.redraw = 1;
	}
}

int proc_busy(void)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++)
		if (procs[i].pid)
			return 1;
	return 0;
}

/* Run and wait (for short queries); returns malloc'd output or NULL. */
char *proc_capture(const char *const argv[], int flags)
{
	int out[2];
	pid_t pid;
	char *buf = NULL;
	size_t len = 0, cap = 0;
	char tmp[4096];
	ssize_t n;

	if (pipe2(out, O_CLOEXEC))
		return NULL;
	pid = fork();
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		dup2(null, 0);
		dup2(out[1], 1);
		dup2(null, 2);
		if (flags & PROC_AS_USER)
			user_become();
		setenv("LC_ALL", "C.UTF-8", 1);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(out[1]);
	if (pid < 0) {
		close(out[0]);
		return NULL;
	}
	while ((n = read(out[0], tmp, sizeof(tmp))) > 0 ||
	       (n < 0 && errno == EINTR)) {
		if (n <= 0)
			continue;
		if (len + n + 1 > cap) {
			cap = (len + n + 1) * 2;
			buf = realloc(buf, cap);
		}
		memcpy(buf + len, tmp, n);
		len += n;
	}
	close(out[0]);
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
		;
	if (!buf)
		buf = calloc(1, 1);
	else
		buf[len] = 0;
	return buf;
}
