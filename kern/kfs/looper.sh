#!/bin/ash

if [ $# -lt 1 ] ; then
	echo "Need an app!"
	exit 
fi
RET=0 ;
while [ $RET -eq 0 ]; do
	$@
	RET=$?
done
