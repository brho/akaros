/* Copyright © 1994-1999 Lucent Technologies Inc.  All rights reserved.
 * Portions Copyright © 1997-1999 Vita Nuova Limited
 * Portions Copyright © 2000-2007 Vita Nuova Holdings Limited
 *                                (www.vitanuova.com)
 * Revisions Copyright © 2000-2007 Lucent Technologies Inc. and others
 *
 * Modified for the Akaros operating system:
 * Copyright (c) 2013-2014 The Regents of the University of California
 * Copyright (c) 2013-2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>

typedef struct DS DS;

static int call(char *cp, char *cp1, DS * DS);
static int csdial(DS * DS);
static void _dial_string_parse(char *cp, DS * DS);
static int nettrans(char *cp, char *cp1, int na, char *cp2, int i);

enum {
	Maxstring = 128,
};

struct DS {
	char buf[Maxstring];		/* dist string */
	char *netdir;
	char *proto;
	char *rem;
	char *local;				/* other args */
	char *dir;
	int *cfdp;
};

/* only used here for now. */
static void kerrstr(void *err, int len)
{
	strlcpy(err, current_errstr(), len);
}

/*
 *  the dialstring is of the form '[/net/]proto!dest'
 */
int kdial(char *dest, char *local, char *dir, int *cfdp)
{
	DS ds;
	int rv;
	char *err, *alterr;

	err = kmalloc(ERRMAX, MEM_WAIT);
	alterr = kmalloc(ERRMAX, MEM_WAIT);

	ds.local = local;
	ds.dir = dir;
	ds.cfdp = cfdp;

	_dial_string_parse(dest, &ds);
	if (ds.netdir) {
		rv = csdial(&ds);
		goto out;
	}

	ds.netdir = "/net";
	rv = csdial(&ds);
	if (rv >= 0)
		goto out;

	err[0] = 0;
	strlcpy(err, current_errstr(), ERRMAX);
	if (strstr(err, "refused") != 0) {
		goto out;
	}

	ds.netdir = "/net.alt";
	rv = csdial(&ds);
	if (rv >= 0)
		goto out;

	alterr[0] = 0;
	kerrstr(alterr, ERRMAX);

	if (strstr(alterr, "translate") || strstr(alterr, "does not exist"))
		kerrstr(err, ERRMAX);
	else
		kerrstr(alterr, ERRMAX);
out:
	kfree(err);
	kfree(alterr);
	return rv;
}

static int csdial(DS * ds)
{
	int n, fd, rv = -1;
	char *p, *buf, *clone, *err, *besterr;

	buf = kmalloc(Maxstring, MEM_WAIT);
	clone = kmalloc(Maxpath, MEM_WAIT);
	err = kmalloc(ERRMAX, MEM_WAIT);
	besterr = kmalloc(ERRMAX, MEM_WAIT);
	/*
	 *  open connection server
	 */
	snprintf(buf, Maxstring, "%s/cs", ds->netdir);
	fd = sysopen(buf, O_RDWR);
	if (fd < 0) {
		/* no connection server, don't translate */
		snprintf(clone, Maxpath, "%s/%s/clone", ds->netdir, ds->proto);
		rv = call(clone, ds->rem, ds);
		goto out;
	}

	/*
	 *  ask connection server to translate
	 */
	snprintf(buf, Maxstring, "%s!%s", ds->proto, ds->rem);
	if (syswrite(fd, buf, strlen(buf)) < 0) {
		kerrstr(err, ERRMAX);
		sysclose(fd);
		set_errstr("%s (%s)", err, buf);
		goto out;
	}

	/*
	 *  loop through each address from the connection server till
	 *  we get one that works.
	 */
	*besterr = 0;
	strlcpy(err, "csdial() connection reset", ERRMAX);
	sysseek(fd, 0, 0);
	while ((n = sysread(fd, buf, Maxstring - 1)) > 0) {
		buf[n] = 0;
		p = strchr(buf, ' ');
		if (p == 0)
			continue;
		*p++ = 0;
		rv = call(buf, p, ds);
		if (rv >= 0)
			break;
		err[0] = 0;
		kerrstr(err, ERRMAX);
		if (strstr(err, "does not exist") == 0)
			memmove(besterr, err, ERRMAX);
	}
	sysclose(fd);

	if (rv < 0 && *besterr)
		kerrstr(besterr, ERRMAX);
	else
		kerrstr(err, ERRMAX);
out:
	kfree(buf);
	kfree(clone);
	kfree(err);
	kfree(besterr);
	return rv;
}

