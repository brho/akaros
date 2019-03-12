#define _LARGEFILE64_SOURCE /* needed to use lseek64 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <utest/utest.h>

/* This test will run with a 2LS */
#include <parlib/parlib.h>
#include <parlib/uthread.h>

TEST_SUITE("MMAP PAGE FAULT");

/* <--- Begin definition of test cases ---> */

char global_char;

/* Hacky helper to start flushing the page cache.  It will never stop btw. */
static void turn_on_flusher(void)
{
	int ret;
	int fd = open("#regress/mondata", O_RDWR);
	const char msg[] = "fs pmflusher";

	assert(fd >= 0);
	ret = write(fd, msg, sizeof(msg));
	assert(ret == sizeof(msg));
}

/* This test isn't guaranteed to trigger a PF on an mmap'd file, but it's
 * likely.  This is close coupled to KFS (hello.txt), #regress, and the behavior
 * of pm_flusher. */
bool test_pf(void)
{
	int fd;
	void *addr;

	fd = open("hello.txt", O_READ);
	UT_ASSERT(fd >= 0);
	addr = mmap(0, 4096, PROT_READ, MAP_SHARED, fd, 0);
	UT_ASSERT(addr != MAP_FAILED);
	turn_on_flusher();
	for (int i = 0; i < 100; i++) {
		global_char = *(char*)addr;
		uthread_usleep(1000);
	}
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(pf),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	//parlib_wants_to_be_mcp = FALSE; // XXX should be able to do this!
	uthread_mcp_init();
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
