/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

/*
 * This file implements the #T device and was based upon the UCB Plan 9 kprof.c
 */

#include <assert.h>
#include <atomic.h>
#include <kmalloc.h>
#include <ns.h>
#include <smallidpool.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <ttrace.h>
#include <umem.h>

#include <ros/fs.h>

/*
 * ttrace macros and constant data
 */
#ifndef min
#define min(a, b) ({ \
	typeof (a) _a = (a); typeof (b) _b = (b); _a < _b ? _a : _b; })
#endif

#define TTRACE_CTL_LEN     46
#define TTRACE_MAX_TSID    min(MAX_U16_POOL_SZ, (1 << Logtsid))
#define TTRACE_NUM_OPENERS 8
#define TT_SAFE_GENBUF_SZ  (GENBUF_SZ-1)  // Leave room for newline

/* TODO(gvdl): I don't get plan 9's permissions, why do directories get group
 * rx permissions, and what's with the DMDIR. Some devices use it and others
 * don't. In theory the DMDIR is copied over by a higher layer but I have no
 * idea why two copies seems necessary. */
#define TTPERMDIR    (S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|DMDIR)
#define TTPERMRWFILE (S_IRUSR|S_IWUSR)
#define TTPERMROFILE (S_IRUSR)

enum {
	Ttdevdirqid = 0,
	Ttdevbase,
	Ttdevctlqid = Ttdevbase,	// 1
	Ttdevauxqid,				// 2
	Ttdevcpudataqid,			// 3

	Logtype = 4,  // Enough for 16 unique qids types
	Masktype = (1 << Logtype) - 1,
	Shifttype = 0,

	Logcpu = 12, // Upto 4096 cpus can be time traced
	Maskcpu = (1 << Logcpu) - 1,
	Shiftcpu = Shifttype + Logtype,

	/* ttrace timestamp id, used by data file readers */
	Logtsid = 12 + 3, // 4096 cpus by 8 simultaneous opened
	Masktsid = (1 << Logtsid) - 1,
	Shifttsid = Shiftcpu + Logcpu,
};

#define TTYPE(x)		( (int) ((uint32_t)(x).path) & Masktype )
#define TTCPU(q)		( ((q).path >> Shiftcpu) & Maskcpu )
#define TTCPUQID(c, t)	( ((c) << Shiftcpu) | (t))
#define TTTSID(q)		( ((q).path >> Shifttsid) & Masktsid )
#define TTTSIDQID(q, i)	( ((i) << Shifttsid) | (q).path )

/*
 * ttrace timestamp pool and accessor
 */
static uintptr_t *ttdevtimestamp;		// array of open file timestamps
static struct u16_pool *ttdevtspool;	// pool of timestamp indices
static inline int get_tsid(void) {
	return (ttdevtspool)? get_u16(ttdevtspool) : -1;
}
static inline put_tsid(int tsid) {
	dassert(tsid >= 1 && ttdevtspool);
	put_u16(ttdevtspool, tsid);
}

/*
 * ttrace device gen implementation
 *
 * #T directory layout
 * [-1]        {".",      {Ttdevdirqid, 0, QTDIR},    0, TTPERMDIR},
 * [0..ncpu-1] {"cpunnn", {Ttdevcpudataqid|coreid},   0, TTPERMRWFILE},
 * [ncpu]      {"ctl",    {Ttdevctlqid}, TTRACE_CTL_LEN, TTPERMROFILE},
 * [ncpu+1]    {"aux",    {Ttdevauxqid},              0, TTPERMRWFILE},
 */

/* Generate qids for the top level directory */
static inline int ttdev1gen(const struct chan *c, int s, struct qid *qp)
{
	int ret = 1;
	int path = -1;
	/* Must only be called to decode top level dir channel */
	dassert(TTYPE(c->qid) == Ttdevdirqid);

	if (s < num_cpus) // "cpunnn" data files
		path = TTCPUQID(s, Ttdevcpudataqid);
	else {
		switch (s - num_cpus) {
		case 0:  path = Ttdevctlqid; break;		// "ctl"
		case 1:  path = Ttdevauxqid; break;		// "aux"
		default: return -1;
		}
	}
	dassert(path > 0);
	mkqid(qp, path, 0, QTFILE);
	return ret;
}

