#include <net/ip.h>
#include <ktest.h>
#include <linker_func.h>

KTEST_SUITE("NET")

static uint16_t simplesum(const uint8_t *buf, int len)
{
	uint64_t hi = 0, lo = 0, sum;
	int i;

	for (i = 0; i < len; i++) {
		if (i % 2 == 0)
			hi += buf[i];
		else
			lo += buf[i];
	}
	sum = (hi << 8) + lo;
	while (sum >> 16)
		sum = (sum >> 16) + (sum & 0xffff);
	return sum & 0xffff;
}

bool test_ptclbsum(void)
{
	uint16_t csum, expected;
	uint8_t buf[100];
	int i, j, len;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = i & 0xff;
	for (i = 0; i < sizeof(buf); i++) {
		for (j = i; j < sizeof(buf); j++) {
			len = j - i + 1;
			csum = ptclbsum(buf + i, len);
			expected = simplesum(buf + i, len);
			if (csum != expected) {
				printk("i %d j %d len %d csum %04x expected %04x\n",
					   i, j, len, csum, expected);
				return false;
			}
		}
	}
	return true;
}

#define CSUM_BENCH_BUFSIZE 4000

bool test_simplesum_bench(void)
{
	uint8_t buf[CSUM_BENCH_BUFSIZE];
	uint16_t csum = 0;
	int i, j, len;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = i & 0xff;
	for (i = 0; i < sizeof(buf); i++) {
		for (j = i; j < sizeof(buf); j++) {
			len = j - i + 1;
			csum += simplesum(buf + i, len);
		}
	}
	return true;
}

bool test_ptclbsum_bench(void)
{
	uint8_t buf[CSUM_BENCH_BUFSIZE];
	uint16_t csum = 0;
	int i, j, len;

	for (i = 0; i < sizeof(buf); i++)
		buf[i] = i & 0xff;
	for (i = 0; i < sizeof(buf); i++) {
		for (j = i; j < sizeof(buf); j++) {
			len = j - i + 1;
			csum += ptclbsum(buf + i, len);
		}
	}
	return true;
}

static struct ktest ktests[] = {
	KTEST_REG(ptclbsum,		CONFIG_TEST_ptclbsum),
	KTEST_REG(simplesum_bench,	CONFIG_TEST_simplesum_bench),
	KTEST_REG(ptclbsum_bench,	CONFIG_TEST_ptclbsum_bench),
};

static int num_ktests = sizeof(ktests) / sizeof(struct ktest);

static void __init register_net_ktests(void)
{
	REGISTER_KTESTS(ktests, num_ktests);
}
init_func_1(register_net_ktests);
