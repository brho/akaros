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
#include <ip.h>
#include <time.h>

typedef struct IOMap IOMap;
struct IOMap {
	IOMap *next;
	int reserved;
	char tag[13];
	uint32_t start;
	uint32_t end;
};

static struct {
	spinlock_t lock;
	IOMap *map;
	IOMap *free;
	IOMap maps[32];				// some initial free maps

	qlock_t ql;					// lock for reading map
} iomap;

enum {
	Qdir = 0,
	Qioalloc = 1,
	Qiob,
	Qiow,
	Qiol,
	Qgdb,
	Qbase,
	Qmapram,
	Qrealmem,

	Qmax = 16,
};

typedef long Rdwrfn(struct chan *, void *, long, int64_t);

static Rdwrfn *readfn[Qmax];
static Rdwrfn *writefn[Qmax];

static struct dirtab archdir[Qmax] = {
	{".", {Qdir, 0, QTDIR}, 0, 0555},
	{"ioalloc", {Qioalloc, 0}, 0, 0444},
	{"iob", {Qiob, 0}, 0, 0660},
	{"iow", {Qiow, 0}, 0, 0660},
	{"iol", {Qiol, 0}, 0, 0660},
	{"gdb", {Qgdb, 0}, 0, 0660},
	{"mapram", {Qmapram, 0}, 0, 0444},
	{"realmodemem", {Qrealmem, 0}, 0, 0660},
};

spinlock_t archwlock;			/* the lock is only for changing archdir */
int narchdir = Qbase;
int gdbactive = 0;

/*
 * Add a file to the #P listing.  Once added, you can't delete it.
 * You can't add a file with the same name as one already there,
 * and you get a pointer to the Dirtab entry so you can do things
 * like change the Qid version.  Changing the Qid path is disallowed.
 */
struct dirtab *addarchfile(char *name, int perm, Rdwrfn * rdfn, Rdwrfn * wrfn)
{
	int i;
	struct dirtab d;
	struct dirtab *dp;

	memset(&d, 0, sizeof d);
	strncpy(d.name, name, sizeof(d.name));
	d.perm = perm;

	spin_lock(&archwlock);
	if (narchdir >= Qmax) {
		spin_unlock(&archwlock);
		return NULL;
	}

	for (i = 0; i < narchdir; i++)
		if (strcmp(archdir[i].name, name) == 0) {
			spin_unlock(&archwlock);
			return NULL;
		}

	d.qid.path = narchdir;
	archdir[narchdir] = d;
	readfn[narchdir] = rdfn;
	writefn[narchdir] = wrfn;
	dp = &archdir[narchdir++];
	spin_unlock(&archwlock);

	return dp;
}

void ioinit(void)
{
	int i;
	char *excluded = "";

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

#warning "how do we do io allocate"
		//ioalloc(io_s, io_e - io_s + 1, 0, "pre-allocated");
	}
}

// Reserve a range to be ioalloced later.
// This is in particular useful for exchangable cards, such
// as pcmcia and cardbus cards.
int ioreserve(int unused_int, int size, int align, char *tag)
{
	IOMap *map, **l;
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
	strncpy(map->tag, tag, sizeof(map->tag));
	map->tag[sizeof(map->tag) - 1] = 0;
	*l = map;

	archdir[0].qid.vers++;

	spin_unlock(&(&iomap)->lock);
	return map->start;
}

//
//  alloc some io port space and remember who it was
//  alloced to.  if port < 0, find a free region.
//
int ioalloc(int port, int size, int align, char *tag)
{
	IOMap *map, **l;
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
	strncpy(map->tag, tag, sizeof(map->tag));
	map->tag[sizeof(map->tag) - 1] = 0;
	*l = map;

	archdir[0].qid.vers++;

	spin_unlock(&(&iomap)->lock);
	return map->start;
}

void iofree(int port)
{
	IOMap *map, **l;

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
	IOMap *map;

	for (map = iomap.map; map; map = map->next) {
		if (((start >= map->start) && (start < map->end))
			|| ((start <= map->start) && (end > map->start)))
			return 0;
	}
	return 1;
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
	error(Eperm);
}

static struct chan *archattach(char *spec)
{
	return devattach('P', spec);
}

struct walkqid *archwalk(struct chan *c, struct chan *nc, char **name,
						 int nname)
{
	return devwalk(c, nc, name, nname, archdir, narchdir, devgen);
}

static int archstat(struct chan *c, uint8_t * dp, int n)
{
	return devstat(c, dp, n, archdir, narchdir, devgen);
}

static struct chan *archopen(struct chan *c, int omode)
{
	return devopen(c, omode, archdir, narchdir, devgen);
}