static int ttdevgen(struct chan *c, char *unused_name,
					struct dirtab *unused_tab, int ntab, int s, struct dir *dp)
{
	dassert(s >= 0); // DOTDOT must handled before getting here

	/* Always return the top kprof dir for '..' */
	if (s == DEVDOTDOT) {
		static const struct qid topqid = {Ttdevdirqid, 0, QTDIR};
		devdir(c, topqid, "#T", 0, eve, TTPERMDIR, dp);
		return 1;
	}

	struct qid q = c->qid;
	if (Ttdevdirqid == TTYPE(q) && ttdev1gen(c, s, &q) < 0)
		return -1;

	const char *name = NULL;
	long perm = TTPERMRWFILE;
	switch (TTYPE(q)) {
	case Ttdevctlqid: name = "ctl"; break;
	case Ttdevauxqid: name = "aux"; perm = TTPERMROFILE; break;
	case Ttdevcpudataqid:
		snprintf(get_cur_genbuf(), GENBUF_SZ, "cpu%03d", TTCPU(q));
		name = get_cur_genbuf();
		break;

	default:
		panic("devttrace: Where did bad qid come from?\n");
	case Ttdevdirqid:
		panic("devttrace: What happened to ttdev1gen decode?\n");
	}
	dassert(name);
	devdir(c, q, (char *) name, 0, eve, perm, dp);
	return 1;
}

/*
 * ttrace read implementation
 */
static size_t ttdevcopyout(char *va, long n, size_t offset,
						   const char *buf, long len)
{
	if (offset + len > n)
		error(Eshort);

	if (!current)
		memcpy(&va[offset], buf, len);
	else if (ESUCCESS != memcpy_to_user(current, &va[offset], buf, len)) {
		/* UMEM */
		// TODO(gvdl): No p9 equivalent to EFAULT, determine causes of failure.
		error(Enovmem);
	}

	return len;
}

/* Context for trace_ring_foreach call of ttdevread_cpu_entry() */
#define TTRACE_ENTRY_QUADS (sizeof(struct ttrace_entry) / sizeof(uint64_t))
/* #quads * (whitespace + len(hex(quad))) */
#define CTXT_GENBUF_SZ     (TTRACE_ENTRY_QUADS * (1 + 2 * sizeof(uint64_t)))

struct ttdevread_cpu_ctxt {
	int64_t c;
	uintptr_t min_timestamp;
	char *va;
	long n;
	char genbuf[CTXT_GENBUF_SZ];
};

static inline int ttdevhexdigit(uint8_t x)
{
	return "0123456789abcdef"[x];
}

static void ttdevread_cpu_entry(void *ventry, void *vctxt)
{
	struct ttdevread_cpu_ctxt *ctxt = (struct ttdevread_cpu_ctxt *) vctxt;
	/* A cache line aligned copy of the input entry, should make partial entrys
	 * less likely. Still an entry is bracketted with timestamp == -1 */
	uint8_t buf[2 * sizeof(struct ttrace_entry)];  // 128 byte buffer
	const uintptr_t size_mask = sizeof(struct ttrace_entry) - 1;
	struct ttrace_entry* entry = (struct ttrace_entry *)
		(((uintptr_t) buf + size_mask) & ~size_mask); // align to cache line
	*entry = *((struct ttrace_entry *) ventry);  // Grab the entry

	/* If time stamp == -1 (i.e. entry is a partial) or is less than
	 * the minimum then ignore this entry */
	if (!(entry->timestamp + 1) || entry->timestamp < ctxt->min_timestamp)
		return;

	uint64_t *sqp = (uint64_t *) entry;
	char *dcp = ctxt->genbuf;
	for (int i = 0; i < TTRACE_ENTRY_QUADS; i++) {
		const uint64_t quad = sqp[i];
		dcp[0] = ttdevhexdigit((quad >> 28) & 0xf);
		dcp[1] = ttdevhexdigit((quad >> 24) & 0xf);
		dcp[2] = ttdevhexdigit((quad >> 20) & 0xf);
		dcp[3] = ttdevhexdigit((quad >> 16) & 0xf);
		dcp[4] = ttdevhexdigit((quad >> 12) & 0xf);
		dcp[5] = ttdevhexdigit((quad >>  8) & 0xf);
		dcp[6] = ttdevhexdigit((quad >>  4) & 0xf);
		dcp[7] = ttdevhexdigit((quad >>  0) & 0xf);
		dcp[8] = ' ';
		dcp += 9;
	}
	dassert(&ctxt->genbuf[sizeof(ctxt->genbuf)] == dcp);
	dcp[-1] = '\n';  // Replace trailing space with a newline

	ctxt->c += ttdevcopyout(ctxt->va, ctxt->n, ctxt->c,
			                ctxt->genbuf, sizeof(ctxt->genbuf));
}

