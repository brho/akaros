#!/bin/ash
#
# runall
#
# Runs all userspace tests
TEST_DIR=/bin/tests/user

# Run all test suites in test directory.
for file in $TEST_DIR/*[!.sh]
do
	$file
done
