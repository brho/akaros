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
#include <parlib/parlib.h>

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

/* This tests opening a lot of files, enough to grow our file/chan table, then
 * forking/spawning with DUP_FGRP.  It caused a panic at one point. */
bool test_open_lots_and_spawn(void)
{
	char *p_argv[] = {0, 0, 0};
	char *p_envp[] = {"LD_LIBRARY_PATH=/lib", 0};
	#define NR_LOOPS 128
	int fd[NR_LOOPS];
	int pid;
	const char *filename = "/bin/hello";

	/* the kernel-internal number is 32 at the moment. */
	for (int i = 0; i < NR_LOOPS; i++) {
		fd[i] = open("hello.txt", O_RDONLY);
		UT_ASSERT(fd[i] >= 0);
	}
	pid = sys_proc_create(filename, strlen(filename), p_argv, p_envp,
	                      PROC_DUP_FGRP);
	UT_ASSERT(pid > 0);
	sys_proc_destroy(pid, 0);
	for (int i = 0; i < NR_LOOPS; i++)
		close(fd[i]);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(openat),
	UTEST_REG(open_lots_and_spawn),
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	// Run test suite passing it all the args as whitelist of what tests to run.
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;
	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}