static int call(char *clone, char *dest, DS * ds)
{
	int fd, cfd, n, retval;
	char *name, *data, *err, *p;

	name = kmalloc(Maxpath, MEM_WAIT);
	data = kmalloc(Maxpath, MEM_WAIT);
	err = kmalloc(ERRMAX, MEM_WAIT);

	cfd = sysopen(clone, O_RDWR);
	if (cfd < 0) {
		kerrstr(err, ERRMAX);
		set_errstr("%s (%s)", err, clone);
		retval = -1;
		goto out;
	}

	/* get directory name */
	n = sysread(cfd, name, Maxpath - 1);
	if (n < 0) {
		kerrstr(err, ERRMAX);
		sysclose(cfd);
		set_errstr("read %s: %s", clone, err);
		retval = -1;
		goto out;
	}
	name[n] = 0;
	for (p = name; *p == ' '; p++) ;
	snprintf(name, Maxpath, "%ld", strtoul(p, 0, 0));
	p = strrchr(clone, '/');
	*p = 0;
	if (ds->dir)
		snprintf(ds->dir, NETPATHLEN, "%s/%s", clone, name);
	snprintf(data, Maxpath, "%s/%s/data", clone, name);

	/* connect */
	if (ds->local)
		snprintf(name, Maxpath, "connect %s %s", dest, ds->local);
	else
		snprintf(name, Maxpath, "connect %s", dest);
	if (syswrite(cfd, name, strlen(name)) < 0) {
		err[0] = 0;
		kerrstr(err, ERRMAX);
		sysclose(cfd);
		set_errstr("%s (%s)", err, name);
		retval = -1;
		goto out;
	}

	/* open data connection */
	fd = sysopen(data, O_RDWR);
	if (fd < 0) {
		err[0] = 0;
		kerrstr(err, ERRMAX);
		set_errstr("%s (%s)", err, data);
		sysclose(cfd);
		retval = -1;
		goto out;
	}
	if (ds->cfdp)
		*ds->cfdp = cfd;
	else
		sysclose(cfd);
	retval = fd;
out:
	kfree(name);
	kfree(data);
	kfree(err);

	return retval;
}

/*
 *  parse a dial string
 */
static void _dial_string_parse(char *str, DS * ds)
{
	char *p, *p2;

	strlcpy(ds->buf, str, Maxstring);

	p = strchr(ds->buf, '!');
	if (p == 0) {
		ds->netdir = 0;
		ds->proto = "net";
		ds->rem = ds->buf;
	} else {
		if (*ds->buf != '/' && *ds->buf != '#') {
			ds->netdir = 0;
			ds->proto = ds->buf;
		} else {
			for (p2 = p; *p2 != '/'; p2--) ;
			*p2++ = 0;
			ds->netdir = ds->buf;
			ds->proto = p2;
		}
		*p = 0;
		ds->rem = p + 1;
	}
}

/*
 *  announce a network service.
 */
