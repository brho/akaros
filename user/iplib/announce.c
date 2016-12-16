/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <fcntl.h>
#include <iplib/iplib.h>
#include <parlib/parlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

static int nettrans(char *, char *, int na, char *, int);

enum {
	Maxpath = 256,
};

/* Helper, given a net directory (translated from an addr/dialstring) by
 * nettrans), clones a conversation, returns the ctl, and optionally fills in
 * dir with the path of the directory (e.g. /net/tcp/1/).
 *
 * Returns the ctl FD, or -1 on error.  err_func is the function name to print
 * for error output. */
static int __clone9(char *netdir, char *dir, char *err_func, int flags)
{
	int ctl, n, m;
	char buf[Maxpath];
	char *cp;

	/* get a control channel */
	ctl = open(netdir, O_RDWR);
	if (ctl < 0) {
		fprintf(stderr, "%s opening %s: %r\n", err_func, netdir);
		return -1;
	}
	cp = strrchr(netdir, '/');
	if (cp == NULL) {
		fprintf(stderr, "%s arg format %s\n", err_func, netdir);
		close(ctl);
		return -1;
	}
	*cp = 0;

	/* find out which line we have */
	n = snprintf(buf, sizeof(buf), "%s/", netdir);
	m = read(ctl, &buf[n], sizeof(buf) - n - 1);
	if (m <= 0) {
		fprintf(stderr, "%s reading %s: %r\n", err_func, netdir);
		close(ctl);
		return -1;
	}
	buf[n + m] = 0;

	/* return directory etc. */
	if (dir) {
		strncpy(dir, buf, NETPATHLEN);
		dir[NETPATHLEN - 1] = 0;
	}
	return ctl;
}

/* Clones a new network connection for a given dialstring (e.g. tcp!*!22). */
int clone9(char *addr, char *dir, int flags)
{
	char netdir[Maxpath];
	char naddr[Maxpath];

	if (nettrans(addr, naddr, sizeof(naddr), netdir, sizeof(netdir)) < 0)
		return -1;
	return __clone9(addr, dir, "clone", flags);
}

/*
 *  announce a network service.
 */
int announce9(char *addr, char *dir, int flags)
{
	int ctl, n;
	char buf[Maxpath];
	char netdir[Maxpath];
	char naddr[Maxpath];

	/*
	 *  translate the address
	 */
	if (nettrans(addr, naddr, sizeof(naddr), netdir, sizeof(netdir)) < 0)
		return -1;

	ctl = __clone9(netdir, dir, "announce", flags);
	if (ctl < 0)
		return -1;

	/*
	 *  make the call
	 */
	n = snprintf(buf, sizeof(buf), "announce %s", naddr);
	if (write(ctl, buf, n) != n) {
		fprintf(stderr, "announce writing %s: %r\n", netdir);
		close(ctl);
		return -1;
	}

	return ctl;
}

/* Gets a conversation and bypasses the protocol layer */
int bypass9(char *addr, char *conv_dir, int flags)
{
	int ctl, n;
	char buf[Maxpath];
	char netdir[Maxpath];
	char naddr[Maxpath];

	if (nettrans(addr, naddr, sizeof(naddr), netdir, sizeof(netdir)) < 0)
		return -1;
	ctl = __clone9(netdir, conv_dir, "bypass", flags);
	if (ctl < 0)
		return -1;
	n = snprintf(buf, sizeof(buf), "bypass %s", naddr);
	if (write(ctl, buf, n) != n) {
		fprintf(stderr, "bypass writing %s: %r\n", netdir);
		close(ctl);
		return -1;
	}
	return ctl;
}

/*
 *  listen for an incoming call
 */
