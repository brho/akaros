#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
#
# Resolves functions from an Akaros user backtrace.
# Pipe a backtrace (echo "huge-copy-paste" | ./thisfile.sh) to it.
# Be sure to set your environment paths for the SOLIBS and BIN

: ${SOLIBS_PREFIX:=~/akaros/ros-kernel/kern/kfs/lib/}
: ${SO_REGEX:=.*so$}
: ${BIN_PREFIX:=~/akaros/ros-kernel/kern/kfs/bin/}

# takes the path to the binary and offset (offset in hex), prints name of the
# function where the offset is in the binary.  basically a wrapper for
# addr2line.
function print_func()
{
	addr2line -e $1 -fC $2 | xargs
}

while read line
do
	binary=`echo $line | cut -f 6 -d ' '`
	lib_off=`echo $line | cut -f 9 -d ' '`
	app_off=`echo $line | cut -f 3 -d ' '`
	if [[ $binary == "" ]]
	then
		break
	fi
	echo -n $line " "
	if [[ $binary =~ $SO_REGEX ]]
	then
		# could also do addr=$(print_func $lib $off)
		print_func $SOLIBS_PREFIX/$binary $lib_off
	else
		print_func $BIN_PREFIX/$binary $app_off
	fi
done
