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
#include <kthread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <umem.h>
#include <smp.h>
#include <ip.h>
#include <time.h>
#include <atomic.h>
#include <core_set.h>
#include <completion.h>
#include <address_range.h>
#include <arch/uaccess.h>
#include <arch/devarch.h>

#define REAL_MEM_SIZE (1024 * 1024)

struct io_map {
	struct io_map *next;
	int reserved;
	char tag[13];
	uint32_t start;
	uint32_t end;
};

static struct {
	spinlock_t lock;
	struct io_map *map;
	struct io_map *free;
	struct io_map maps[32];				// some initial free maps
	qlock_t ql;					// lock for reading map
} iomap;

enum {
	Qdir = 0,
	Qioalloc = 1,
	Qiob,
	Qiow,
	Qiol,
	Qgdb,
	Qrealmem,
	Qmsr,

	Qmax,
};

enum {
	Linelen = 31,
};

struct smp_read_values {
	uint64_t *values;
	uint32_t addr;
	atomic_t err;
};
struct smp_write_values {
	uint32_t addr;
	uint64_t value;
	atomic_t err;
};

struct dev archdevtab;
static struct dirtab archdir[Qmax] = {
	{".", {Qdir, 0, QTDIR}, 0, 0555},
	{"ioalloc", {Qioalloc, 0}, 0, 0444},
	{"iob", {Qiob, 0}, 0, 0666},
	{"iow", {Qiow, 0}, 0, 0666},
	{"iol", {Qiol, 0}, 0, 0666},
	{"gdb", {Qgdb, 0}, 0, 0660},
	{"realmem", {Qrealmem, 0}, 0, 0444},
	{"msr", {Qmsr, 0}, 0, 0666},
};

int gdbactive = 0;

static void cpu_read_msr(void *opaque)
{
	int err, cpu = core_id();
	struct smp_read_values *srv = (struct smp_read_values *) opaque;

	err = safe_read_msr(srv->addr, &srv->values[cpu]);
	if (unlikely(err))
		atomic_cas(&srv->err, 0, err);
}

uint64_t *coreset_read_msr(const struct core_set *cset, uint32_t addr,
						   size_t *nvalues)
{
	int err;
	struct smp_read_values srv;

	srv.addr = addr;
	atomic_init(&srv.err, 0);
	srv.values = kzmalloc(num_cores * sizeof(*srv.values), 0);
	if (unlikely(!srv.values))
		return ERR_PTR(-ENOMEM);
	smp_do_in_cores(cset, cpu_read_msr, &srv);
	err = (int) atomic_read(&srv.err);
	if (unlikely(err)) {
		kfree(srv.values);
		return ERR_PTR(err);
	}
	*nvalues = num_cores;

	return srv.values;
}

static void cpu_write_msr(void *opaque)
{
	int err;
	struct smp_write_values *swv = (struct smp_write_values *) opaque;

	err = safe_write_msr(swv->addr, swv->value);
	if (unlikely(err))
		atomic_cas(&swv->err, 0, err);
}

int coreset_write_msr(const struct core_set *cset, uint32_t addr,
					   uint64_t value)
{
	struct smp_write_values swv;

	swv.addr = addr;
	swv.value = value;
	atomic_init(&swv.err, 0);
	smp_do_in_cores(cset, cpu_write_msr, &swv);

	return (int) atomic_read(&swv.err);
}

//
//  alloc some io port space and remember who it was
//  alloced to.  if port < 0, find a free region.
//
int ioalloc(int port, int size, int align, char *tag)
{
	struct io_map *map, **l;
	int i;

	spin_lock(&(&iomap)->lock);
	if (port < 0) {
		// find a free port above 0x400 and below 0x1000
		port = 0x400;
		for (l = &iomap.map; *l; l = &(*l)->next) {
			map = *l;
			if (map->start < 0x400)
				continue;
			i = map->start - port;
			if (i > size)
				break;
			if (align > 0)
				port = ((port + align - 1) / align) * align;
			else
				port = map->end;
		}
		if (*l == NULL) {
			spin_unlock(&(&iomap)->lock);
			return -1;
		}
	} else {
		// Only 64KB I/O space on the x86.
		if ((port + size) > 0x10000) {
			spin_unlock(&(&iomap)->lock);
			return -1;
		}
		// see if the space clashes with previously allocated ports
		for (l = &iomap.map; *l; l = &(*l)->next) {
			map = *l;
			if (map->end <= port)
				continue;
			if (map->reserved && map->start == port && map->end == port + size) {
				map->reserved = 0;
				spin_unlock(&(&iomap)->lock);
				return map->start;
			}
			if (map->start >= port + size)
				break;
			spin_unlock(&(&iomap)->lock);
			return -1;
		}
	}
	map = iomap.free;
	if (map == NULL) {
		printd("ioalloc: out of maps");
		spin_unlock(&(&iomap)->lock);
		return port;
	}
	iomap.free = map->next;
	map->next = *l;
	map->start = port;
	map->end = port + size;
	strlcpy(map->tag, tag, sizeof(map->tag));
	*l = map;

	archdir[0].qid.vers++;

	spin_unlock(&(&iomap)->lock);
	return map->start;
}

