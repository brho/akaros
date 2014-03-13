/* 
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */
#include <stdlib.h>

#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <iplib.h>
#include <ndb.h>

static void nstrcpy(char*, char*, int);
static void mkptrname(char*, char*, int);
static struct ndbtuple *doquery(int, char *dn, char *type);

/*
 *  search for a tuple that has the given 'attr=val' and also 'rattr=x'.
 *  copy 'x' into 'buf' and return the whole tuple.
 *
 *  return 0 if not found.
 */
struct ndbtuple*
dnsquery(char *net, char *val, char *type)
{
	char rip[128];
	char *p;
	struct ndbtuple *t;
	int fd;

	/* if the address is V4 or V6 null address, give up early */
	if(strcmp(val, "::") == 0 || strcmp(val, "0.0.0.0") == 0)
		return NULL;

	if(net == NULL)
		net = "/net";
	snprintf(rip, sizeof(rip), "%s/dns", net);
	fd = open(rip, O_RDWR);
	if(fd < 0){
		if(strcmp(net, "/net") == 0)
			snprintf(rip, sizeof(rip), "/srv/dns");
		else {
			snprintf(rip, sizeof(rip), "/srv/dns%s", net);
			p = strrchr(rip, '/');
			*p = '_';
		}
		fd = open(rip, O_RDWR);
		if(fd < 0)
			return NULL;
#if 0
		if(mount(fd, -1, net, MBEFORE, "") < 0){
			close(fd);
			return NULL;
		}
#else
#define MBEFORE 1
#define NOAUTHFD -1
		int ret;
		ret = syscall(SYS_nmount, fd, NOAUTHFD, net, MBEFORE, "");
		if (ret < 0){
			close(fd);
			return NULL;
		}
#endif
		/* fd is now closed */
		snprintf(rip, sizeof(rip), "%s/dns", net);
		fd = open(rip, O_RDWR);
		if(fd < 0)
			return NULL;
	}

	/* zero out the error string */
	werrstr("");

	/* if this is a reverse lookup, first lookup the domain name */
	if(strcmp(type, "ptr") == 0){
		mkptrname(val, rip, sizeof rip);
		t = doquery(fd, rip, "ptr");
	} else
		t = doquery(fd, val, type);

	/*
	 * TODO: make fd static and keep it open to reduce 9P traffic
	 * walking to /net*^/dns.  Must be prepared to re-open it on error.
	 */
	close(fd);
	ndbsetmalloctag(t, getcallerpc(&net));
	return t;
}

/*
 *  convert address into a reverse lookup address
 */
static void
mkptrname(char *ip, char *rip, int rlen)
{
	char buf[128];
	char *p, *np;
	int len;

	if(strstr(ip, "in-addr.arpa") || strstr(ip, "IN-ADDR.ARPA")){
		nstrcpy(rip, ip, rlen);
		return;
	}

	nstrcpy(buf, ip, sizeof buf);
	for(p = buf; *p; p++)
		;
	*p = '.';
	np = rip;
	len = 0;
	while(p >= buf){
		len++;
		p--;
		if(*p == '.'){
			memmove(np, p+1, len);
			np += len;
			len = 0;
		}
	}
	memmove(np, p+1, len);
	np += len;
	strcpy(np, "in-addr.arpa");
}

static void
nstrcpy(char *to, char *from, int len)
{
	strncpy(to, from, len);
	to[len-1] = 0;
}

static struct ndbtuple*
doquery(int fd, char *dn, char *type)
{
	char buf[1024];
	int n;
	struct ndbtuple *t, *first, *last;

	lseek(fd, 0, 0);
	snprintf(buf, sizeof(buf), "!%s %s", dn, type);
	if(write(fd, buf, strlen(buf)) < 0)
		return NULL;
		
	lseek(fd, 0, 0);

	first = last = NULL;
	
	for(;;){
		n = read(fd, buf, sizeof(buf)-2);
		if(n <= 0)
			break;
		if(buf[n-1] != '\n')
			buf[n++] = '\n';	/* ndbparsline needs a trailing new line */
		buf[n] = 0;

		/* check for the error condition */
		if(buf[0] == '!'){
			werrstr("%s", buf+1);
			return NULL;
		}

		t = _ndbparseline(buf);
		if(t != NULL){
			if(first)
				last->entry = t;
			else
				first = t;
			last = t;

			while(last->entry)
				last = last->entry;
		}
	}

	ndbsetmalloctag(first, getcallerpc(&fd));
	return first;
}
