/* Copyright (c) 2015 Google Inc
 * Davide Libenzi <dlibenzi@google.com>
 * See LICENSE for details.
 */

#include <ros/common.h>
#include <ros/errno.h>
#include <smp.h>
#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <err.h>
#include <build_info.h>

enum {
	Kverdirqid = 0,
	Kverdate,
	Kvercommitid,
	Kverversion,
	Kverversionname,
};

struct dev verdevtab;
static struct dirtab vertab[] = {
	{".",				{Kverdirqid,		0, QTDIR}, 0,	DMDIR|0550},
	{"date",			{Kverdate},			0,	0444},
	{"commitid",		{Kvercommitid},		0,	0444},
	{"version",			{Kverversion},		0,	0444},
	{"version_name",	{Kverversionname},	0,	0444},
};

static long ver_emit_nlstr(char *dest, const char *src, long size,
						   long offset)
{
	long n, slen = strlen(src);
	char *buf = kmalloc(slen + 2, MEM_WAIT);

	snprintf(buf, slen + 2, "%s\n", src);
	n = readmem(offset, dest, size, buf, slen + 2);
	kfree(buf);

	return n;
}

static struct chan *ver_attach(char *spec)
{
	return devattach(verdevtab.name, spec);
}

static void ver_init(void)
{

}

static void ver_shutdown(void)
{

}

static struct walkqid *ver_walk(struct chan *c, struct chan *nc, char **name,
								 int nname)
{
	return devwalk(c, nc, name, nname, vertab, ARRAY_SIZE(vertab), devgen);
}

static int ver_stat(struct chan *c, uint8_t *db, int n)
{
	return devstat(c, db, n, vertab, ARRAY_SIZE(vertab), devgen);
}

static struct chan *ver_open(struct chan *c, int omode)
{
	if (c->qid.type & QTDIR) {
		if (openmode(omode) != O_READ)
			error(EPERM, ERROR_FIXME);
	}
	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void ver_close(struct chan *c)
{

}

static long ver_read(struct chan *c, void *va, long n, int64_t off)
{
	switch ((int) c->qid.path) {
	case Kverdirqid:
		return devdirread(c, va, n, vertab, ARRAY_SIZE(vertab), devgen);
	case Kverdate:
		if (build_info_date)
			return ver_emit_nlstr(va, build_info_date, n, (long) off);
		break;
	case Kvercommitid:
		if (build_info_commitid)
			return ver_emit_nlstr(va, build_info_commitid, n, (long) off);
		break;
	case Kverversion:
		if (build_info_version)
			return ver_emit_nlstr(va, build_info_version, n, (long) off);
		break;
	case Kverversionname:
		if (build_info_version_name)
			return ver_emit_nlstr(va, build_info_version_name, n, (long) off);
		break;
	default:
		error(EINVAL, ERROR_FIXME);
	}

	return 0;
}

static long ver_write(struct chan *c, void *a, long n, int64_t unused)
{
	error(ENOTSUP, ERROR_FIXME);
	return -1;
}

struct dev verdevtab __devtab = {
	.name = "version",

	.reset = devreset,
	.init = ver_init,
	.shutdown = ver_shutdown,
	.attach = ver_attach,
	.walk = ver_walk,
	.stat = ver_stat,
	.open = ver_open,
	.create = devcreate,
	.close = ver_close,
	.read = ver_read,
	.bread = devbread,
	.write = ver_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
};
