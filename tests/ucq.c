#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <sys/mman.h>
#include <ucq.h>

int main(int argc, char** argv)
{
	/* this program should only be started from the kernel for tests */
	printf("Attempting to read ucq messages from test_ucq().  "
	       "Don't call this manually.\n");
	/* Map into a known, extremely ghetto location.  The kernel knows to look
	 * here. */
	struct ucq *ucq = mmap((void*)USTACKTOP, PGSIZE, PROT_WRITE | PROT_READ,
	                       MAP_POPULATE, -1, 0);
	assert((uintptr_t)ucq == USTACKTOP);
	/* Now init it */
	uintptr_t two_pages = (uintptr_t)mmap(0, PGSIZE * 2, PROT_WRITE | PROT_READ,
	                                      MAP_POPULATE, -1, 0);
	assert(two_pages);
	ucq_init(ucq, two_pages, two_pages + PGSIZE);
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
	printf("Received a bunch!  Last one was %d(4999)\n", msg.ev_type);
	return 0;
}
