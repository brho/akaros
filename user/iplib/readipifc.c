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
#include <iplib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define NFIELD 200
#define nelem(x) (sizeof(x)/sizeof(x[0]))
static struct ipifc**
_readoldipifc(char *buf, struct ipifc **l, int index)
{
	char *f[NFIELD];
	int i, n;
	struct ipifc *ifc;
	struct iplifc *lifc, **ll;

	/* allocate new interface */
	*l = ifc = calloc(sizeof(struct ipifc), 1);
	if(ifc == NULL)
		return l;
	l = &ifc->next;
	ifc->index = index;

	n = tokenize(buf, f, NFIELD);
	if(n < 2)
		return l;

	strncpy(ifc->dev, f[0], sizeof ifc->dev);
	ifc->dev[sizeof(ifc->dev) - 1] = 0;
	ifc->mtu = strtoul(f[1], NULL, 10);

	ll = &ifc->lifc;
	for(i = 2; n-i >= 7; i += 7){
		/* allocate new local address */
		*ll = lifc = calloc(sizeof(struct iplifc), 1);
		ll = &lifc->next;

		parseip(lifc->ip, f[i]);
		parseipmask(lifc->mask, f[i+1]);
		parseip(lifc->net, f[i+2]);
		ifc->pktin = strtoul(f[i+3], NULL, 10);
		ifc->pktout = strtoul(f[i+4], NULL, 10);
		ifc->errin = strtoul(f[i+5], NULL, 10);
		ifc->errout = strtoul(f[i+6], NULL, 10);
	}
	return l;
}

static char*
findfield(char *name, char **f, int n)
{
	int i;

	for(i = 0; i < n-1; i++)
		if(strcmp(f[i], name) == 0)
			return f[i+1];
	return "";
}

static struct ipifc**
_readipifc(char *file, struct ipifc **l, int index)
{
	int i, n, fd, lines;
	char buf[4*1024];
	char *line[32];
	char *f[64];
	struct ipifc *ifc, **l0;
	struct iplifc *lifc, **ll;

	/* read the file */
	fd = open(file, O_RDONLY);
	if(fd < 0)
		return l;
	n = 0;
	while((i = read(fd, buf+n, sizeof(buf)-1-n)) > 0 && n < sizeof(buf) - 1)
		n += i;
	buf[n] = 0;
	close(fd);

	if(strncmp(buf, "device", 6) != 0)
		return _readoldipifc(buf, l, index);
	/* ignore ifcs with no associated device */
	if(strncmp(buf+6, "  ", 2) == 0)
		return l;
	/* allocate new interface */
	*l = ifc = calloc(sizeof(struct ipifc), 1);
	if(ifc == NULL)
		return l;
	l0 = l;
	l = &ifc->next;
	ifc->index = index;

	lines = getfields(buf, line, nelem(line), 1, "\n");

	/* pick off device specific info(first line) */
	n = tokenize(line[0], f, nelem(f));
	if(n%2 != 0)
		goto lose;
	strncpy(ifc->dev, findfield("device", f, n), sizeof(ifc->dev));
	ifc->dev[sizeof(ifc->dev)-1] = 0;
	if(ifc->dev[0] == 0){
lose:
		free(ifc);
		*l0 = NULL;
		return l;
	}
	ifc->mtu = strtoul(findfield("maxtu", f, n), NULL, 10);
	ifc->sendra6 = atoi(findfield("sendra", f, n));
	ifc->recvra6 = atoi(findfield("recvra", f, n));
	ifc->rp.mflag = atoi(findfield("mflag", f, n));
	ifc->rp.oflag = atoi(findfield("oflag", f, n));
	ifc->rp.maxraint = atoi(findfield("maxraint", f, n));
	ifc->rp.minraint = atoi(findfield("minraint", f, n));
	ifc->rp.linkmtu = atoi(findfield("linkmtu", f, n));
	ifc->rp.reachtime = atoi(findfield("reachtime", f, n));
	ifc->rp.rxmitra = atoi(findfield("rxmitra", f, n));
	ifc->rp.ttl = atoi(findfield("ttl", f, n));
	ifc->rp.routerlt = atoi(findfield("routerlt", f, n));
	ifc->pktin = strtoul(findfield("pktin", f, n), NULL, 10);
	ifc->pktout = strtoul(findfield("pktout", f, n), NULL, 10);
	ifc->errin = strtoul(findfield("errin", f, n), NULL, 10);
	ifc->errout = strtoul(findfield("errout", f, n), NULL, 10);

	/* now read the addresses */
	ll = &ifc->lifc;
	for(i = 1; i < lines; i++){
		n = tokenize(line[i], f, nelem(f));
		if(n < 5)
			break;

		/* allocate new local address */
		*ll = lifc = calloc(sizeof(struct iplifc), 1);
		ll = &lifc->next;

		parseip(lifc->ip, f[0]);
		parseipmask(lifc->mask, f[1]);
		parseip(lifc->net, f[2]);

		lifc->validlt = strtoul(f[3], NULL, 10);
		lifc->preflt = strtoul(f[4], NULL, 10);
	}

	return l;
}

static void
_freeifc(struct ipifc *ifc)
{
	struct ipifc *next;
	struct iplifc *lnext, *lifc;

	if(ifc == NULL)
		return;
	for(; ifc; ifc = next){
		next = ifc->next;
		for(lifc = ifc->lifc; lifc; lifc = lnext){
			lnext = lifc->next;
			free(lifc);
		}
		free(ifc);
	}
}

struct ipifc*
readipifc(char *net, struct ipifc *ifc, int index)
{
	int fd, i, n;
	struct dir *dir;
	char directory[128];
	char buf[128];
	struct ipifc **l;

	_freeifc(ifc);

	l = &ifc;
	ifc = NULL;

	if(net == 0)
		net = "/net";
	snprintf(directory, sizeof(directory), "%s/ipifc", net);

	if(index >= 0){
		snprintf(buf, sizeof(buf), "%s/%d/status", directory, index);
		_readipifc(buf, l, index);
	} else {
		DIR *d;
		struct dirent *de;
		d = opendir(directory);
		if(! d)
			return NULL;
		
		while (de = readdir(d)){
			if(strcmp(de->d_name, "clone") == 0)
				continue;
			if(strcmp(de->d_name, "stats") == 0)
				continue;
			snprintf(buf, sizeof(buf), "%s/%s/status", directory, de->d_name);
			l = _readipifc(buf, l, atoi(de->d_name));
		}
		closedir(d);
	}
	
	return ifc;
}
