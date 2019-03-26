#!/bin/bash

# will run lock_test with whatever arguments are passed in.  default arguments
# are -wMAX_VCORES and -l10000.

# TODO: take a param up to MAXVC for how many cores we preempt per loop
# TODO: make a program that does the preemption, instead of the script.  it
# takes around 2ms to spawn, run, and wait on a program, which messes with the
# timing.  When we do this, pull in the vcore shuffling code from prov.c and
# have that be an option.
# TODO: have better options for the two preempt styles (periodic, vs total)
#
# Run this from looper.sh for multiple runs.

if [[ $# -lt 2 ]]
then
	echo Usage: $0 PREEMPT_DELAY_USEC ARGS_FOR_LOCK_TEST
	exit
fi

PREEMPT_DELAY=$1

shift

# for suppressing output later (no /dev/null, and also append doesn't work)
echo "" >> tmpfile

# retval of max_vcores is the value
max_vcores
MAXVC=$?
# pth_test exists to hog the machine
pthread_test 100 999999999 $MAXVC >> tmpfile 2>&1 &
PTHPID=$!
echo Launch pth_test, pid was $PTHPID

# if arguments, like -w are repeated, the second one is used
lock_test -w $MAXVC -l 10000 $@ &
LOCKPID=$!
echo Launch lock_test, pid was $LOCKPID

# while lock_test is still alive, we preempt every so often, then give it back.
# using the retval of prov -p$LOCKPID to tell us if lock_test is around
RET=0
INC=0

# This is the preempt and never return, hoping to catch deadlocks
prov -tc -p$LOCKPID -m >> tmpfile 2>&1

usleep $PREEMPT_DELAY

prov -tc -p$PTHPID -v1 >> tmpfile 2>&1
prov -tc -p$PTHPID -v2 >> tmpfile 2>&1

# giving it vc3, which it already has.  this is just a test on process existence
while [ $RET -eq 0 ]; do
	prov -tc -p$LOCKPID -v3 >> tmpfile 2>&1
	RET=$?
	usleep $PREEMPT_DELAY
done

## This is the preempt, return, preempt return cycle
#while [ $RET -eq 0 ]; do
#	prov -tc -p$LOCKPID -m >> tmpfile 2>&1
#	RET=$?
#
#	usleep $PREEMPT_DELAY
#
#	prov -tc -p$PTHPID -v1 >> tmpfile 2>&1
#	prov -tc -p$PTHPID -v2 >> tmpfile 2>&1
#
#	# the extra preempts here are to make us wait longer, to see gaps where
#	# we "locked up" more clearly.
#	usleep $PREEMPT_DELAY
#	usleep $PREEMPT_DELAY
#	usleep $PREEMPT_DELAY
#	usleep $PREEMPT_DELAY
#	usleep $PREEMPT_DELAY
#
#	let INC=$INC+1
#done

## This is the 'run for a bit, preempt a lot just once, then return all style
#prov -tc -p$LOCKPID -m >> tmpfile 2>&1
#usleep $PREEMPT_DELAY
#prov -tc -p$PTHPID -v1 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v2 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v3 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v4 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v5 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v6 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v7 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v8 >> tmpfile 2>&1
#prov -tc -p$PTHPID -v9 >> tmpfile 2>&1
#usleep $PREEMPT_DELAY
#prov -tc -p$LOCKPID -m >> tmpfile 2>&1
#
#while [ $RET -eq 0 ]; do
#	prov -tc -p$LOCKPID -m >> tmpfile 2>&1
#	RET=$?
#	usleep $PREEMPT_DELAY
#done

echo All done, killing pth_test.  Did $INC prov-preempt loops.
kill -9 $PTHPID
