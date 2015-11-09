
#include <ros/arch/arch.h>
#include <ros/arch/msr-index.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <utest/utest.h>

TEST_SUITE("DEVARCH");

/* <--- Begin definition of test cases ---> */

static bool test_msr(void)
{
	static const int max_cpus = MAX_NUM_CORES;
	int fd = open("#arch/msr", O_RDWR);
	uint64_t *values = malloc(max_cpus * sizeof(uint64_t));
	uint64_t tmp = 0;
	ssize_t n;

	UT_ASSERT_M("Failed to open MSR device file (#arch/msr)", fd >= 0);
	UT_ASSERT_M("Failed to allocated MSR values memory", values);

	n = pread(fd, values, max_cpus * sizeof(uint64_t), MSR_IA32_TSC);
	UT_ASSERT_M("Failed to read MSR values from 0x10", n > 0);

	n = pread(fd, values, max_cpus * sizeof(uint64_t),
				  MSR_CORE_PERF_FIXED_CTR0);
	UT_ASSERT_M("Failed to read MSR values from 0x309", n > 0);

	n = pwrite(fd, &tmp, sizeof(uint64_t), MSR_CORE_PERF_FIXED_CTR0);
	UT_ASSERT_M("Failed to write MSR values to 0x309",
				n == sizeof(uint64_t));

	free(values);
	close(fd);

	return true;
}

static bool test_mem(void)
{
	int fd = open("#arch/realmem", O_RDONLY);
	ssize_t n;
	char buf[256];

	UT_ASSERT_M("Failed to open memory device file (#arch/mem)", fd >= 0);

	n = pread(fd, buf, sizeof(buf), 0);
	UT_ASSERT_M("Failed to read real mode memory from offset 0",
				n == sizeof(buf));

	close(fd);

	return true;
}

/* <--- End definition of test cases ---> */

static struct utest utests[] = {
	UTEST_REG(msr),
	UTEST_REG(mem)
};
static const int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[])
{
	RUN_TEST_SUITE(utests, num_utests, argv + 1, argc - 1);
}