void iofree(int port)
{
	struct io_map *map, **l;

	spin_lock(&(&iomap)->lock);
	for (l = &iomap.map; *l; l = &(*l)->next) {
		if ((*l)->start == port) {
			map = *l;
			*l = map->next;
			map->next = iomap.free;
			iomap.free = map;
			break;
		}
		if ((*l)->start > port)
			break;
	}
	archdir[0].qid.vers++;
	spin_unlock(&(&iomap)->lock);
}

int iounused(int start, int end)
{
	struct io_map *map;

	for (map = iomap.map; map; map = map->next) {
		if (((start >= map->start) && (start < map->end))
			|| ((start <= map->start) && (end > map->start)))
			return 0;
	}
	return 1;
}

void ioinit(void)
{
	int i;
	char *excluded = "";

	panic("Akaros doesn't do IO port allocation yet.  Don't init.");
	for (i = 0; i < ARRAY_SIZE(iomap.maps) - 1; i++)
		iomap.maps[i].next = &iomap.maps[i + 1];
	iomap.maps[i].next = NULL;
	iomap.free = iomap.maps;
	char *s;

	s = excluded;
	while (s && *s != '\0' && *s != '\n') {
		char *ends;
		int io_s, io_e;

		io_s = (int)strtol(s, &ends, 0);
		if (ends == NULL || ends == s || *ends != '-') {
			printd("ioinit: cannot parse option string\n");
			break;
		}
		s = ++ends;

		io_e = (int)strtol(s, &ends, 0);
		if (ends && *ends == ',')
			*ends++ = '\0';
		s = ends;

		ioalloc(io_s, io_e - io_s + 1, 0, "pre-allocated");
	}
}

// Reserve a range to be ioalloced later.
// This is in particular useful for exchangable cards, such
// as pcmcia and cardbus cards.
int ioreserve(int unused_int, int size, int align, char *tag)
{
	struct io_map *map, **l;
	int i, port;

	spin_lock(&(&iomap)->lock);
	// find a free port above 0x400 and below 0x1000
	port = 0x400;
	for (l = &iomap.map; *l; l = &(*l)->next) {
		map = *l;
		if (map->start < 0x400)
			continue;
		i = map->start - port;
		if (i > size)
			break;
		if (align > 0)
			port = ((port + align - 1) / align) * align;
		else
			port = map->end;
	}
	if (*l == NULL) {
		spin_unlock(&(&iomap)->lock);
		return -1;
	}
	map = iomap.free;
	if (map == NULL) {
		printd("ioalloc: out of maps");
		spin_unlock(&(&iomap)->lock);
		return port;
	}
	iomap.free = map->next;
	map->next = *l;
	map->start = port;
	map->end = port + size;
	map->reserved = 1;
	strlcpy(map->tag, tag, sizeof(map->tag));
	*l = map;

	archdir[0].qid.vers++;

	spin_unlock(&(&iomap)->lock);
	return map->start;
}

static void checkport(int start, int end)
{
	/* standard vga regs are OK */
	if (start >= 0x2b0 && end <= 0x2df + 1)
		return;
	if (start >= 0x3c0 && end <= 0x3da + 1)
		return;

	if (iounused(start, end))
		return;
	error(EPERM, NULL);
}

static struct chan *archattach(char *spec)
{
	return devattach(archdevtab.name, spec);
}

struct walkqid *archwalk(struct chan *c, struct chan *nc, char **name,
						 int nname)
{
	return devwalk(c, nc, name, nname, archdir, Qmax, devgen);
}

static int archstat(struct chan *c, uint8_t * dp, int n)
{
	archdir[Qrealmem].length = REAL_MEM_SIZE;

	return devstat(c, dp, n, archdir, Qmax, devgen);
}

static struct chan *archopen(struct chan *c, int omode)
{
	return devopen(c, omode, archdir, Qmax, devgen);
}

static void archclose(struct chan *unused)
{
}