int listen9(char *dir, char *newdir, int flags)
{
	int ctl, n, m;
	char buf[Maxpath];
	char *cp;

	/*
	 *  open listen, wait for a call
	 */
	snprintf(buf, sizeof(buf), "%s/listen", dir);
	ctl = open(buf, O_RDWR | (flags & O_NONBLOCK));
	if (ctl < 0) {
		if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			fprintf(stderr, "listen opening %s: %r\n", buf);
		return -1;
	}

	/*
	 *  find out which line we have
	 */
	strncpy(buf, dir, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	cp = strrchr(buf, '/');
	if (cp == NULL) {
		close(ctl);
		fprintf(stderr, "listen arg format %s\n", dir);
		return -1;
	}
	*++cp = 0;
	n = cp - buf;
	m = read(ctl, cp, sizeof(buf) - n - 1);
	if (m <= 0) {
		close(ctl);
		fprintf(stderr, "listen reading %s/listen: %r\n", dir);
		return -1;
	}
	buf[n + m] = 0;

	/*
	 *  return directory etc.
	 */
	if (newdir) {
		strncpy(newdir, buf, NETPATHLEN);
		newdir[NETPATHLEN - 1] = 0;
	}
	return ctl;
}

/*
 *  accept a call, return an fd to the open data file
 */
int accept9(int ctl, char *dir)
{
	char buf[Maxpath];
	char *num;
	long n;

	num = strrchr(dir, '/');
	if (num == NULL)
		num = dir;
	else
		num++;

	n = snprintf(buf, sizeof(buf), "accept %s", num);
	/* ignore return value, network might not need accepts */
	write(ctl, buf, n);

	snprintf(buf, sizeof(buf), "%s/data", dir);
	return open(buf, O_RDWR);
}

/*
 *  reject a call, tell device the reason for the rejection
 */
int reject9(int ctl, char *dir, char *cause)
{
	char buf[Maxpath];
	char *num;
	long n;

	num = strrchr(dir, '/');
	if (num == 0)
		num = dir;
	else
		num++;
	snprintf(buf, sizeof(buf), "reject %s %s", num, cause);
	n = strlen(buf);
	if (write(ctl, buf, n) != n)
		return -1;
	return 0;
}

/*
 *  perform the identity translation (in case we can't reach cs)
 */
static int identtrans(char *netdir, char *addr, char *naddr, int na, char *file,
                      int nf)
{
	char proto[Maxpath];
	char *p;

	/* parse the protocol */
	strncpy(proto, addr, sizeof(proto));
	proto[sizeof(proto) - 1] = 0;
	p = strchr(proto, '!');
	if (p)
		*p++ = 0;

	snprintf(file, nf, "%s/%s/clone", netdir, proto);
	strncpy(naddr, p, na);
	naddr[na - 1] = 0;

	return 1;
}

/*
 *  call up the connection server and get a translation
 */
static int nettrans(char *addr, char *naddr, int na, char *file, int nf)
{
	int i, fd;
	char buf[Maxpath];
	char netdir[Maxpath];
	char *p, *p2;
	long n;

	/*
	 *  parse, get network directory
	 */
	p = strchr(addr, '!');
	if (p == 0) {
		fprintf(stderr, "bad dial string: %s\n", addr);
		return -1;
	}
	if (*addr != '/') {
		strncpy(netdir, "/net", sizeof(netdir));
		netdir[sizeof(netdir) - 1] = 0;
	} else {
		for (p2 = p; *p2 != '/'; p2--)
			;
		i = p2 - addr;
		if (i == 0 || i >= sizeof(netdir)) {
			fprintf(stderr, "bad dial string: %s\n", addr);
			return -1;
		}
		strncpy(netdir, addr, i);
		netdir[i] = 0;
		addr = p2 + 1;
	}

	/*
	 *  ask the connection server
	 */
	snprintf(buf, sizeof(buf), "%s/cs", netdir);
	fd = open(buf, O_RDWR);
	if (fd < 0)
		return identtrans(netdir, addr, naddr, na, file, nf);
	if (write(fd, addr, strlen(addr)) < 0) {
		close(fd);
		return -1;
	}
	lseek(fd, 0, 0);
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;

	/*
	 *  parse the reply
	 */
	p = strchr(buf, ' ');
	if (p == 0)
		return -1;
	*p++ = 0;
	strncpy(naddr, p, na);
	naddr[na - 1] = 0;

	if (buf[0] == '/') {
		p = strchr(buf + 1, '/');
		if (p == NULL)
			p = buf;
		else
			p++;
	}
	snprintf(file, nf, "%s/%s", netdir, p);
	return 0;
}

int open_data_fd9(char *conv_dir, int flags)
{
	char path_buf[MAX_PATH_LEN];

	snprintf(path_buf, sizeof(path_buf), "%s/data", conv_dir);
	return open(path_buf, O_RDWR | flags);
}

/* Given a conversation directory, return the "remote" or "local" port, passed
 * as the string which.  Returns the port via *port and TRUE on success. */
bool get_port9(char *conv_dir, char *which, uint16_t *port)
{
	/* We don't have a MAX_DIALSTRING, but MAX_PATH_LEN should be enough. */
	char buf[MAX_PATH_LEN];
	int local_fd;
	int ret;
	char *p;

	snprintf(buf, sizeof(buf), "%s/%s", conv_dir, which);
	local_fd = open(buf, O_RDONLY);
	if (local_fd < 0)
		return FALSE;
	ret = read(local_fd, buf, sizeof(buf));
	close(local_fd);
	if (ret <= 0)
		return FALSE;
	p = strrchr(buf, '!');
	if (!p)
		return FALSE;
	p++;
	*port = atoi(p);
	return TRUE;
}
