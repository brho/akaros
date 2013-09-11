//#define DEBUG
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
#include <schedule.h>
#include <apipe.h>

#define RAND_BUF_SZ 1024

struct rb {
	uint8_t buf[RAND_BUF_SZ];
	struct atomic_pipe ap;
	uint32_t randn;
} rb;

static void genrandom(uint32_t srcid, long a0, long a1, long a2)
{
	unsigned int counter = 0;
	uint16_t bits = 0;
	uint32_t where = 0;
	for (;;) {
		uint16_t new;
		/* magic number from ron.  the only thing truly random in here!  Right
		 * now, all this does is slow down our production of random numbers - we
		 * don't actually use this timing info for anything. */
		udelay_sched(772);
		/* the 1 used to be randomcount.  though it was always 1, here and in
		 * the old plan9 devrandom */
		bits = (bits << 2) ^ 1;
		/* only produce a random byte every four wakeups.  not sure why. */
		counter++;
		if (counter != 4)
			continue;
		counter = 0;
		/* where indexes our previous entries pushed in the pipe.  we end up
		 * looking at the first entry we are about to fill (assuming only one
		 * producer), which means we'll look at the oldest entry in the pipe
		 * (RAND_BUF_SZ entries ago, in this case).
		 *
		 * we use this old byte for the upper half of the two bytes we produce,
		 * and use the lower byte of the tsc for the lower half. */
		new = rb.buf[where++ % RAND_BUF_SZ];
		new <<= 8;
		new |= read_tsc() & 0xff;
		new ^= bits;
		apipe_write(&rb.ap, &new, 2);
	}
}

void randominit(void)
{
	/* Frequency close but not equal to HZ */
	apipe_init(&rb.ap, rb.buf, sizeof(rb.buf), 1);
	send_kernel_message(core_id(), genrandom, 0, 0, 0, KMSG_ROUTINE);
}

/*
 *  consume random bytes from a circular buffer
 */
uint32_t randomread(void *xp, uint32_t n)
{
	ERRSTACK(2);
	uint8_t *e, *p;
	uint32_t x;
	int amt;
	p = xp;
	if (waserror()) {
		nexterror();
	}

	for (e = p + n; p < e;) {
		amt = apipe_read(&rb.ap, p, 1);
		if (amt < 0)
			error("randomread apipe");
		/* This is all bullshit, and we really don't have any randomness in the
		 * system.  This is some leftover hacked stuff from plan9. */
		/*
		 *  beating clocks will be predictable if they are
		 *  synchronized.  Use a cheap pseudo random number
		 *  generator to obscure any cycles.
		 */
		x = (rb.randn + 1) * 1103515245;
		*p++ = rb.randn = x;

	}
	poperror();

	return n;
}