static void archclose(struct chan *unused)
{
}

enum {
	Linelen = 31,
};

static long archread(struct chan *c, void *a, long n, int64_t offset)
{
	char *buf, *p;
	int port;
	uint16_t *sp;
	uint32_t *lp;
	IOMap *map;
	Rdwrfn *fn;

	switch ((uint32_t) c->qid.path) {

		case Qdir:
			return devdirread(c, a, n, archdir, narchdir, devgen);

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
				error(Ebadarg);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				*sp++ = inw(port);
			return n;

		case Qiol:
			if (n & 3)
				error(Ebadarg);
			checkport(offset, offset + n);
			lp = a;
			for (port = offset; port < offset + n; port += 4)
				*lp++ = inl(port);
			return n;

		case Qioalloc:
			break;

		default:
			if (c->qid.path < narchdir && (fn = readfn[c->qid.path]))
				return fn(c, a, n, offset);
			error(Eperm);
			break;
	}

	if ((buf = kzmalloc(n, 0)) == NULL)
		error(Enomem);
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
		case Qmapram:
			error("Not yet");
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
	int port;
	uint16_t *sp;
	uint32_t *lp;
	Rdwrfn *fn;

	switch ((uint32_t) c->qid.path) {

		case Qgdb:
			p = a;
			if (n != 1)
				error("Gdb: Write one byte, '1' or '0'");
			if (*p == '1')
				gdbactive = 1;
			else if (*p == '0')
				gdbactive = 0;
			else
				error("Gdb: must be 1 or 0");
			return 1;

		case Qiob:
			p = a;
			checkport(offset, offset + n);
			for (port = offset; port < offset + n; port++)
				outb(port, *p++);
			return n;

		case Qiow:
			if (n & 1)
				error(Ebadarg);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				outw(port, *sp++);
			return n;

		case Qiol:
			if (n & 3)
				error(Ebadarg);
			checkport(offset, offset + n);
			lp = a;
			for (port = offset; port < offset + n; port += 4)
				outl(port, *lp++);
			return n;

		default:
			if (c->qid.path < narchdir && (fn = writefn[c->qid.path]))
				return fn(c, a, n, offset);
			error(Eperm);
			break;
	}
	return 0;
}

struct dev archdevtab = {
	'P',
	"arch",

	devreset,
	devinit,
	devshutdown,
	archattach,
	archwalk,
	archstat,
	archopen,
	devcreate,
	archclose,
	archread,
	devbread,
	archwrite,
	devbwrite,
	devremove,
	devwstat,
};

/*
 */
void nop(void)
{
}

//void (*coherence)(void) = mfence;
#warning "need memory fence"
#define coherence()

static long cputyperead(struct chan *unused, void *a, long n, int64_t off)
{
	char buf[512], *s, *e;
	int i, k;
	error("unimplemented");
#if 0
	e = buf + sizeof buf;
	s = seprintf(buf, e, "%s %d\n", "AMD64", 0);
	k = m->ncpuinfoe - m->ncpuinfos;
	if (k > 4)
		k = 4;
	for (i = 0; i < k; i++)
		s = seprintf(s, e, "%#8.8ux %#8.8ux %#8.8ux %#8.8ux\n",
					 m->cpuinfo[i][0], m->cpuinfo[i][1],
					 m->cpuinfo[i][2], m->cpuinfo[i][3]);
	return readstr(off, a, n, buf);
#endif
}

static long rmemrw(int isr, void *a, long n, int64_t off)
{
	if (off < 0)
		error("offset must be >= 0");
	if (n < 0)
		error("count must be >= 0");
	if (isr) {
		if (off >= MB)
			error("offset must be < 1MB");
		if (off + n >= MB)
			n = MB - off;
		memmove(a, KADDR((uint32_t) off), n);
	} else {
		/* realmode buf page ok, allow vga framebuf's access */
		if (off >= MB)
			error("offset must be < 1MB");
		if (off + n > MB && (off < 0xA0000 || off + n > 0xB0000 + 0x10000))
			error("bad offset/count in write");
		memmove(KADDR((uint32_t) off), a, n);
	}
	return n;
}

static long rmemread(struct chan *unused, void *a, long n, int64_t off)
{
	return rmemrw(1, a, n, off);
}

static long rmemwrite(struct chan *unused, void *a, long n, int64_t off)
{
	return rmemrw(0, a, n, off);
}

void archinit(void)
{
	spinlock_init(&archwlock);
	addarchfile("cputype", 0444, cputyperead, NULL);
	addarchfile("realmodemem", 0660, rmemread, rmemwrite);
}

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
