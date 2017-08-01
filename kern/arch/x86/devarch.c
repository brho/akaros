/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include <ros/memops.h>
#include <vfs.h>
#include <kmalloc.h>
#include <kref.h>
#include <kthread.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <err.h>
#include <pmap.h>
#include <umem.h>
#include <smp.h>
#include <ip.h>
#include <time.h>
#include <bitops.h>
#include <core_set.h>
#include <address_range.h>
#include <arch/ros/perfmon.h>
#include <arch/topology.h>
#include <arch/perfmon.h>
#include <arch/ros/msr-index.h>
#include <arch/msr.h>
#include <arch/devarch.h>

#define REAL_MEM_SIZE (1024 * 1024)

struct perf_context {
	struct perfmon_session *ps;
	qlock_t resp_lock;
	size_t resp_size;
	uint8_t *resp;
};

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
	Qperf,
	Qcstate,
	Qpstate,

	Qmax,
};

enum {
	Linelen = 31,
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
	{"perf", {Qperf, 0}, 0, 0666},
	{"c-state", {Qcstate, 0}, 0, 0666},
	{"p-state", {Qpstate, 0}, 0, 0666},
};
/* White list entries needs to be ordered by start address, and never overlap.
 */
#define MSR_MAX_VAR_COUNTERS 16
#define MSR_MAX_FIX_COUNTERS 4

static struct address_range msr_rd_wlist[] = {
	ADDRESS_RANGE(0x00000000, 0xffffffff),
};
static struct address_range msr_wr_wlist[] = {
	ADDRESS_RANGE(MSR_IA32_PERFCTR0,
				  MSR_IA32_PERFCTR0 + MSR_MAX_VAR_COUNTERS - 1),
	ADDRESS_RANGE(MSR_ARCH_PERFMON_EVENTSEL0,
				  MSR_ARCH_PERFMON_EVENTSEL0 + MSR_MAX_VAR_COUNTERS - 1),
	ADDRESS_RANGE(MSR_IA32_PERF_CTL, MSR_IA32_PERF_CTL),
	ADDRESS_RANGE(MSR_CORE_PERF_FIXED_CTR0,
				  MSR_CORE_PERF_FIXED_CTR0 + MSR_MAX_FIX_COUNTERS - 1),
	ADDRESS_RANGE(MSR_CORE_PERF_FIXED_CTR_CTRL, MSR_CORE_PERF_GLOBAL_OVF_CTRL),
};
int gdbactive = 0;

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
		if (((start >= map->start) && (start < map->end)) ||
		    ((start <= map->start) && (end > map->start)))
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
	error(EPERM, ERROR_FIXME);
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

static struct perf_context *arch_create_perf_context(void)
{
	ERRSTACK(1);
	struct perf_context *pc = kzmalloc(sizeof(struct perf_context),
	                                   MEM_WAIT);

	if (waserror()) {
		kfree(pc);
		nexterror();
	}
	qlock_init(&pc->resp_lock);
	pc->ps = perfmon_create_session();
	poperror();

	return pc;
}

/* Called after the last reference (FD / chan) to pc is closed. */
static void arch_free_perf_context(struct perf_context *pc)
{
	perfmon_close_session(pc->ps);
	kfree(pc->resp);
	kfree(pc);
}

static const uint8_t *arch_read_core_set(struct core_set *cset,
                                         const uint8_t *kptr,
                                         const uint8_t *ktop)
{
	int i, nb;
	uint32_t n;

	error_assert(EBADMSG, (kptr + sizeof(uint32_t)) <= ktop);
	kptr = get_le_u32(kptr, &n);
	error_assert(EBADMSG, (kptr + n) <= ktop);
	core_set_init(cset);
	nb = MIN((int) n * 8, num_cores);
	for (i = 0; i < nb; i++) {
		if (test_bit(i, (const unsigned long *) kptr))
			core_set_setcpu(cset, i);
	}

	return kptr + n;
}

