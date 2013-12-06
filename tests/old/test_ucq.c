#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <sys/mman.h>
#include <ucq.h>
#include <assert.h>
#include <arch/atomic.h>

int main(int argc, char** argv)
{
	/* this program should only be started from the kernel for tests */
	printf("[user] Attempting to read ucq messages from test_ucq().  "
	       "Don't call this manually.\n");
	/* Map into a known, extremely ghetto location.  The kernel knows to look
	 * here. */
	struct ucq *ucq = mmap((void*)USTACKTOP, PGSIZE, PROT_WRITE | PROT_READ,
	                       MAP_POPULATE, -1, 0);
	assert((uintptr_t)ucq == USTACKTOP);
	/* Now init it */
	uintptr_t two_pages = (uintptr_t)mmap(0, PGSIZE * 2, PROT_WRITE | PROT_READ,
	                                      MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
	assert(two_pages);
	ucq_init_raw(ucq, two_pages, two_pages + PGSIZE);
	printf("[user] UCQ %08p initialized\n", ucq);
	/* try to get a simple message */
	struct event_msg msg;
	/* 1: Spin til we can get a message (0 on success breaks) */
	while (get_ucq_msg(ucq, &msg))
		cpu_relax();
	printf("[user] Got simple message type %d(7) with A2 %08p(0xdeadbeef)\n",
	       msg.ev_type, msg.ev_arg2);
	/* 2: get a bunch */
	for (int i = 0; i < 5000; i++) {
		while (get_ucq_msg(ucq, &msg))
			cpu_relax();
		assert(msg.ev_type == i);
	}
	printf("[user] #2 Received a bunch!  Last one was %d(4999), "
	       "extra pages %d(6, if #3 is 1000 and was blasted already)\n",
	       msg.ev_type, atomic_read(&ucq->nr_extra_pgs));
	/* 3: test chaining */
	while (atomic_read(&ucq->nr_extra_pgs) < 2)
		cpu_relax();
	printf("[user] #3 There's now a couple pages (%d), trying to receive...\n",
	       atomic_read(&ucq->nr_extra_pgs));
	/* this assumes 1000 is enough for a couple pages */
	for (int i = 0; i < 1000; i++) {
		while (get_ucq_msg(ucq, &msg))
			cpu_relax();
		assert(msg.ev_type == i);
	}
	printf("[user] Done, extra pages: %d(0)\n", atomic_read(&ucq->nr_extra_pgs));
	int extra = 0;
	while (!get_ucq_msg(ucq, &msg)) {
		printf("[user] got %d extra messages in the ucq, type %d\n", ++extra,
		       msg.ev_type);
	}
	printf("[user] Spinning...\n");
	while(1);

	return 0;
}