/* iotimestamp takes the timestamp pointer and the I/Os offset and returns the
 * minimum timestamp last requested in a write. In the case where the channel
 * has been opened readonly we will complete the offset == 0 request and return
 * end of file for all subsequent (offset > 0) requests; this allows cat to
 * return one page of data. */
static inline uintptr_t ttdevread_mintimestamp(const int tsid, int64_t offset)
{
	/* ttdevread_cpu code can not deal sensibly with offsets without making the
	 * code much more complicated, probably not worth it. */ 
	if (offset)
		return 0;

	const uintptr_t min_timestamp = ttdevtimestamp[tsid];
	if (min_timestamp > read_tscp()) {
		// no point in trying to read the future.
		error(Ebadarg);
	}

	return min_timestamp;
}

static inline long ttdevread_cpu(const int tsid,
								 int coreid, void *va, long n, int64_t offset)
{
	ERRSTACK(1);
	const uintptr_t min_timestamp = ttdevread_mintimestamp(tsid, offset);
	if (!min_timestamp)
		return 0;

	struct ttdevread_cpu_ctxt *ctxt = kzalloc(sizeof(*ctxt), KMALLOC_WAIT);
	if (!ctxt)
		error(Enomem);
	else if (waserror()) {
		kfree(ctxt);
		nexterror();
	}

	ctxt->min_timestamp = min_timestamp
	ctxt->va = va;
	ctxt->n = n;

	struct trace_ring * const ring = get_ttrace_ring_for_core(coreid);
	trace_ring_foreach(ring, &ttdevread_cpu_entry, ctxt);

	kfree(ctxt);
	poperror();
	return ctxt->c;
}

static inline long ttdevread_ctl(void *va, long n, int64_t offset)
{
	/* Read the ttrace_type_mask and create a 'setmask' ctl command
	 *
	 * cmd     ttrace_bits       bit mask
	 * setmask 0x0123456789abcdef 0x0123456789abcdef\n"
	 * 123456789012345678901234567890123456789012345 6  len 46 bytes
	 */
	char * const buffer = get_cur_genbuf();
	static_assert(TTRACE_CTL_LEN <= GENBUF_SZ);

	int c = snprintf(buffer, GENBUF_SZ, "setmask 0x%016llx 0x%016llx\n",
					 ttrace_type_mask & TTRACE_TYPE_MASK, TTRACE_TYPE_MASK);
	dassert(TTRACE_CTL_LEN == c);

	return readstr(offset, va, n, buffer);
}

/* This code will be more efficient if the user data is page aligned, but
 * should work no matter which alignment the use gives.
 * Output:
 *   Page 0:   struct ttrace_version
 *   Page 1-n: Auxillary buffer.
 */
