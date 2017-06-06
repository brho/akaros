#include <utest/utest.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <limits.h>
#include <signal.h>
#include <pthread.h>
#include <setjmp.h>

TEST_SUITE("EFENCE");

/* <--- Begin definition of test cases ---> */

/* The guts of this test came from electric fence's tstheap:
 *
 *  Electric Fence - Red-Zone memory allocator.
 *  Bruce Perens, 1988, 1993
 *
 *  For email below, drop spaces and <spam-buster> tag.
 *  MODIFIED:  March 20, 2014 (jric<spam-buster> @ <spam-buster> chegg DOT com)
 */

#define	POOL_SIZE 1024
#define	LARGEST_BUFFER 30000
#define	TEST_DURATION 1000

static void *pool[POOL_SIZE];

bool test_alloc_and_free(void)
{
	void **element;
	size_t size;

	for (int count = 0; count < TEST_DURATION; count++) {
		element = &pool[(int)(drand48() * POOL_SIZE)];
		size = (size_t)(drand48() * (LARGEST_BUFFER + 1));
		if (*element) {
			free(*element);
			*element = 0;
		} else if (size > 0) {
			*element = malloc(size);
			*(uint8_t*)(*element) = 0xab;
			*(uint8_t*)(*element + size - 1) = 0xcd;
		}
	}
	/* Surviving without page faulting is success. */
	return TRUE;
}

/* The pointer Needs to be volatile, so that blob = malloc() gets assigned
 * before the fault. */
static char *volatile blob;
static jmp_buf save;
static void *fault_addr;

static void segv_action(int signr, siginfo_t *si, void *arg)
{
	fault_addr = si->si_addr;
	longjmp(save, 1);
}

static struct sigaction sigact = {.sa_sigaction = segv_action, 0};

bool test_catching_fault(void)
{
	pthread_yield();	/* link in pth for intra-thread signals (SIGSEGV) */
	sigaction(SIGSEGV, &sigact, 0);
	blob = malloc(PGSIZE);
	if (!setjmp(save)) {
		/* First time through, we'll try to pagefault. */
		blob[PGSIZE + 1] = 0;
		UT_ASSERT_FMT("Tried to fault, but didn't!", FALSE);
	}
	/* Second time, we'll return via setjmp */
	UT_ASSERT_FMT("Fault addr was %p, should be %p",
	              fault_addr == blob + PGSIZE + 1, fault_addr,
	              blob + PGSIZE + 1);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(alloc_and_free),
	UTEST_REG(catching_fault),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
