#!/bin/ash
#
# runall
#
# Runs all userspace tests
TEST_DIR=/bin/tests/utest

# Run all test suites in test directory.
echo "<-- BEGIN_USERSPACE_TESTS -->"
for file in $TEST_DIR/*[!.sh]
do
	$file
done
echo "<-- END_USERSPACE_TESTS -->"