static inline long ttdevread_aux(uint8_t *va, long n, int64_t offset)
{
	ptrdiff_t dummy_offset;
	struct ttrace_version vers;
	fill_ttrace_version(&vers);

	const long buffer_length = vers.buffer_mask + 1;
	if (offset)
		return 0; // Only allow single reads at offset 0, all others are empty
	else if (n < PGSIZE + buffer_length)
		error(Etoosmall);

	size_t c = PGSIZE; // Advance count to second page

	/* Implements reader side of auxillary buffer protocol, see
	 * _ttrace_point_string comment in ttrace.c
	 *
	 * Touch memory to get any page faults out of the way now, hopefully we
	 * will not be under paging pressure. Note that I'm accumulating into the
	 * vers.last_offset so that the compiler doesn't throw out the memory touch
	 * loop, the vers.last_offset is reset when we take a buffer snapshot.
	 *
	 * TODO(gvdl): formalise memory pinning for later I/O.
	 */
	vers.last_offset = 0;
	size_t t = PGSIZE + ((uintptr_t) va & (sizeof(long) - 1));
	for (t = 0; t < n, t += PGSIZE)
		vers.last_offset += atomic_read((atomic_t *) va[t]);

	get_ttrace_aux_buffer_snapshot(&dummy_offset, &vers.last_offset);
	const uint8_t * const aux_buffer = get_ttrace_aux_buffer();
	c += ttdevcopyout(va, n, c, aux_buffer, buffer_length);
	get_ttrace_aux_buffer_snapshot(&vers.first_offset, &dummy_ffset);

	/* Output version with buffer offsets last */
	ttdevcopyout(va, n, 0, &vers, sizeof(vers));

	return c;
}

/*
 * ttrace write utility routines and macros
 */
static uint64_t parseul(const char * const num_str, int base)
{
	char *end_num = NULL;
	uint64_t ret = strtoul(num_str, &end_num, base);
	if (num_str == end_num)
		error(Ebadarg);
	return ret;
}

/*
 * ttrace devtab entry points
 */
static void ttdevinit(void)
{
	static_assert(MAX_NUM_CPUS <= Maskcpu);  // Assert encoding is still good

	/* Support upto 8 simultaneous opens on the ttrace/cpunnn files. */
	const int pool_size
		= min(TTRACE_MAX_TSID, TTRACE_NUM_OPENERS * num_cpus);
	/* Test for too many cpus for our tsid mechanism, re-implement. */
	dassert(num_cpus <= pool_size);
	if (num_cpus > pool_size) {
		printk("Insufficient ids for ttrace timestamp pool");
		return;
	}

	const size_t ts_size = pool_size * sizeof(*ttdevtimestamp);
	ttdevtimestamp = kmalloc(ts_size, KMALLOC_WAIT);
	memset(ttdevtimestamp, 0xff, ts_size);
	ttdevtspool = create_u16_pool(pool_size);

	/* Always allocate 0 as a unused/NULL sentinel */
	int tsidnull = get_tsid(ttdevtspool);
	assert(!tsidnull);
	ttdevtimestamp[tsidnull] = 1;  // tsid[0] is set to timestamp of 1.
}

#define KFREE_AND_NULL(x) do { kfree(x); x = NULL; } while(false)
static void ttdevshutdown(void)
{
	KFREE_AND_NULL(ttdevtspool);
	KFREE_AND_NULL(ttdevtimestamp);
}

static struct chan *ttdevattach(char *spec)
{
	return devattach('T', spec);
}

static struct walkqid *ttdevwalk(struct chan *c, struct chan *nc,
								 char **name, int nname)
{
	return devwalk(c, nc, name, nname, NULL, 0, ttdevgen);
}

static int ttdevstat(struct chan *c, uint8_t *db, int n)
{
	int ret = devstat(c, db, n, NULL, 0, ttdevgen);
	return ret;
}

static struct chan *ttdevopen(struct chan *c, int omode)
{
	const int o = openmode(omode);
	int tsid = TTTSID(c->qid);
	switch(TTYPE(c->qid)) {
	default:
		assert(false); // How did a bad chan get to us?

	case Ttdevdirqid:
		dassert(c->qid.type & QTDIR);
		if (openmode(omode) != OREAD)
			error(Eperm);
		break;

	case Ttdevcpudataqid:
		if (tsid)
			break; // Already allocated, reopen

		if (o == O_RDWR) {
			tsid = get_tsid(ttdevtspool);
			if (tsid < 0)
				error(Enoenv);
			else
				dassert(tsid);
			ttdevtimestamp[tsid] = 1;
			c->qid.path = TTTSIDQID(c->qid, tsid);  // Record tsid
		} else if (o == O_RDONLY) {
			// Nothing to do for O_RDONLY
		} else
			error(Eperm);
		break;

	case Ttdevauxqid:
	case Ttdevctlqid:
		break;
	}

	c->mode = o;
	c->flag |= COPEN;
	c->offset = 0;

