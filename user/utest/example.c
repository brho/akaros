#include <utest/utest.h>

TEST_SUITE("EXAMPLE");

/* <--- Begin definition of test cases ---> */

bool test_one(void) {
	UT_ASSERT_M("One plus one should equal 2", 1+1 == 2);
	return TRUE;
}

bool test_two(void) {
	UT_ASSERT_M("One minus one should equal 0", 1-1 == 0);
	return TRUE;
}

bool test_three(void) {
	UT_ASSERT_M("1 should equal 1", 1 == 1);
	return TRUE;
}

/* <--- End definition of test cases ---> */

struct utest utests[] = {
	UTEST_REG(one),
	UTEST_REG(two),
	UTEST_REG(three) 
};
int num_utests = sizeof(utests) / sizeof(struct utest);

int main(int argc, char *argv[]) {
	char **whitelist = &argv[1];
	int whitelist_len = argc - 1;

	RUN_TEST_SUITE(utests, num_utests, whitelist, whitelist_len);
}

