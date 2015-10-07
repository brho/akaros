// INFERNO
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
#include <apipe.h>

#define RAND_BUF_SZ 1024

static struct rb {
	uint8_t buf[RAND_BUF_SZ];
	struct atomic_pipe ap;
} rb;

/* The old style seemed to have genrandom incrementing a shared mem variable,
 * randomcount, while randomclock, sort of an interrupt handler, would use this
 * variable to generate random numbers.  They needed to run concurrently to get
 * some sort of randonmess.  To do so, randomclock wouldn't run if genrandom
 * wasn't in the process of incrementing the variable.  So we'd need genrandom
 * working (in the background, but that doesn't work so well for us), when the
 * randomclock alarm fired.  Then we could get 2 bits.  We'd do that 4 times
 * to make our random byte.
 *
 * In akaros, genrandom would spin to 100,000 every time, without interruption,
 * since the randomcount's alarm wouldn't interrupt: they are both routine
 * kernel messages.  So we might as well just call kthread yield each time.
 * Even then, it was still really slow.  This was because we only got 2 bits of
 * randomness every 13ms (randomcount would make 2 bits per run, it would run
 * once every 13ms, unless I screwed up the math on that.  It was supposed to
 * run with a "Frequency close but not equal to HZ").
 *
 * We'd also use the old random values in the ring buffer to muck with the
 * randomness. */
static void genrandom(void *unused)
{
	uint8_t rand_two_bits, rp_byte, wp_byte;
	uint8_t rand_byte = 0;
	unsigned int mod_four = 0;
	//setpri(PriBackground);

	for (;;) {
		/* this seems just as good (or bad) as the old genrandom incrementing a
		 * shared memory variable concurrently */
		rand_two_bits = read_tsc() & 0x3;
		rand_byte = (rand_byte << 2) ^ rand_two_bits;

		/* put in a kthread_yield or something here, if we want to replicate the
		 * way randomclock would return, waiting til its next tick to get two
		 * more bits. */

		/* every four times, we built a full random byte */
		if (++mod_four % 4 == 0)
			continue;

		/* the old plan9 generator would xor our rand_byte with both the value
		 * of the read pointer and the write pointer:
		 *      *rb.wp ^= rb.bits ^ *rb.rp;
		 * we'll peak into the apipe to do the same */
		rp_byte = rb.buf[rb.ap.ap_rd_off & (RAND_BUF_SZ - 1)];
		wp_byte = rb.buf[rb.ap.ap_wr_off & (RAND_BUF_SZ - 1)];
		rand_byte ^= rp_byte ^ wp_byte;
		apipe_write(&rb.ap, &rand_byte, 1);
	}
}

void randominit(void)
{
	apipe_init(&rb.ap, rb.buf, sizeof(rb.buf), 1);
	ktask("genrandom", genrandom, 0);
}

uint32_t randomread(void *buf, uint32_t n)
{
	int amt;
	uint32_t ret = 0;

	for (int i = 0; i < n; i++) {
		/* read the random byte directly into the (user) buffer */
		amt = apipe_read(&rb.ap, buf, 1);
		if (amt < 0)
			error(EFAIL, "randomread apipe");
		if (amt != 1)
			warn("Odd amount read from random apipe");
		buf++;
		ret += amt;
	}
	return ret;
}