	return c;
}

static void ttdevclose(struct chan *c)
{
	if (!(c->flag & COPEN))
		return;

	const int tsid = TTTSID(c->qid);
	switch (TTYPE(c->qid)) {
	case Ttdevcpudataqid:
		/* Release timestamp */
		if (tsid) {
			ttdevtimestamp[tsid] = -1;
			put_tsid(ttdevtspool, tsid);
		}
		break;
	case Ttdevauxqid:
	case Ttdevctlqid:
	case Ttdevdirqid:
		break;
	default:
		assert(false);
	}
}

static long ttdevread(struct chan *c, void *va, long n, int64_t offset)
{
	const unsigned tsid = TTTSID(c->qid);
	assert(tsid < ttdevtspool->size);

	switch (TTYPE(c->qid)) {
	case Ttdevdirqid:  // ttrace directory read
		return devdirread(c, va, n, NULL, 0, ttdevgen);
	case Ttdevctlqid:
		return ttdevread_ctl(va, n, offset);
	case Ttdevauxqid:
		return ttdevread_aux(va, n, offset);
	case Ttdevcpudataqid:
		return ttdevread_cpu(tsid, TTCPU(c->qid), va, n, offset);
	}
	return 0; // Not us
}

#define CONST_STRNCMP(vp, conststr) strncmp((vp), conststr, sizeof(conststr)-1)
static long ttdevwrite(struct chan *c, void *a, long n, int64_t unused_off)
{
	ERRSTACK(1);
	uintptr_t pc;
	struct cmdbuf *cb;
	static const char ctlstring[]
		= "setmask <value> <mask>|setbits <value>|clrbits <mask>";
	static const char tsstring[] = "settimestamp <minimum timestamp>";

	cb = parsecmd(a, n);
	if (waserror()) {
		kfree(cb);
		nexterror();
	}

	const unsigned tsid = TTTSID(c->qid);
	assert(tsid < ttdevtspool->size);

	uint64_t mask = TTRACE_TYPE_MASK;
	uint64_t value = 0;

	switch(TTYPE(c->qid)) {
	default:
		error(Ebadusefd);

	case Ttdevctlqid:
		if (cb->nf == 3 && !CONST_STRNCMP(cb->f[0], "setmask")) {
			value = parseul(cb->f[1], 0);
			mask &= parseul(cb->f[2], 0);
		} else if (cb->nf == 2 && !CONST_STRNCMP(cb->f[0], "setbits"))
			value = parseul(cb->f[1], 0);
		else if (cb->nf == 2 && !CONST_STRNCMP(cb->f[0], "clrbits"))
			mask &= parseul(cb->f[1], 0);
		else
			error(ctlstring);

		/* Thread safe, but... lets face it if we have competing controllers
		 * setting and clearing mask bits then the behaviour is going to be
		 * unexpected. Perhaps we could enforce exclusive open of the ctl
		 * channel. */
		{
			uint64_t cur_mask, new_mask;
			do {
				cur_mask = atomic_read((void **) &ttrace_type_mask);
				new_mask = (cur_mask & ~mask) | (value & mask);
			} while (!atomic_cas((void **) &ttrace_type_mask,
								 cur_mask, new_mask));
		}
		break;

	case Ttdevcpudataqid:
		if (cb->nf == 2 && !CONST_STRNCMP(cb->f[0], "settimestamp")) {
			if (!tsid) error(Ebadfd);

			const char *endptr = NULL;
			const unsigned long ts = parseul(cb->f[1], /* base */ 0);
			if (ts > read_tscp()) // Is the timestamp in the future.
				error(Ebadarg);
			ttdevtimestamp[tsid] = ts;
		} else
			error((char *) tsstring);
		break;
	}

	kfree(cb);
	poperror();
	return n;
}

struct dev ttdevdevtab __devtab = {
	'T',
	"ttrace",

	devreset,
	ttdevinit,
	ttdevshutdown,
	ttdevattach,
	ttdevwalk,
	ttdevstat,
	ttdevopen,
	devcreate,
	ttdevclose,
	ttdevread,
	devbread,
	ttdevwrite,
	devbwrite,
	devremove,
	devwstat,
};
