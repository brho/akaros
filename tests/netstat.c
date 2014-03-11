/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <sys/types.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <iplib.h>
#include <dirent.h>

enum {
	maxproto = 20,
};
void pip(char *, struct dirent *);
void nstat(char *, void (*)(char *, struct dirent *));
void pipifc(void);

FILE *out;
char *netroot;
char *proto[maxproto];
int nproto;
int notrans;

void usage(char *argv0)
{
	fprintf(stderr, "usage: %s [-in] [-p proto] [network-dir]\n", argv0);
	fprintf(stderr, "usage");
	exit(1);
}

void main(int argc, char *argv[])
{
	int justinterfaces = 0;
	int i, tot, fd;
	DIR *dir;
	struct dirent *d;
	char buf[128];
	out = stdout;
	argc--, argv++;
	while (argc > 0 && *argv[0] == '-') {
		switch (argv[0][1]) {
			case 'i':
				justinterfaces = 1;
				break;
			case 'n':
				notrans = 1;
				break;
			case 'p':
				if (nproto >= maxproto){
					fprintf(stderr, "too many protos");
					exit(1);
				}
				if (argc < 2)
					usage("netstat");
				argc--, argv++;
				proto[nproto++] = argv[0];
				break;
			default:
				usage("netstat");
		}
		argc--, argv++;
	}

	netroot = "/net";
	switch (argc) {
		case 0:
			break;
		case 1:
			netroot = argv[0];
			break;
		default:
			usage("netstat");
	}

	if (justinterfaces) {
		pipifc();
		exit(0);
	}

	if (nproto) {
		for (i = 0; i < nproto; i++)
			nstat(proto[i], pip);
	} else {
		dir = opendir(netroot);
		if (!dir) {
			fprintf(stderr, "open %s: %r", netroot);
			exit(1);
		}

		while ((d = readdir(dir))) {
			if (strcmp(d->d_name, "ipifc") == 0)
				continue;
			snprintf(buf, sizeof buf, "%s/%s/0/local", netroot, d->d_name);
			/* access is bogus for now. */
			if (1 || access(buf, 0) >= 0)
				nstat(d->d_name, pip);
			else
				fprintf(stderr, "Can't access %s\n", d->d_name);
		}
	}

	exit(0);
}

void nstat(char *net, void (*f) (char *, struct dirent *))
{
	int fdir, i, tot;
	struct dirent *d;
	DIR *dir;
	char buf[128];

	snprintf(buf, sizeof buf, "%s/%s", netroot, net);
	dir = opendir(buf);
	if (!dir)
		return;

	while ((d = readdir(dir))) {
		(*f) (net, d);
	}
	/* leak dir */
}

char *getport(char *net, char *p)
{
	static char port[10];

	strncpy(port, p, sizeof(port) - 1);
	port[sizeof(port) - 1] = 0;
	if (1)	//if(notrans || (p = csgetvalue(netroot, "port", p, net, nil)) == nil)
		return port;
	strncpy(port, p, sizeof(port) - 1);
	port[sizeof(port) - 1] = 0;
	free(p);
	return port;
}

void pip(char *net, struct dirent *db)
{
	int n, fd;
	char buf[128], *p;
	char *dname;

	if (strcmp(db->d_name, "clone") == 0)
		return;
	if (strcmp(db->d_name, "stats") == 0)
		return;

	snprintf(buf, sizeof buf, "%s/%s/%s/status", netroot, net, db->d_name);
	fd = open(buf, O_RDONLY);
	if (fd < 0)
		return;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < 0)
		return;
	buf[n] = 0;

	p = strchr(buf, ' ');
	if (p != 0)
		*p = 0;
	p = strrchr(buf, '\n');
	if (p != 0)
		*p = 0;
	fprintf(out, "%-4s %-4s %-12s ", net, db->d_name, /*db->uid, */ buf);

	snprintf(buf, sizeof buf, "%s/%s/%s/local", netroot, net, db->d_name);
	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		fprintf(out, "\n");
		return;
	}
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < 0) {
		fprintf(out, "\n");
		return;
	}
	buf[n - 1] = 0;
	p = strchr(buf, '!');
	if (p == 0) {
		fprintf(out, "\n");
		return;
	}
	*p = '\0';
	fprintf(out, "%-10s ", getport(net, p + 1));

	snprintf(buf, sizeof buf, "%s/%s/%s/remote", netroot, net, db->d_name);
	fd = open(buf, O_RDONLY);
	if (fd < 0) {
		printf("\n");
		return;
	}
	n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n < 0) {
		printf("\n");
		return;
	}
	buf[n - 1] = 0;
	p = strchr(buf, '!');
	if (p != NULL)
		*p++ = '\0';

	if (notrans) {
		fprintf(out, "%-10s %s\n", getport(net, p), buf);
		return;
	}
	dname = NULL;	//csgetvalue(netroot, "ip", buf, "dom", nil);
	if (dname == NULL) {
		fprintf(out, "%-10s %s\n", getport(net, p), buf);
		return;
	}
	fprintf(out, "%-10s %s\n", getport(net, p), dname);
	free(dname);
}

void pipifc(void)
{
	struct ipifc *ip, *nip;
	struct iplifc *lifc;
	char buf[100];
	int l, i;

//  fmtinstall('I', eipfmt);
//  fmtinstall('M', eipfmt);

	ip = readipifc(netroot, NULL, -1);

	l = 7;
	for (nip = ip; nip; nip = nip->next) {
		for (lifc = nip->lifc; lifc; lifc = lifc->next) {
			i = snprintf(buf, sizeof buf, "%I", lifc->ip);
			if (i > l)
				l = i;
			i = snprintf(buf, sizeof buf, "%I", lifc->net);
			if (i > l)
				l = i;
		}
	}

	for (nip = ip; nip; nip = nip->next) {
		for (lifc = nip->lifc; lifc; lifc = lifc->next)
			fprintf(out, "%-12s %5d %-*I %5M %-*I %8lud %8lud %8lud %8lud\n",
					nip->dev, nip->mtu,
					l, lifc->ip, lifc->mask, l, lifc->net,
					nip->pktin, nip->pktout, nip->errin, nip->errout);
	}
}
