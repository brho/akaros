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

struct ds;

static struct chan *call(char *unused_char_p_t, char *, struct ds *);
static void _dial_string_parse(char *unused_char_p_t, struct ds *);

enum {
	Maxstring = 128,
};

struct ds {
	char buf[Maxstring];		/* dist string */
	char *netdir;
	char *proto;
	char *rem;
	char *local;				/* other args */
	char *dir;
	struct chan **ctlp;
};

/*
 *  the dialstring is of the form '[/net/]proto!dest'
 */
struct chan *chandial(char *dest, char *local, char *dir, struct chan **ctlp)
{
	struct ds ds;
	char clone[Maxpath];

	ds.local = local;
	ds.dir = dir;
	ds.ctlp = ctlp;

	_dial_string_parse(dest, &ds);
	if (ds.netdir == 0)
		ds.netdir = "/net";

	/* no connection server, don't translate */
	snprintf(clone, sizeof(clone), "%s/%s/clone", ds.netdir, ds.proto);
	return call(clone, ds.rem, &ds);
}

static struct chan *call(char *clone, char *dest, struct ds *ds)
{
	ERRSTACK(2);
	int n;
	struct chan *dchan, *cchan;
	char name[Maxpath], data[Maxpath], *p;

	cchan = namec(clone, Aopen, ORDWR, 0);

	/* get directory name */
	if (waserror()) {
		cclose(cchan);
		nexterror();
	}
	n = cchan->dev->read(cchan, name, sizeof(name) - 1, 0);
	name[n] = 0;
	for (p = name; *p == ' '; p++) ;
	snprintf(name, sizeof(name), "%lud", strtol /*strtoul */ (p, 0, 0));
	p = strrchr(clone, '/');
	*p = 0;
	if (ds->dir)
		snprintf(ds->dir, Maxpath, "%s/%s", clone, name);
	snprintf(data, sizeof(data), "%s/%s/data", clone, name);

	/* connect */
	if (ds->local)
		snprintf(name, sizeof(name), "connect %s %s", dest, ds->local);
	else
		snprintf(name, sizeof(name), "connect %s", dest);
	cchan->dev->write(cchan, name, strlen(name), 0);

	/* open data connection */
	dchan = namec(data, Aopen, ORDWR, 0);
	if (ds->ctlp)
		*ds->ctlp = cchan;
	else
		cclose(cchan);
	poperror();
	return dchan;

}

/*
 *  parse a dial string
 */
static void _dial_string_parse(char *str, struct ds *ds)
{
	char *p, *p2;

	strncpy(ds->buf, str, Maxstring);
	ds->buf[Maxstring - 1] = 0;

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