static long archread(struct chan *c, void *a, long n, int64_t offset)
{
	char *buf, *p;
	int port;
	size_t nvalues;
	uint64_t *values;
	uint16_t *sp;
	uint32_t *lp;
	struct io_map *map;
	struct core_set cset;

	switch ((uint32_t) c->qid.path) {
		case Qdir:
			return devdirread(c, a, n, archdir, Qmax, devgen);
		case Qgdb:
			p = gdbactive ? "1" : "0";
			return readstr(offset, a, n, p);
		case Qiob:
			port = offset;
			checkport(offset, offset + n);
			for (p = a; port < offset + n; port++)
				*p++ = inb(port);
			return n;
		case Qiow:
			if (n & 1)
				error(EINVAL, NULL);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				*sp++ = inw(port);
			return n;
		case Qiol:
			if (n & 3)
				error(EINVAL, NULL);
			checkport(offset, offset + n);
			lp = a;
			for (port = offset; port < offset + n; port += 4)
				*lp++ = inl(port);
			return n;
		case Qioalloc:
			break;
		case Qrealmem:
			return readmem(offset, a, n, KADDR(0), REAL_MEM_SIZE);
		case Qmsr:
			if (!address_range_find(msr_rd_wlist, ARRAY_SIZE(msr_rd_wlist),
									(uintptr_t) offset))
				error(EPERM, NULL);
			core_set_init(&cset);
			core_set_fill_available(&cset);
			values = coreset_read_msr(&cset, (uint32_t) offset, &nvalues);
			if (likely(!IS_ERR(values))) {
				if (n >= nvalues * sizeof(uint64_t)) {
					if (memcpy_to_user_errno(current, a, values,
											 nvalues * sizeof(uint64_t)))
						n = -1;
					else
						n = nvalues * sizeof(uint64_t);
				} else {
					kfree(values);
					error(ERANGE, NULL);
				}
				kfree(values);
			} else {
				error(-PTR_ERR(values), NULL);
			}
			return n;
		default:
			error(EINVAL, NULL);
	}

	if ((buf = kzmalloc(n, 0)) == NULL)
		error(ENOMEM, NULL);
	p = buf;
	n = n / Linelen;
	offset = offset / Linelen;

	switch ((uint32_t) c->qid.path) {
		case Qioalloc:
			spin_lock(&(&iomap)->lock);
			for (map = iomap.map; n > 0 && map != NULL; map = map->next) {
				if (offset-- > 0)
					continue;
				snprintf(p, n * Linelen, "%#8p %#8p %-12.12s\n", map->start,
						 map->end - 1, map->tag);
				p += Linelen;
				n--;
			}
			spin_unlock(&(&iomap)->lock);
			break;
	}

	n = p - buf;
	memmove(a, buf, n);
	kfree(buf);

	return n;
}

static long archwrite(struct chan *c, void *a, long n, int64_t offset)
{
	char *p;
	int port, err;
	uint64_t value;
	uint16_t *sp;
	uint32_t *lp;
	struct core_set cset;

	switch ((uint32_t) c->qid.path) {
		case Qgdb:
			p = a;
			if (n != 1)
				error(EINVAL, "Gdb: Write one byte, '1' or '0'");
			if (*p == '1')
				gdbactive = 1;
			else if (*p == '0')
				gdbactive = 0;
			else
				error(EINVAL, "Gdb: must be 1 or 0");
			return 1;
		case Qiob:
			p = a;
			checkport(offset, offset + n);
			for (port = offset; port < offset + n; port++)
				outb(port, *p++);
			return n;
		case Qiow:
			if (n & 1)
				error(EINVAL, NULL);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				outw(port, *sp++);
			return n;
		case Qiol:
			if (n & 3)
				error(EINVAL, NULL);
			checkport(offset, offset + n);
			lp = a;
			for (port = offset; port < offset + n; port += 4)
				outl(port, *lp++);
			return n;
		case Qmsr:
			if (!address_range_find(msr_wr_wlist, ARRAY_SIZE(msr_wr_wlist),
									(uintptr_t) offset))
				error(EPERM, NULL);
			core_set_init(&cset);
			core_set_fill_available(&cset);
			if (n != sizeof(uint64_t))
				error(EINVAL, NULL);
			if (memcpy_from_user_errno(current, &value, a, sizeof(value)))
				return -1;
			err = coreset_write_msr(&cset, (uint32_t) offset, value);
			if (unlikely(err))
				error(-err, NULL);
			return sizeof(uint64_t);
		default:
			error(EINVAL, NULL);
	}
	return 0;
}

struct dev archdevtab __devtab = {
	.name = "arch",

	.reset = devreset,
	.init = devinit,
	.shutdown = devshutdown,
	.attach = archattach,
	.walk = archwalk,
	.stat = archstat,
	.open = archopen,
	.create = devcreate,
	.close = archclose,
	.read = archread,
	.bread = devbread,
	.write = archwrite,
	.bwrite = devbwrite,
	.remove = devremove,
	.wstat = devwstat,
};

void archreset(void)
{
	int i;

	/*
	 * And sometimes there is no keyboard...
	 *
	 * The reset register (0xcf9) is usually in one of the bridge
	 * chips. The actual location and sequence could be extracted from
	 * ACPI but why bother, this is the end of the line anyway.
	 print("Takes a licking and keeps on ticking...\n");
	 */
	i = inb(0xcf9);	/* ICHx reset control */
	i &= 0x06;
	outb(0xcf9, i | 0x02);	/* SYS_RST */
	udelay(1000);
	outb(0xcf9, i | 0x06);	/* RST_CPU transition */

	udelay(100 * 1000);

	/* some broken hardware -- as well as qemu -- might
	 * never reboot anyway with cf9. This is a standard
	 * keyboard reboot sequence known to work on really
	 * broken stuff -- like qemu. If there is no
	 * keyboard it will do no harm.
	 */
	for (;;) {
		(void)inb(0x64);
		outb(0x64, 0xFE);
		udelay(100 * 1000);
	}
}
