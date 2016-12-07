/*
 * ttcp. Modelled after the unix ttcp
 *
 * Copyright (c) 2012, Bakul Shah <bakul@bitblocks.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The author's name may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * This software is provided by the author AS IS.  The author DISCLAIMS
 * any and all warranties of merchantability and fitness for a particular
 * purpose.  In NO event shall the author be LIABLE for any damages
 * whatsoever arising in any way out of the use of this software.
 */

/*
 * Options not supported (may be supported in future):
 * +	-u	Use UDP instead of TCP
 *	-D	don't want for TCP send (TCP_NODELAY)
 *	-A num	align buffers on this boundary (default 16384)
 *	-O off	start buffers at this offset (default 0)
 * Misc:
 *	- print calls, msec/call calls/sec
 *	- print user/sys/real times
 * May be:
 *	- multicast support
 *	- isochronous transfer
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <parlib/net.h>
#include <sys/time.h>
#include <iplib/iplib.h>
#include <parlib/timing.h>

long ncalls;
char scale;

static void sysfatal(char *msg)
{
	perror(msg);
	exit(-1);
}

long nread(int fd, char *buf, long len)
{
	int cnt, rlen = 0;
	char *b = buf;
	for (;;) {
		cnt = read(fd, b, len);
		ncalls++;
		if (cnt <= 0)
			break;
		rlen += cnt;
		len -= cnt;
		if (len == 0)
			break;
		b += cnt;
	}
	return rlen;
}

long nwrite(int fd, char *buf, long len)
{
	int cnt, rlen = 0;
	char *b = buf;
	for (;;) {
		cnt = write(fd, b, len);
		ncalls++;
		if (cnt <= 0)
			break;
		rlen += cnt;
		len -= cnt;
		if (len == 0)
			break;
		b += cnt;
	}
	return rlen;
}

void pattern(char *buf, int buflen)
{
	int i;
	char ch = ' ';
	char *b = buf;
	for (i = 0; i < buflen; i++) {
		*b++ = ch++;
		if (ch == 127)
			ch = ' ';
	}
}

char fmt = 'K';
char *unit;

double rate(long nbytes, double time)
{
	switch (fmt) {
	case 'k':
		unit = "Kbit";
		return nbytes * 8 / time / (1 << 10);
	case 'K':
		unit = "KB";
		return nbytes / time / (1 << 10);
	case 'm':
		unit = "Mbit";
		return nbytes * 8 / time / (1 << 20);
	case 'M':
		unit = "MB";
		return nbytes / time / (1 << 20);
	case 'g':
		unit = "Gbit";
		return nbytes * 8 / time / (1 << 30);
	case 'G':
		unit = "GB";
		return nbytes / time / (1 << 30);
	}
	return 0.0;
}

void reader(int udp, char *addr, char *port, int buflen, int nbuf, int sink)
{
	char *buf, adir[40], ldir[40];
	int fd, cnt, acfd, lcfd;
	long nbytes = 0;
	long now;
	double elapsed;
	int pd;
	char peer[100];

	fprintf(stderr, "ttcp-r: buflen=%d, nbuf=%d, port=%s %s\n",
		buflen, nbuf, port, udp ? "udp" : "tcp");

	acfd = announce9(netmkaddr(addr, udp ? "udp" : "tcp", port), adir, 0);
	if (acfd < 0)
		sysfatal("announce: %r");
	buf = malloc(buflen);

	lcfd = listen9(adir, ldir, 0);
	if (lcfd < 0)
		sysfatal("listen: %r");

	fd = accept9(lcfd, ldir);
	if (fd < 0)
		return;

	sprintf(peer, "%s/remote", ldir);
	pd = open(peer, O_READ);
	cnt = read(pd, peer, 100);
	close(pd);

	fprintf(stderr, "ttcp-r: accept from %*.*s", cnt, cnt, peer);
	now = nsec();
	if (sink) {
		while ((cnt = nread(fd, buf, buflen)) > 0)
			nbytes += cnt;
	} else {
		while ((cnt = nread(fd, buf, buflen)) > 0 && write(1, buf, cnt) == cnt)
			nbytes += cnt;
	}
	elapsed = (nsec() - now) / 1E9;

	fprintf(stderr, "ttcp-r: %lld bytes in %.2f real seconds = %.2f %s/sec\n",
	        nbytes, elapsed, rate(nbytes, elapsed), unit);
}

void writer(int udp, char *addr, char *port, int buflen, int nbuf, int src)
{
	char *buf;
	int fd, cnt;
	long nbytes = 0;
	long now;
	double elapsed;
	char netaddr[128];

	fprintf(stderr, "ttcp-t: buflen=%d, nbuf=%d, port=%s %s -> %s\n",
		    buflen, nbuf, port, udp ? "udp" : "tcp", addr);

	buf = malloc(buflen);
	snprintf(netaddr, sizeof(netaddr), "%s!%s!%s",
		 udp ? "udp" : "tcp", addr, port);
	fprintf(stderr, "dialing %s\n", netaddr);
	fd = dial9(netaddr, 0, 0, 0, 0);
	if (fd < 0)
		sysfatal("dial: %r");

	fprintf(stderr, "ttcp-t: connect\n");

	now = nsec();
	if (src) {
		pattern(buf, buflen);
		while (nbuf-- && nwrite(fd, buf, buflen) == buflen)
			nbytes += buflen;
	} else {
		while ((cnt = read(0, buf, buflen)) > 0 && nwrite(fd, buf, cnt) == cnt)
			nbytes += cnt;
	}
	elapsed = (nsec() - now) / 1E9;

	fprintf(stderr, "ttcp-t: %lld bytes in %.2f real seconds = %.2f %s/sec\n",
	        nbytes, elapsed, rate(nbytes, elapsed), unit);
}

void usage(void)
{
	fprintf(stderr, "usage:\tttcp -t [options] host\n"
	      "\t\tttcp -r [options]\n"
	      " options:\n"
	      "  -f fmt\trate format: k,m,g,K,M,G = {kilo,mega,giga}{bit,byte}\n"
	      "  -l\t\tlength of buf (default 8192)\n"
	      "  -p port\tport number (default 5001)\n"
	      "  -n num\tnumber of bufs written (default 2048)\n"
	      "  -s\t\t-t: source a pattern to network\n"
	      "\t\t-r: sink (discard) all data from network\n"
	      );
	exit(0);
}

void main(int argc, char *argv[])
{
	int buflen = 8192;
	int nbuf = 2048;
	int srcsink = 0;
	char *port = "5001";
	int udp = 0;
	enum { none, recv, xmit } mode = none;
	char c;

	while ((c = getopt(argc, argv, "rstuf:l:n:p:")) != -1) {
		switch (c) {
		case 'f':
			fmt = *optarg;
			break;
		case 'l':
			buflen = atoi(optarg);
			break;
		case 'n':
			nbuf = atoi(optarg);
			break;
		case 'p':
			port = optarg;
			break;
		case 'r':
			mode = recv;
			break;
		case 's':
			srcsink = 1;
			break;
		case 't':
			mode = xmit;
			break;
		case 'u':
			udp = 1;
			break;
		default:
			usage();
		}
	}
	switch (mode) {
	case none:
		usage();
		break;
	case xmit:
		if (optind == argc)
			usage();
		writer(udp, argv[optind], port, buflen, nbuf, srcsink);
		break;
	case recv:
		reader(udp, "*", port, buflen, nbuf, srcsink);
		break;
	}
	exit(0);
}
