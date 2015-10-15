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

#include <utest/utest.h>

TEST_SUITE("FILE POSIX");

/* <--- Begin definition of test cases ---> */

bool test_openat(void)
{
	int dfd = open("/dir1", O_RDONLY);
	UT_ASSERT(dfd >= 0);

	int ffd = openat(dfd, "f1.txt", O_RDWR);
	UT_ASSERT(ffd >= 0, close(dfd));

	close(ffd);
	close(dfd);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(openat),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