static long arch_perf_write(struct perf_context *pc, const void *udata,
                            long usize)
{
	ERRSTACK(1);
	void *kdata;
	const uint8_t *kptr, *ktop;

	kdata = user_memdup_errno(current, udata, usize);
	if (unlikely(!kdata))
		return -1;
	qlock(&pc->resp_lock);
	if (waserror()) {
		qunlock(&pc->resp_lock);
		kfree(kdata);
		nexterror();
	}
	/* Fresh command, reset the response buffer */
	kfree(pc->resp);
	pc->resp = NULL;
	pc->resp_size = 0;

	kptr = kdata;
	ktop = kptr + usize;
	error_assert(EBADMSG, (kptr + 1) <= ktop);
	switch (*kptr++) {
		case PERFMON_CMD_COUNTER_OPEN: {
			int ped;
			struct perfmon_event pev;
			struct core_set cset;

			error_assert(EBADMSG, (kptr + 3 * sizeof(uint64_t)) <= ktop);
			perfmon_init_event(&pev);
			kptr = get_le_u64(kptr, &pev.event);
			kptr = get_le_u64(kptr, &pev.flags);
			kptr = get_le_u64(kptr, &pev.trigger_count);
			kptr = get_le_u64(kptr, &pev.user_data);
			kptr = arch_read_core_set(&cset, kptr, ktop);

			ped = perfmon_open_event(&cset, pc->ps, &pev);

			pc->resp_size = sizeof(uint32_t);
			pc->resp = kmalloc(pc->resp_size, MEM_WAIT);
			put_le_u32(pc->resp, (uint32_t) ped);
			break;
		}
		case PERFMON_CMD_COUNTER_STATUS: {
			uint32_t ped;
			uint8_t *rptr;
			struct perfmon_status *pef;

			error_assert(EBADMSG, (kptr + sizeof(uint32_t)) <= ktop);
			kptr = get_le_u32(kptr, &ped);

			pef = perfmon_get_event_status(pc->ps, (int) ped);

			pc->resp_size = sizeof(uint32_t) + num_cores * sizeof(uint64_t);
			pc->resp = kmalloc(pc->resp_size, MEM_WAIT);
			rptr = put_le_u32(pc->resp, num_cores);
			for (int i = 0; i < num_cores; i++)
				rptr = put_le_u64(rptr, pef->cores_values[i]);

			perfmon_free_event_status(pef);
			break;
		}
		case PERFMON_CMD_COUNTER_CLOSE: {
			uint32_t ped;

			error_assert(EBADMSG, (kptr + sizeof(uint32_t)) <= ktop);
			kptr = get_le_u32(kptr, &ped);

			perfmon_close_event(pc->ps, (int) ped);
			break;
		}
		case PERFMON_CMD_CPU_CAPS: {
			uint8_t *rptr;
			struct perfmon_cpu_caps pcc;

			perfmon_get_cpu_caps(&pcc);

			pc->resp_size = 6 * sizeof(uint32_t);
			pc->resp = kmalloc(pc->resp_size, MEM_WAIT);

			rptr = put_le_u32(pc->resp, pcc.perfmon_version);
			rptr = put_le_u32(rptr, pcc.proc_arch_events);
			rptr = put_le_u32(rptr, pcc.bits_x_counter);
			rptr = put_le_u32(rptr, pcc.counters_x_proc);
			rptr = put_le_u32(rptr, pcc.bits_x_fix_counter);
			rptr = put_le_u32(rptr, pcc.fix_counters_x_proc);
			break;
		}
		default:
			error(EINVAL, "Invalid perfmon command: 0x%x", kptr[-1]);
	}
	poperror();
	qunlock(&pc->resp_lock);
	kfree(kdata);

	return (long) (kptr - (const uint8_t *) kdata);
}

static struct chan *archopen(struct chan *c, int omode)
{
	c = devopen(c, omode, archdir, Qmax, devgen);
	switch ((uint32_t) c->qid.path) {
		case Qperf:
			if (!perfmon_supported())
				error(ENODEV, "perf is not supported");
			assert(!c->aux);
			c->aux = arch_create_perf_context();
			break;
	}

	return c;
}

static void archclose(struct chan *c)
{
	switch ((uint32_t) c->qid.path) {
		case Qperf:
			if (c->aux) {
				arch_free_perf_context((struct perf_context *) c->aux);
				c->aux = NULL;
			}
			break;
	}
}

