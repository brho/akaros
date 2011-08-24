#!/bin/ash

if [ $# -lt 1 ] ; then
	echo "Need an app!"
	exit 
fi
RET=0 ;
INC=0 ;
while [ $RET -eq 0 ]; do
	echo $INC
	$@
	RET=$?
	let INC=$INC+1
done
