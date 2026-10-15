// SPDX-License-Identifier: GPL-2.0
/*
 * Water Candy - network status, read straight from the kernel.
 */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "wc.h"

static int default_route(char *ifname, size_t ifsz, char *gw, size_t gwsz)
{
	FILE *f = fopen("/proc/net/route", "r");
	char line[256], iface[32];
	unsigned dest, gate, flags;
	int found = 0;

	if (!f)
		return 0;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%31s %x %x %x", iface, &dest, &gate, &flags) != 4)
			continue;
		if (dest == 0 && (flags & 1)) {
			struct in_addr a = { .s_addr = gate };

			snprintf(ifname, ifsz, "%s", iface);
			inet_ntop(AF_INET, &a, gw, gwsz);
			found = 1;
			break;
		}
	}
	fclose(f);
	return found;
}

void net_poll(struct net_status *st)
{
	struct ifaddrs *ifa, *i;
	char path[96], route_if[32] = "";
	FILE *f;

	memset(st, 0, sizeof(*st));
	default_route(route_if, sizeof(route_if), st->gateway, sizeof(st->gateway));

	if (!getifaddrs(&ifa)) {
		for (i = ifa; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET ||
			    (i->ifa_flags & IFF_LOOPBACK) || !(i->ifa_flags & IFF_UP))
				continue;
			/* Prefer the interface that carries the default route. */
			if (st->ifname[0] && strcmp(i->ifa_name, route_if))
				continue;
			snprintf(st->ifname, sizeof(st->ifname), "%s", i->ifa_name);
			inet_ntop(AF_INET,
				  &((struct sockaddr_in *)i->ifa_addr)->sin_addr,
				  st->addr, sizeof(st->addr));
		}
		freeifaddrs(ifa);
	}
	if (!st->ifname[0] && route_if[0])
		snprintf(st->ifname, sizeof(st->ifname), "%s", route_if);

	snprintf(st->kind, sizeof(st->kind), "Ethernet");
	if (st->ifname[0]) {
		snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", st->ifname);
		if (!access(path, F_OK))
			snprintf(st->kind, sizeof(st->kind), "Wi-Fi");
		else if (!strncmp(st->ifname, "usb", 3) || !strncmp(st->ifname, "enx", 3))
			snprintf(st->kind, sizeof(st->kind), "USB Ethernet");
	}

	f = fopen("/etc/resolv.conf", "r");
	if (f) {
		char line[256];

		while (fgets(line, sizeof(line), f))
			if (sscanf(line, "nameserver %63s", st->dns) == 1)
				break;
		fclose(f);
	}
	st->connected = st->addr[0] && route_if[0] && !strcmp(route_if, st->ifname);
}