static long archread(struct chan *c, void *a, long n, int64_t offset)
{
	char *buf, *p;
	int err, port;
	uint64_t *values;
	uint16_t *sp;
	uint32_t *lp;
	struct io_map *map;
	struct core_set cset;
	struct msr_address msra;
	struct msr_value msrv;

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
				error(EINVAL, ERROR_FIXME);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				*sp++ = inw(port);
			return n;
		case Qiol:
			if (n & 3)
				error(EINVAL, ERROR_FIXME);
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
				error(EPERM, "MSR 0x%x not in read whitelist", offset);
			core_set_init(&cset);
			core_set_fill_available(&cset);
			msr_set_address(&msra, (uint32_t) offset);
			values = kzmalloc(num_cores * sizeof(uint64_t),
					  MEM_WAIT);
			if (!values)
				error(ENOMEM, ERROR_FIXME);
			msr_set_values(&msrv, values, num_cores);

			err = msr_cores_read(&cset, &msra, &msrv);

			if (likely(!err)) {
				if (n >= num_cores * sizeof(uint64_t)) {
					if (!memcpy_to_user_errno(current, a, values,
					                          num_cores * sizeof(uint64_t)))
						n = num_cores * sizeof(uint64_t);
					else
						n = -1;
				} else {
					kfree(values);
					error(ERANGE, "Not enough space for MSR read");
				}
			} else {
				switch (-err) {
				case (EFAULT):
					error(-err, "read_msr() faulted on MSR 0x%x", offset);
				case (ERANGE):
					error(-err, "Not enough space for MSR read");
				};
				error(-err, "MSR read failed");
			}
			kfree(values);
			return n;
		case Qperf: {
			struct perf_context *pc = (struct perf_context *) c->aux;

			assert(pc);
			qlock(&pc->resp_lock);
			if (pc->resp && ((size_t) offset < pc->resp_size)) {
				n = MIN(n, (long) pc->resp_size - (long) offset);
				if (memcpy_to_user_errno(current, a, pc->resp + offset, n))
					n = -1;
			} else {
				n = 0;
			}
			qunlock(&pc->resp_lock);

			return n;
		case Qcstate:
			return readnum_hex(offset, a, n, get_cstate(), NUMSIZE32);
		case Qpstate:
			return readnum_hex(offset, a, n, get_pstate(), NUMSIZE32);
		}
		default:
			error(EINVAL, ERROR_FIXME);
	}

	if ((buf = kzmalloc(n, 0)) == NULL)
		error(ENOMEM, ERROR_FIXME);
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

static ssize_t cstate_write(void *ubuf, size_t len, off64_t off)
{
	set_cstate(strtoul_from_ubuf(ubuf, len, off));
	/* Poke the other cores so they use the new C-state. */
	send_broadcast_ipi(I_POKE_CORE);
	return len;
}

static void __smp_set_pstate(void *arg)
{
	unsigned int val = (unsigned int)(unsigned long)arg;

	set_pstate(val);
}

static ssize_t pstate_write(void *ubuf, size_t len, off64_t off)
{
	struct core_set all_cores;

	core_set_init(&all_cores);
	core_set_fill_available(&all_cores);
	smp_do_in_cores(&all_cores, __smp_set_pstate,
	                (void*)strtoul_from_ubuf(ubuf, len, off));
	return len;
}

static long archwrite(struct chan *c, void *a, long n, int64_t offset)
{
	char *p;
	int port, err;
	uint64_t value;
	uint16_t *sp;
	uint32_t *lp;
	struct core_set cset;
	struct msr_address msra;
	struct msr_value msrv;

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
				error(EINVAL, ERROR_FIXME);
			checkport(offset, offset + n);
			sp = a;
			for (port = offset; port < offset + n; port += 2)
				outw(port, *sp++);
			return n;
		case Qiol:
			if (n & 3)
				error(EINVAL, ERROR_FIXME);
			checkport(offset, offset + n);
			lp = a;
			for (port = offset; port < offset + n; port += 4)
				outl(port, *lp++);
			return n;
		case Qmsr:
			if (!address_range_find(msr_wr_wlist, ARRAY_SIZE(msr_wr_wlist),
			                        (uintptr_t) offset))
				error(EPERM, "MSR 0x%x not in write whitelist", offset);
			if (n != sizeof(uint64_t))
				error(EINVAL, "Tried to write more than a u64 (%p)", n);
			if (memcpy_from_user_errno(current, &value, a, sizeof(value)))
				return -1;

			core_set_init(&cset);
			core_set_fill_available(&cset);
			msr_set_address(&msra, (uint32_t) offset);
			msr_set_value(&msrv, value);

			err = msr_cores_write(&cset, &msra, &msrv);
			if (unlikely(err)) {
				switch (-err) {
				case (EFAULT):
					error(-err, "write_msr() faulted on MSR 0x%x", offset);
				case (ERANGE):
					error(-err, "Not enough space for MSR write");
				};
				error(-err, "MSR write failed");
			}
			return sizeof(uint64_t);
		case Qperf: {
			struct perf_context *pc = (struct perf_context *) c->aux;

			assert(pc);

			return arch_perf_write(pc, a, n);
		}
		case Qcstate:
			return cstate_write(a, n, 0);
		case Qpstate:
			return pstate_write(a, n, 0);
		default:
			error(EINVAL, ERROR_FIXME);
	}
	return 0;
}

static void archinit(void)
{
	int ret;

	ret = address_range_init(msr_rd_wlist, ARRAY_SIZE(msr_rd_wlist));
	assert(!ret);
	ret = address_range_init(msr_wr_wlist, ARRAY_SIZE(msr_wr_wlist));
	assert(!ret);
}

struct dev archdevtab __devtab = {
	.name = "arch",

	.reset = devreset,
	.init = archinit,
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
