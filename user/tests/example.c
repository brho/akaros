#include <testing.h>

TEST_SUITE("EXAMPLE");



/* <--- Begin definition of test cases ---> */

bool test_one(void) {
	UT_ASSERT_M("One plus one should equal 2", 1+1 == 2);
}

bool test_two(void) {
	UT_ASSERT_M("One minus one should equal 0", 1-1 == 0);
}

bool test_three(void) {
	UT_ASSERT_M("One should equal 0", 1 == 0);
}

/* <--- End definition of test cases ---> */



struct usertest usertests[] = {
	U_TEST_REG(one),
	U_TEST_REG(two),
	U_TEST_REG(three) // This one will fail.
};

int main(int argc, char *argv[]) {
	// Run test suite passing it all the args as whitelist of what tests to run.
	RUN_TEST_SUITE(&argv[1], argc-1);
}

