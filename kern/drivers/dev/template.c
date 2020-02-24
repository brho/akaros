/* Copyright (c) 2020 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * #template.  Dummy device with a devdir table.
 */

#include <ns.h>
#include <kmalloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>

struct dev template_devtab;

static char *devname(void)
{
	return template_devtab.name;
}

enum {
	Qdir,
	Qctl,
};

static struct dirtab XXX_dir[] = {
	{".", {Qdir, 0, QTDIR}, 0, DMDIR | 0555},
	{"ctl", {Qctl, 0, QTFILE}, 0, 0666},
};

static struct chan *XXX_attach(char *spec)
{
	return devattach(devname(), spec);
}

static struct walkqid *XXX_walk(struct chan *c, struct chan *nc, char **name,
			       unsigned int nname)
{
	return devwalk(c, nc, name, nname, XXX_dir, ARRAY_SIZE(XXX_dir),
		       devgen);
}

static size_t XXX_stat(struct chan *c, uint8_t *db, size_t n)
{
	return devstat(c, db, n, XXX_dir, ARRAY_SIZE(XXX_dir), devgen);
}

static struct chan *XXX_open(struct chan *c, int omode)
{
	return devopen(c, omode, XXX_dir, ARRAY_SIZE(XXX_dir), devgen);
}

static void XXX_close(struct chan *c)
{
	/* If you do anything after open, only undo it for COPEN chans */
	if (!(c->flag & COPEN))
		return;
}

static size_t XXX_read(struct chan *c, void *ubuf, size_t n, off64_t offset)
{
	switch (c->qid.path) {
	case Qdir:
		return devdirread(c, ubuf, n, XXX_dir, ARRAY_SIZE(XXX_dir),
				  devgen);
	case Qctl:
		return readstr(offset, ubuf, n, "XXX");
	default:
		panic("Bad Qid %p!", c->qid.path);
	}
	return -1;
}

#define XXX_CTL_USAGE "start|stop|print|reset"

static void XXX_ctl_cmd(struct chan *c, struct cmdbuf *cb)
{
	ERRSTACK(1);

	if (cb->nf < 1)
		error(EFAIL, XXX_CTL_USAGE);

	if (waserror()) {
		nexterror();
	}
	if (!strcmp(cb->f[0], "start")) {
		;
	} else if (!strcmp(cb->f[0], "stop")) {
		;
	} else if (!strcmp(cb->f[0], "print")) {
		;
	} else if (!strcmp(cb->f[0], "reset")) {
		;
	} else {
		error(EFAIL, XXX_CTL_USAGE);
	}
	poperror();
}

static size_t XXX_write(struct chan *c, void *ubuf, size_t n, off64_t unused)
{
	ERRSTACK(1);
	struct cmdbuf *cb = parsecmd(ubuf, n);

	if (waserror()) {
		kfree(cb);
		nexterror();
	}
	switch (c->qid.path) {
	case Qctl:
		XXX_ctl_cmd(c, cb);
		break;
	default:
		error(EFAIL, "Unable to write to %s", devname());
	}
	kfree(cb);
	poperror();
	return n;
}

struct dev template_devtab __devtab = {
	.name = "template",
	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = XXX_attach,
	.walk = XXX_walk,
	.stat = XXX_stat,
	.open = XXX_open,
	.create = devcreate,
	.close = XXX_close,
	.read = XXX_read,
	.bread = devbread,
	.write = XXX_write,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
	.power = devpower,
	.chaninfo = devchaninfo,
};
