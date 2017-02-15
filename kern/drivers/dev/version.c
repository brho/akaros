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
	Kverbuildid,
	Kverdate,
	Kvercommitid,
	Kverversion,
	Kverversionname,
	Kverkconfig,
	BUILD_ID_SZ = 20,
	BUILD_ID_OFFSET = 16,
};

struct dev verdevtab;
static struct dirtab vertab[] = {
	{".",				{Kverdirqid,		0, QTDIR}, 0,	DMDIR|0550},
	{"build_id",		{Kverbuildid},		0,	0444},
	{"date",			{Kverdate},			0,	0444},
	{"commitid",		{Kvercommitid},		0,	0444},
	{"version",			{Kverversion},		0,	0444},
	{"version_name",	{Kverversionname},	0,	0444},
	{"kconfig",			{Kverkconfig},		0,	0444},
};

extern char __note_build_id_start[];
extern char __note_build_id_end[];

extern const char *__kconfig_str;

static char *get_build_id_start(void)
{
	return __note_build_id_start + BUILD_ID_OFFSET;
}

static size_t build_id_sz(void)
{
	return __note_build_id_end - get_build_id_start();
}

static long ver_emit_nlstr(char *dest, const char *src, long size,
						   long offset)
{
	long n, slen = strlen(src);
	char *buf = kmalloc(slen + 1, MEM_WAIT);

	snprintf(buf, slen + 1, "%s", src);
	n = readmem(offset, dest, size, buf, slen + 1);
	kfree(buf);

	return n;
}

static size_t ver_get_file_size(const char *src)
{
	if (!src)
		return 0;
	return strlen(src) + 1;
}

static struct chan *ver_attach(char *spec)
{
	return devattach(verdevtab.name, spec);
}

static void ver_init(void)
{
	/* Our devtab's length params are wrong - need to stitch them up. */
	vertab[Kverbuildid].length = build_id_sz();
	vertab[Kverdate].length = ver_get_file_size(build_info_date);
	vertab[Kvercommitid].length = ver_get_file_size(build_info_commitid);
	vertab[Kverversion].length = ver_get_file_size(build_info_version);
	vertab[Kverversionname].length = ver_get_file_size(build_info_version_name);
	vertab[Kverkconfig].length = strlen(__kconfig_str) + 1;
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

/* Returns a char representing the lowest 4 bits of x */
static char num_to_nibble(unsigned int x)
{
	return "0123456789abcdef"[x & 0xf];
}

static ssize_t read_buildid(void *va, long n, off64_t off)
{
	/* Each build_id byte needs 2 chars, and 1 for the \0 */
	char build_id[BUILD_ID_SZ * 2 + 1] = {0};
	uint8_t hi, lo;
	uint8_t *b = (uint8_t*)get_build_id_start();

	for (int i = 0; i < BUILD_ID_SZ; i++) {
		hi = *b >> 4;
		lo = *b & 0xf;
		build_id[i * 2 + 0] = num_to_nibble(hi);
		build_id[i * 2 + 1] = num_to_nibble(lo);
		b++;
	}
	return readmem(off, va, n, build_id, sizeof(build_id));
}

static long ver_read(struct chan *c, void *va, long n, int64_t off)
{
	switch ((int) c->qid.path) {
	case Kverdirqid:
		return devdirread(c, va, n, vertab, ARRAY_SIZE(vertab), devgen);
	case Kverbuildid:
		return read_buildid(va, n, off);
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
	case Kverkconfig:
		return readstr(off, va, n, __kconfig_str);
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