int kannounce(char *addr, char *dir, size_t dirlen)
{
	int ctl, n, m;
	char buf[NETPATHLEN];
	char buf2[Maxpath];
	char netdir[NETPATHLEN];
	char naddr[Maxpath];
	char *cp;

	/*
	 *  translate the address
	 */
	if (nettrans(addr, naddr, sizeof(naddr), netdir, sizeof(netdir)) < 0)
		return -1;

	/*
	 * get a control channel
	 */
	ctl = sysopen(netdir, O_RDWR);
	if (ctl < 0)
		return -1;
	cp = strrchr(netdir, '/');
	*cp = 0;

	/*
	 *  find out which line we have
	 */
	n = snprintf(buf, sizeof(buf), "%.*s/", sizeof buf, netdir);
	m = sysread(ctl, &buf[n], sizeof(buf) - n - 1);
	if (m <= 0) {
		sysclose(ctl);
		return -1;
	}
	buf[n + m] = 0;

	/*
	 *  make the call
	 */
	n = snprintf(buf2, sizeof buf2, "announce %s", naddr);
	if (syswrite(ctl, buf2, n) != n) {
		sysclose(ctl);
		return -1;
	}

	/*
	 *  return directory etc.
	 */
	if (dir)
		strlcpy(dir, buf, dirlen);
	return ctl;
}

/*
 *  listen for an incoming call
 */
int klisten(char *dir, char *newdir, size_t newdirlen)
{
	int ctl, n, m;
	char buf[NETPATHLEN + 1];
	char *cp;

	/*
	 *  open listen, wait for a call
	 */
	snprintf(buf, sizeof buf, "%s/listen", dir);
	ctl = sysopen(buf, O_RDWR);
	if (ctl < 0)
		return -1;

	/*
	 *  find out which line we have
	 */
	strlcpy(buf, dir, sizeof(buf));
	cp = strrchr(buf, '/');
	*++cp = 0;
	n = cp - buf;
	m = sysread(ctl, cp, sizeof(buf) - n - 1);
	if (m <= 0) {
		sysclose(ctl);
		return -1;
	}
	buf[n + m] = 0;

	/*
	 *  return directory etc.
	 */
	if (newdir)
		strlcpy(newdir, buf, newdirlen);
	return ctl;

}

/*
 *  perform the identity translation (in case we can't reach cs)
 */
static int
identtrans(char *netdir, char *addr, char *naddr, int na, char *file, int nf)
{
	char proto[Maxpath];
	char *p;

	/* parse the protocol */
	strlcpy(proto, addr, sizeof(proto));
	p = strchr(proto, '!');
	if (p)
		*p++ = 0;

	snprintf(file, nf, "%s/%s/clone", netdir, proto);
	strlcpy(naddr, p, na);

	return 1;
}

/*
 *  call up the connection server and get a translation
 */
static int nettrans(char *addr, char *naddr, int na, char *file, int nf)
{
	int i, fd;
	char buf[Maxpath];
	char netdir[NETPATHLEN];
	char *p, *p2;
	long n;

	/*
	 *  parse, get network directory
	 */
	p = strchr(addr, '!');
	if (p == 0) {
		set_errstr("bad dial string: %s", addr);
		return -1;
	}
	if (*addr != '/') {
		strlcpy(netdir, "/net", sizeof(netdir));
	} else {
		for (p2 = p; *p2 != '/'; p2--) ;
		i = p2 - addr;
		if (i == 0 || i >= sizeof(netdir)) {
			set_errstr("bad dial string: %s", addr);
			return -1;
		}
		strlcpy(netdir, addr, i + 1);
		addr = p2 + 1;
	}

	/*
	 *  ask the connection server
	 */
	snprintf(buf, sizeof(buf), "%s/cs", netdir);
	fd = sysopen(buf, O_RDWR);
	if (fd < 0)
		return identtrans(netdir, addr, naddr, na, file, nf);
	if (syswrite(fd, addr, strlen(addr)) < 0) {
		sysclose(fd);
		return -1;
	}
	sysseek(fd, 0, 0);
	n = sysread(fd, buf, sizeof(buf) - 1);
	sysclose(fd);
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
	strlcpy(naddr, p, na);
	strlcpy(file, buf, nf);
	return 0;
}
