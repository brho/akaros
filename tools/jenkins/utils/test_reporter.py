#!/usr/bin/python
"""Parses AKAROS output to detect tests and report on them.
Arguments:
	[0]: Path to file containing AKAROS output.
	[1]: Path to directory where to save test reports.
	[2]: IDs of tests suites to look for.
"""
import markup
import re
import sys



class TestSuite() :
	"""Represents a test suite (collection of test cases) and has the ability of
	printing itself plus the test cases into XML markup.
	"""
	def __init__(self, name, class_name) :
		"""The tests will be reported as belonging to 'name.class_name', so you 
		can represent two levels of hierarchy this way.
		"""
		self.name = name
		self.class_name = class_name
		self.test_cases = []
		self.errors_num = 0
		self.failures_num = 0
		self.skipped_num = 0

	def add_test_case(self, name, status, el_time=None, failure_msg=None) :
		"""Adds a test case to the suite.
		"""
		test_case = TestCase(self, name, status, el_time, failure_msg)
		self.test_cases.append(test_case)

		if status == 'DISABLED' :
			self.skipped_num += 1
		elif status == 'FAILED' :
			self.failures_num += 1

	def generate_markup(self) :
		"""Generates and returns a string containing the representation of the
		suite and the testcases in XML XUnit format.

		Returns:
			String containing the representation.
		"""
		report = markup.page( mode='xml' )
		report.init( encoding='UTF-8' )

		report.testsuite.open(name='akaros_tests', tests=len(self.test_cases), \
		                      errors=self.errors_num, \
		                      failures=self.failures_num, \
		                      skip=self.skipped_num)
		for test_case in self.test_cases:
			test_case.generate_markup(report)
		report.testsuite.close()

		return report

class TestCase() :
	"""Represents a test case, and has ability to print it into a markup page.
	"""
	def __init__(self, suite, name, status, el_time=None, failure_msg=None) :
		self.suite = suite
		self.name = name
		self.status = status
		if self.status in ['PASSED', 'FAILED'] :
			self.el_time = el_time
		if self.status == 'FAILED' :
			self.failure_msg = failure_msg

	def generate_markup(self, report) :
		"""Generates XML markup representing the test case, and in XUnit format
		in the given markup.page file.
		"""
		full_name = self.suite.name + '.' + self.suite.class_name

		if self.status in ['PASSED', 'FAILED'] :
			report.testcase.open(classname=full_name, name=self.name, \
			                     time=self.el_time)
		else :
			report.testcase.open(classname=full_name, name=self.name)

		if self.status == 'DISABLED' :
			report.skipped.open(type='DISABLED', message='Disabled')
			report.skipped.close()
		elif self.status == 'FAILED' :
			report.failure.open(type='FAILED', message=self.failure_msg)
			report.failure.close()

		report.testcase.close()



