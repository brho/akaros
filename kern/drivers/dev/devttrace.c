/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

//
// This file implements the #T device and was based upon the UCB Plan 9 kprof.c
//

#include <assert.h>
#include <atomic.h>
#include <kmalloc.h>
#include <ns.h>
#include <smallidpool.h>
#include <smp.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <umem.h>

#include <ros/fs.h>
#include <ros/ttrace.h>

#warning Remove this scaffold when ttrace profiling is going
uint64_t ttrace_type_mask;

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

// TODO(gvdl): I don't get plan 9's permissions, why do directories get group
// rx permissions, and what's with the DMDIR. Some devices use it and others
// don't. In theory the DMDIR is copied over by a higher layer but I have no
// idea why two copies seems necessary.
#define TTPERMDIR    (S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|DMDIR)
#define TTPERMRWFILE (S_IRUSR|S_IWUSR)
#define TTPERMROFILE (S_IRUSR)

enum {
	Ttdevdirqid = 0,
	Ttdevbase,
	Ttdevctlqid = Ttdevbase,	// 1
	Ttdevdataqid,				// 2
	Ttdevcpudataqid,			// 3

	Logtype = 4,  // Enough for 16 unique qids types
	Masktype = (1 << Logtype) - 1,
	Shifttype = 0,

	Logcpu = 12, // Upto 4096 cpus can be time traced
	Maskcpu = (1 << Logcpu) - 1,
	Shiftcpu = Shifttype + Logtype,

	// ttrace timestamp id, used by data file readers
	Logtsid = 13 + 3, // 4096 ttrace/cpus + a ttrace/data file by 8 opened
	Masktsid = (1 << Logtsid) - 1,
	Shifttsid = Shiftcpu + Logcpu,
};

#define TTYPE(x)		( (int) ((uint32_t)(x).path) & Masktype )
#define TTCPU(q)		( ((q).path >> Shiftcpu) & Maskcpu )
#define TTCPUQID(c, t)	( ((c) << Shiftcpu) | (t))
#define TTTSID(q)		( ((q).path >> Shifttsid) & Masktsid )
#define TTTSIDQID(q, i)	( ((i) << Shifttsid) | (q).path )

/*
 * ttrace static data
 */
static uintptr_t *ttdevtimestamp;		// array of open file timestamps
static struct u16_pool *ttdevtspool;		// pool of timestamp indices

//
// ttrace device gen implementation
//
// #T directory layout
// [-1]        {".",      {Ttdevdirqid, 0, QTDIR},    0, TTPERMDIR},
// [0..ncpu-1] {"cpunnn", {Ttdevcpudataqid|core_id},  0, TTPERMRWFILE},
// [ncpu]      {"ctl",    {Ttdevctlqid}, TTRACE_CTL_LEN, TTPERMRWFILE},
// [ncpu+1]    {"data",   {Ttdevdataqid},             0, TTPERMRWFILE},

