#!/usr/bin/python
"""This script keeps running until one of the two following conditions occur:
1) A line in the file specified by argv[1] contains argv[2] => This will exit
   with a result code of 0 and print SUCCESSS.
2) argv[3] seconds or more occur since invoking the script => This will exit
   with a result code of 0 (because Jenkins would autokill the build if we
   returned an error code) and print TIMEOUT.

Please note:
    The timeout specified by argv[3] may be checked with a precision of up to 5
    seconds, so it may be possible that the script runs for argv[3] + 5 secs (or
    even a little bit more).
"""
import sys
import time
import re


OUTPUT_FILE = sys.argv[1]
REGEX_END_LINE = r'^.*' + sys.argv[2] + '.*$'
MAX_TIME_TO_RUN = int(sys.argv[3])

def is_end_line(line) :
	"""Returns true if a given file contains the 'End line' string.
	"""

	if re.match(REGEX_END_LINE, line) :
		return True
	else :
		return False

def main() :
	"""Opens the OUTPUT_FILE and continuously reads lines from it until either
	there are no more lines or the end line is reached. In the former case, it
	waits an exponentially increasing time interval for more lines to be printed
	onto the file.

	If MAX_TIME_TO_RUN seconds are elapsed, then the script also terminates, 
	with an error condition.
	"""
	timeout_time = time.time() + MAX_TIME_TO_RUN

	output_file = open(OUTPUT_FILE, 'r')

	# Min and max waiting times (sec.) between reading two lines of the file.
	MIN_READ_DELAY = 0.1
	MAX_READ_DELAY = 5
	READ_DELAY_INCREM_FACTOR = 1.5 # Times what the read delay is increased.

	secs_before_read = 2
	end_not_reached = True

	while end_not_reached :
		line = output_file.readline()
		
		if (len(line) == 0) :
			time.sleep(secs_before_read)
			# Sleep with exponential backoff.
			secs_before_read = MAX_READ_DELAY \
			                   if (secs_before_read > MAX_READ_DELAY) \
			                   else secs_before_read * READ_DELAY_INCREM_FACTOR
		else :
			secs_before_read = MIN_READ_DELAY
			end_not_reached = not is_end_line(line)


		if (time.time() >= timeout_time) :
			print "TIMEOUT"
			exit(0)

	print "SUCCESS"
	exit(0)

main()