class TestParser() :
	"""This class is a helper for parsing the output from test suite groups
	ran inside AKAROS.

	Tests must be printed on to a file (specified by test_output_path) with the
	following format:
	<-- BEGIN_{test_suite_name}_{test_class_name}_TESTS -->
		(PASSED|FAILED|DISABLED) [{test_case_name}]({test_et}s)? {failure_msg}?
		(PASSED|FAILED|DISABLED) [{test_case_name}]({test_et}s)? {failure_msg}?
		...
	<-- END_{test_suite_name}_{test_class_name}_TESTS -->

	For example:
	<-- BEGIN_KERNEL_PB_TESTS -->
		PASSED   [test_easy_to_pass](1.000s)
		FAILED   [test_will_fail](0.01s)   This test should do X and Y.
		DISABLED [test_useless]
		...
	<-- END_KERNEL_PB_TESTS -->
	"""

	def __init__(self, test_output_path, test_suite_name, test_class_name) :
		self.test_output = open(test_output_path, 'r')
		self.regex_test_start = \
		    re.compile('^\s*<--\s*BEGIN_%s_%s_TESTS\s*-->\s*$' \
		               % (test_suite_name, test_class_name))
		self.regex_test_end = \
		    re.compile('^\s*<--\s*END_%s_%s_TESTS\s*-->\s*$' \
		               % (test_suite_name, test_class_name))
		self.test_suite_name = test_suite_name
		self.test_class_name = test_class_name

		# Prepare for reading.
		self.__advance_to_beginning_of_tests()

	def __advance_to_beginning_of_tests(self) :
		beginning_reached = False
		while not beginning_reached :
			line = self.test_output.readline()
			if (re.match(self.regex_test_start, line)) :
				beginning_reached = True
			elif (len(line) == 0) :
				exc_msg = 'Could not find tests for {0}_{1}.'
				exc_msg = exc_msg.format(self.test_suite_name, \
				                         self.test_class_name)
				raise Exception(exc_msg)

	def __extract_test_result(self, line) :
		regex = r'^\s*([A-Z]+)\s*.*$'
		matchRes = re.match(regex, line)
		return matchRes.group(1)

	def __extract_test_name(self, line) :
		regex = r'^\s*(?:[A-Z]+)\s*\[([a-zA-Z_-]+)\].*$'
		matchRes = re.match(regex, line)
		return matchRes.group(1)

	def __extract_test_elapsed_time(self, line) :
		regex= r'^\s*(?:PASSED|FAILED)\s*\[(?:[a-zA-Z_-]+)\]\(([0-9\.]+)s\).*$'
		matchRes = re.match(regex, line)
		return matchRes.group(1)

	def __extract_test_fail_msg(self, line) :
		regex = r'^\s*FAILED\s*\[(?:[a-zA-Z_-]+)\](?:\(.*\))?\s+(.*)$'
		matchRes = re.match(regex, line)
		return matchRes.group(1)

	def __next_test(self) :
		"""Parses the next test from the test output file.
		Returns:
			First, True if there was a next test and we had not reached the end.
			Second, a String with the name of the test.
			Third, result of the test (PASSED, FAILED, DISABLED).
			Fourth, time elapsed in seconds, with 3 decimals.
			Fifth, message of a failed test.
		"""
		# Look for test.
		line = ''
		while len(line) < 8 :
			line = self.test_output.readline()
			if (len(line) == 0) : # EOF
				return False, '', '', ''

		if (re.match(self.regex_test_end, line)) :
			return False, '', '', 0, ''
		else :
			name = self.__extract_test_name(line)
			res = self.__extract_test_result(line)
			time = self.__extract_test_elapsed_time(line) \
			       if res in ['FAILED', 'PASSED'] else None
			msg = self.__extract_test_fail_msg(line) if res == 'FAILED' \
			      else None
			
			return True, name, res, time, msg

	def __cleanup(self) :
		self.test_output.close()

	def parse_test_suite(self) :
		test_suite = TestSuite(self.test_suite_name, self.test_class_name)

		end_not_reached = True
		while end_not_reached :
			end_not_reached, test_name, test_res, test_et, fail_msg \
			    = self.__next_test()
			if end_not_reached :
				test_suite.add_test_case(test_name, test_res, test_et, fail_msg)
		
		self.__cleanup()
		
		return test_suite



KERNEL_PB_TESTS_KEY = 'KERNEL_POSTBOOT'
KERNEL_PB_TESTS_SUITE_NAME = 'KERNEL'
KERNEL_PB_TESTS_CLASS_NAME = 'POSTBOOT'

def save_report(dir, filename, report) :
	filepath = dir + '/' + filename + '_TESTS.xml'
	report_file = open(filepath, 'w+')
	report_file.write(report)
	report_file.flush()
	report_file.close()


def main() :
	akaros_output_file_path = sys.argv[1]
	test_output_dir = sys.argv[2]
	tests_to_run = sys.argv[3].strip().split(' ')

	# Kernel Postboot Tests
	if KERNEL_PB_TESTS_KEY in tests_to_run :
		test_suite = TestParser(akaros_output_file_path, \
		                        KERNEL_PB_TESTS_SUITE_NAME, \
		                        KERNEL_PB_TESTS_CLASS_NAME).parse_test_suite()
		test_report_str = test_suite.generate_markup().__str__()
		kernel_pb_tests_report = save_report(test_output_dir, \
		                                     KERNEL_PB_TESTS_KEY, \
		                                     test_report_str)

main()
