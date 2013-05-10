#!/bin/ash

# will run lock_test with whatever arguments are passed in.  default arguments
# are -wMAX_VCORES and -l10000.

# TODO: take a param up to MAXVC for how many cores we preempt per loop
# TODO: make a program that does the preemption, instead of the script.  it
# takes around 2ms to spawn, run, and wait on a program, which messes with the
# timing

if [ $# -lt 2 ]
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
pthread_test 100 999999999999999 $MAXVC >> tmpfile 2>&1 &
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
while [ $RET -eq 0 ]; do
	prov -tc -p$LOCKPID -m >> tmpfile 2>&1
	RET=$?

	usleep $PREEMPT_DELAY
	
	prov -tc -p$PTHPID -v1 >> tmpfile 2>&1
	#prov -tc -p$PTHPID -v2 >> tmpfile 2>&1

	usleep $PREEMPT_DELAY

	let INC=$INC+1
done

echo All done, killing pth_test.  Did $INC prov-preempt loops.
kill -9 $PTHPID
