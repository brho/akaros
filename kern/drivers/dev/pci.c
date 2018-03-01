/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

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
#include <arch/io.h>

struct dev pcidevtab;

static char *devname(void)
{
	return pcidevtab.name;
}

enum {
	Qtopdir = 0,

	Qpcidir,
	Qpcictl,
	Qpciraw,

	PCI_CONFIG_SZ = 256,
};

#define TYPE(q)		((uint32_t)(q).path & 0x0F)
#define QID(c, t)	(((c)<<4)|(t))

static struct dirtab topdir[] = {
	{".", {Qtopdir, 0, QTDIR}, 0, 0555},
	{"pci", {Qpcidir, 0, QTDIR}, 0, 0555},
};

static int pcidirgen(struct chan *c, int t, int tbdf, struct dir *dp)
{
	struct qid q;

	q = (struct qid) {
		BUSBDF(tbdf) | t, 0, 0};
	switch (t) {
	case Qpcictl:
		snprintf(get_cur_genbuf(), GENBUF_SZ, "%d.%d.%dctl",
		         BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
		devdir(c, q, get_cur_genbuf(), 0, eve.name, 0444, dp);
		return 1;
	case Qpciraw:
		snprintf(get_cur_genbuf(), GENBUF_SZ, "%d.%d.%draw",
		         BUSBNO(tbdf), BUSDNO(tbdf), BUSFNO(tbdf));
		devdir(c, q, get_cur_genbuf(), 128, eve.name, 0664, dp);
		return 1;
	}
	return -1;
}

static int
pcigen(struct chan *c, char *_1, struct dirtab *_2, int _3, int s,
       struct dir *dp)
{
	int tbdf;
	struct pci_device *p;
	struct qid q;

	switch (TYPE(c->qid)) {
	case Qtopdir:
		if (s == DEVDOTDOT) {
			q = (struct qid) {
				QID(0, Qtopdir), 0, QTDIR};
			snprintf(get_cur_genbuf(), GENBUF_SZ, "#%s", pcidevtab.name);
			devdir(c, q, get_cur_genbuf(), 0, eve.name, 0555, dp);
			return 1;
		}
		return devgen(c, NULL, topdir, ARRAY_SIZE(topdir), s, dp);
	case Qpcidir:
		if (s == DEVDOTDOT) {
			q = (struct qid) {
				QID(0, Qtopdir), 0, QTDIR};
			snprintf(get_cur_genbuf(), GENBUF_SZ, "#%s", pcidevtab.name);
			devdir(c, q, get_cur_genbuf(), 0, eve.name, 0555, dp);
			return 1;
		}
		STAILQ_FOREACH(p, &pci_devices, all_dev) {
			if (s < 2)
				break;
			s -= 2;
		}
		if (p == NULL)
			return -1;
		return pcidirgen(c, s + Qpcictl, pci_to_tbdf(p), dp);
	case Qpcictl:
	case Qpciraw:
		tbdf = MKBUS(BusPCI, 0, 0, 0) | BUSBDF((uint32_t) c->qid.path);
		p = pci_match_tbdf(tbdf);
		if (p == NULL)
			return -1;
		return pcidirgen(c, TYPE(c->qid), tbdf, dp);
	default:
		break;
	}
	return -1;
}

static struct chan *pciattach(char *spec)
{
	return devattach(devname(), spec);
}

struct walkqid *pciwalk(struct chan *c, struct chan *nc, char **name,
                        unsigned int nname)
{
	return devwalk(c, nc, name, nname, (struct dirtab *)0, 0, pcigen);
}

static size_t pcistat(struct chan *c, uint8_t *dp, size_t n)
{
	return devstat(c, dp, n, (struct dirtab *)0, 0L, pcigen);
}

static struct chan *pciopen(struct chan *c, int omode)
{
	c = devopen(c, omode, (struct dirtab *)0, 0, pcigen);
	switch (TYPE(c->qid)) {
	default:
		break;
	}
	return c;
}

static void pciclose(struct chan *_)
{
}

static size_t pciread(struct chan *c, void *va, size_t n, off64_t offset)
{
	char buf[PCI_CONFIG_SZ], *ebuf, *w, *a;
	int i, tbdf, r;
	uint32_t x;
	struct pci_device *p;

	a = va;
	switch (TYPE(c->qid)) {
	case Qtopdir:
	case Qpcidir:
		return devdirread(c, a, n, (struct dirtab *)0, 0L, pcigen);
	case Qpcictl:
		tbdf = MKBUS(BusPCI, 0, 0, 0) | BUSBDF((uint32_t) c->qid.path);
		p = pci_match_tbdf(tbdf);
		if (p == NULL)
			error(EINVAL, ERROR_FIXME);
		ebuf = buf + sizeof(buf) - 1;	/* -1 for newline */
		w = seprintf(buf, ebuf, "%.2x.%.2x.%.2x %.4x/%.4x %3d",
		             p->class, p->subclass, p->progif, p->ven_id, p->dev_id,
		             p->irqline);
		for (i = 0; i < COUNT_OF(p->bar); i++) {
			if (p->bar[i].mmio_sz == 0)
				continue;
			w = seprintf(w, ebuf, " %d:%.8lux %d", i, p->bar[i].pio_base,
			             p->bar[i].mmio_sz);
		}
		*w++ = '\n';
		*w = '\0';
		return readstr(offset, a, n, buf);
	case Qpciraw:
		tbdf = MKBUS(BusPCI, 0, 0, 0) | BUSBDF((uint32_t) c->qid.path);
		p = pci_match_tbdf(tbdf);
		if (p == NULL)
			error(EINVAL, ERROR_FIXME);
		if (n + offset > 256)
			n = 256 - offset;
		if (n < 0)
			return 0;
		r = offset;
		if (!(r & 3) && n == 4) {
			x = pcidev_read32(p, r);
			PBIT32(a, x);
			return 4;
		}
		if (!(r & 1) && n == 2) {
			x = pcidev_read16(p, r);
			PBIT16(a, x);
			return 2;
		}
		for (i = 0; i < n; i++) {
			x = pcidev_read8(p, r);
			PBIT8(a, x);
			a++;
			r++;
		}
		return i;
	default:
		error(EINVAL, ERROR_FIXME);
	}
	return n;
}

static size_t pciwrite(struct chan *c, void *va, size_t n, off64_t offset)
{
	uint8_t *a;
	int i, r, tbdf;
	uint32_t x;
	struct pci_device *p;

	if (n > PCI_CONFIG_SZ)
		n = PCI_CONFIG_SZ;
	a = va;

	switch (TYPE(c->qid)) {
	case Qpciraw:
		tbdf = MKBUS(BusPCI, 0, 0, 0) | BUSBDF((uint32_t) c->qid.path);
		p = pci_match_tbdf(tbdf);
		if (p == NULL)
			error(EINVAL, ERROR_FIXME);
		if (offset > PCI_CONFIG_SZ)
			return 0;
		if (n + offset > PCI_CONFIG_SZ)
			n = PCI_CONFIG_SZ - offset;
		r = offset;
		if (!(r & 3) && n == 4) {
			x = GBIT32(a);
			pcidev_write32(p, r, x);
			return 4;
		}
		if (!(r & 1) && n == 2) {
			x = GBIT16(a);
			pcidev_write16(p, r, x);
			return 2;
		}
		for (i = 0; i < n; i++) {
			x = GBIT8(a);
			pcidev_write8(p, r, x);
			a++;
			r++;
		}
		return i;
	default:
		error(EINVAL, ERROR_FIXME);
	}
	return n;
}

struct dev pcidevtab __devtab = {
	.name = "pci",

	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = pciattach,
	.walk = pciwalk,
	.stat = pcistat,
	.open = pciopen,
	.create = devcreate,
	.close = pciclose,
	.read = pciread,
	.bread = devbread,
	.write = pciwrite,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
};
