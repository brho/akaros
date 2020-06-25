#!/bin/bash

AKA_DIR=~/akaros/ros-kernel/
RESULTS_TOP_DIR=~/data/lock_data

set -e

[ $# -lt 2 ] && echo Need HOST LOOPS && exit -1

HOST=$1
LOOPS=$2
TESTS="
	mcs
	mcscas
	spin
	mcs-kernel
	queue-kernel
	spin-kernel
	"

echo Running lock_test on $HOST for $LOOPS loops, ctl-C to abort
sleep 5

RESULTS_DIR=$RESULTS_TOP_DIR/$HOST-$LOOPS-$(date +%F-%H%M%S)/
mkdir $RESULTS_DIR

HDIR=/some/data/brho/
ssh root@$HOST mkdir -p $HDIR
scp $AKA_DIR/tests/linux/modules/mcs.ko $AKA_DIR/tests/linux/obj/lock_test root@$HOST:$HDIR
set +e
ssh root@$HOST rmmod mcs
set -e
ssh root@$HOST insmod $HDIR/mcs.ko

for i in $TESTS; do
	echo Trying lock_test -l $LOOPS -t $i
	ssh root@$HOST $HDIR/lock_test -l $LOOPS -t $i -o $HDIR/$HOST-$i-$LOOPS.dat
	ssh root@HOST gzip $HDIR/$HOST-$i-$LOOPS.dat
	scp root@$HOST:$HDIR/$HOST-$i-$LOOPS.dat.gz $RESULTS_DIR
done

echo Done... $RESULTS_DIR