// Generate qids for the top level directory
static inline int ttdev1gen(const struct chan *c, int s, struct qid *qp)
{
	int ret = 1;
	int path = -1;
	// Must only be called to decode top level dir channel
	dassert(TTYPE(c->qid) == Ttdevdirqid);

	if (s < num_cpus) // "cpunnn" data files
		path = TTCPUQID(s, Ttdevcpudataqid);
	else {
		switch (s - num_cpus) {
		case 0:  path = Ttdevctlqid;  break; // "ctl"
		case 1:  path = Ttdevdataqid; break; // "data"
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
	switch (TTYPE(q)) {
	case Ttdevctlqid:  name = "ctl";  break;
	case Ttdevdataqid: name = "data"; break;
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
	devdir(c, q, (char *) name, 0, eve, TTPERMRWFILE, dp);
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
		// TODO(gvdl): No p9 equivalent to EFAULT, determine causes of failure.
		error(Enovmem);
	}

	return len;
}

static inline int ttdevputchar(char *buf, int len, char c)
{
	dassert(len >= 1);
	*buf = c;
	return 1;
}

static inline int ttdevputhex8(char* buf, int len, uint8_t x)
{
	static const char hex_digits[] = "0123456789abcdef";
	int c = 0;
	c += ttdevputchar(&buf[c], len-c, hex_digits[x >> 4]);
	c += ttdevputchar(&buf[c], len-c, hex_digits[x & 0xf]);
	return c;
}

static inline int ttdevputhex16(char *buf, int len, uint16_t x)
{
	int c = ttdevputhex8(&buf[0], len, x >> 8);
	ttdevputhex8(&buf[c], len-c, (uint8_t) x);
	return sizeof(uint16_t) * 2;
}

static int ttdevputhex32(char *buf, int len, uint32_t x)
{
	int c = ttdevputhex16(&buf[0], len, x >> 16);
	ttdevputhex16(&buf[c], len-c, (uint16_t) x);
	return sizeof(uint32_t) * 2;
}

static int ttdevputhex64(char *buf, int len, uint64_t x)
{
	int c = ttdevputhex32(&buf[0], len, x >> 32);
	ttdevputhex32(&buf[c], len-c, (uint32_t) x);
	return sizeof(uint64_t) * 2;
}

static inline int ttdevputmem(char *buf, int len, const void *mem, int mem_len)
{
	dassert(mem_len <= len);
	memcpy(buf, mem, mem_len);
	return mem_len;
}

static int ttdevputhdr(char *buf, int len,
					   uint8_t rec_len, uint8_t tag, uintptr_t timestamp)
{
	int c = 0;
	c += ttdevputhex8(&buf[c], len-c, rec_len);
	c += ttdevputhex8(&buf[c], len-c, tag);
	c += ttdevputhex64(&buf[c], len-c, timestamp);
	return c;
}

static size_t ttdev_readnamerec(void *va, long n, uint8_t tag,
								uintptr_t timestamp, uint64_t id,
								const char *name)
{
	const int len = TT_SAFE_GENBUF_SZ; // Always leave room for newline
	char * const buffer = get_cur_genbuf();
	int remaining = strlen(name);
	size_t out_len = 0;

	/* Output first header */
	int nc = min(len - TTRACEH_NAME_LEN, remaining);
	int rec_len = TTRACEH_NAME_LEN + nc; dassert(rec_len <= len);
	int c = 0;
	c += ttdevputhdr(&buffer[c], len-c, rec_len, tag, timestamp);
	c += ttdevputhex64(&buffer[c], len-c, id);
	c += ttdevputmem(&buffer[c], len-c, name, nc);
	c += ttdevputchar(&buffer[c], len+1-c, '\n');
	out_len = ttdevcopyout(va, n, out_len, buffer, c);

	tag |= TTRACEH_TAG_CONT;
	const int buf_nm_len = len - TTRACEH_CONT_LEN;
	while ((remaining -= nc) > 0) {
		name += nc;

		nc = min(buf_nm_len, remaining);
		rec_len = TTRACEH_CONT_LEN + nc; dassert(rec_len <= len);
		c = 0;
		c += ttdevputhex8(&buffer[c], len-c, rec_len);
		c += ttdevputhex8(&buffer[c], len-c, tag);
		c += ttdevputmem(&buffer[c], len-c, name, nc);
		c += ttdevputchar(&buffer[c], len+1-c, '\n');
		out_len += ttdevcopyout(va, n, out_len, buffer, c);
	}
	return out_len;
}

static size_t ttdevread_info(void *va, long n, uintptr_t timestamp)
{
	char * const buffer = get_cur_genbuf();
	const int len = TT_SAFE_GENBUF_SZ; /* Room for new line */
	const int rec_len = TTRACEH_LEN + 12;  /* header + 3*h[4] versions */
	dassert(rec_len <= len);
	int c = 0;

	c += ttdevputhdr(&buffer[c], len-c, rec_len, TTRACEH_TAG_INFO, timestamp);
	c += ttdevputhex16(&buffer[c], len-c, TTRACEH_V1);
	c += ttdevputhex16(&buffer[c], len-c, TTRACEE_V1);
	c += ttdevputhex16(&buffer[c], len-c, num_cpus);
	dassert(c == rec_len); // Don't count '\n' in rec_len
	c += ttdevputchar(&buffer[c], len+1-c, '\n'); // Always have room for newline

	return ttdevcopyout(va, n, /* offset */ 0, buffer, c);
}

// iotimestamp takes the timestamp pointer and the I/Os offset and returns the
// minimum timestamp last requested in a write. In the case where the channel
// has been opened readonly we will complete the offset == 0 request and return
// end of file for all subsequent (offset > 0) requests; this allows cat to
// return one page of data.
static inline uintptr_t ttdevread_mintimestamp(const int tsid, int64_t offset)
{
	// If we don't have a timestamp, then only satisfy a single I/O, that is
	// anything with an offset of 0
	if (!tsid && offset)
		return 0;

	const uintptr_t min_timestamp = ttdevtimestamp[tsid];
	if (min_timestamp > read_tscp()) {
		// no point in trying to read the future.
		error(Ebadarg);
	}

	return min_timestamp;
}

static inline long ttdevread_ctl(void *va, long n, int64_t offset)
{
	// Read the ttrace_type_mask and create a 'setmask' ctl command
	//
	// cmd     ttrace_bits       bit mask
	// setmask 0x0123456789abcdef 0x0123456789abcdef\n"
	// 123456789012345678901234567890123456789012345 6  len 46 bytes
	char * const buffer = get_cur_genbuf();
	static_assert(TTRACE_CTL_LEN <= GENBUF_SZ);

	int c = snprintf(buffer, GENBUF_SZ, "setmask 0x%016llx 0x%016llx\n",
					 ttrace_type_mask & TTRACE_TYPE_MASK, TTRACE_TYPE_MASK);
	dassert(TTRACE_CTL_LEN == c);

	return readstr(offset, va, n, buffer);
}

static inline long ttdevread_data(const int tsid,
								  uint8_t *va, long n, int64_t offset)
{
	const int min_timestamp = ttdevread_mintimestamp(tsid, offset);
	if (!min_timestamp)
		return 0;

	// All data requested, Copy out basic data: version ids and syscall table
	size_t c = 0;  // Number of characters output
	if (min_timestamp == 1) {
		c += ttdevread_info(&va[c], n - c, min_timestamp);
		for (size_t i = 0; i < max_syscall; i++) {
			const char * const name = syscall_table[i].name;
			if (!name) continue;

			c += ttdev_readnamerec(&va[c], n - c, TTRACEH_TAG_SYSC,
								   min_timestamp, i, name);
		}
	}

	// Read ttrace data file, contains names and other data, this data is slow
	// to store and generate and is expected to be done only rarely. Such as at
	// process, semaphore, ktask creation.
	return c;
}

static inline long ttdevread_cpu(const int tsid,
								 int core_id, void *va, long n, int64_t offset)
{
	const int min_timestamp = ttdevread_mintimestamp(tsid, offset);
	if (!min_timestamp)
		return 0;

#warning Scaffolding, test cpu timestamp reading
	// Read the timestamp and output it in decimal (max 20 digits).
	// cmd   timestamp
	// setts 12345678901234567890\n"
	// 12345678901234567890123456 7  len 27
	char * const buffer = get_cur_genbuf();
	static_assert(27 <= GENBUF_SZ);

	int c = snprintf(buffer, GENBUF_SZ, "setts %lld\n", min_timestamp);
	dassert(c <= 27);

	return readstr(offset, va, n, buffer);
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

#define CONST_STRNCMP(vp, conststr) strncmp((vp), conststr, sizeof(conststr)-1)

/*
 * ttrace devtab entry points
 */
static void ttdevinit(void)
{
	static_assert(MAX_NUM_CPUS <= Maskcpu);  // Assert encoding is still good

	// Support upto 8 simultaneous opens on the ttrace/{data,cpu*} files.
	const int pool_size
		= min(TTRACE_MAX_TSID, TTRACE_NUM_OPENERS * (1 + num_cpus));
	// Test for too many cpus for our tsid mechanism, re-implement.
	dassert(1 + num_cpus <= pool_size);
	if (1 + num_cpus > pool_size) return;

	const size_t ts_size = pool_size * sizeof(*ttdevtimestamp);
	ttdevtimestamp = kmalloc(ts_size, KMALLOC_WAIT);
	memset(ttdevtimestamp, 0xff, ts_size);
	ttdevtspool = create_u16_pool(pool_size);

	// Always allocate 0 as a unused/NULL sentinel
	int tsidnull = get_u16(ttdevtspool);
	assert(!tsidnull);
	ttdevtimestamp[tsidnull] = 1;  // tsid==0 always has a 1 minimum timestamp.
}

#define kfree_and_null(x) do { kfree(x); x = NULL; } while(false)
static void ttdevshutdown(void)
{
	kfree_and_null(ttdevtspool);
	kfree_and_null(ttdevtimestamp);
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

	case Ttdevdataqid:
	case Ttdevcpudataqid:
		if (tsid)
			break; // Already allocated, reopen

		// Allocate a timestamp from the pool
		if (o == O_RDWR) {
			tsid = get_u16(ttdevtspool);
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
	case Ttdevdataqid:
	case Ttdevcpudataqid:
		// Release timestamp
		if (tsid) {
			ttdevtimestamp[tsid] = -1;
			put_u16(ttdevtspool, tsid);
		}
		break;
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
	case Ttdevdataqid:
		return ttdevread_data(tsid, va, n, offset);
	case Ttdevcpudataqid:
		return ttdevread_cpu(tsid, TTCPU(c->qid), va, n, offset);
	}
	return 0; // Not us
}

static long ttdevwrite(struct chan *c, void *a, long n, int64_t unused_off)
{
	ERRSTACK(1);
	uintptr_t pc;
	struct cmdbuf *cb;
	static const char ctlstring[]
		= "setmask <value> <mask>|setbits <value>|clrbits <mask>";
	static const char tsstring[] = "setts <minimum timestamp>";

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

		// Thread safe, but... lets face it if we have competing controllers
		// setting anc clearing mask bits then the behaviour is going to be
		// unexpected. Perhaps I should enforce exclusive open of the ctl
		// channel.
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
	case Ttdevdataqid:
		if (cb->nf == 2 && !CONST_STRNCMP(cb->f[0], "setts")) {
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
