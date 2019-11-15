#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
#
# Resolves functions from Akaros backtraces, both user and kernel.
# Pipe a backtrace (echo "huge-copy-paste" | ./thisfile.sh) to it.
#
# There are a few environment variables you may want to override, which control
# the location of the binaries and libraries.  If you use AKAROS_ROOT, KFS, and
# x86, just stick with the defaults.

: ${SOLIBS_PREFIX:=$AKAROS_ROOT/kern/kfs/lib/}
: ${SO_REGEX:=.*so$}
: ${BIN_PREFIX:=$AKAROS_ROOT/kern/kfs/bin/}
: ${KERNEL_BINARY:=$AKAROS_ROOT/obj/kern/akaros-kernel-64b}

# takes the path to the binary and offset (offset in hex), prints name of the
# function where the offset is in the binary.  basically a wrapper for
# addr2line.
print_user_func() {
	addr2line -e $1 -fC $2 | xargs
}

kernel_line() {
	line=$1

	addr=`echo $line | cut -c 7-25`
	frame_num=`echo $line | cut -f 1 -d ' '`

	IFS=' '
	addr2line -afip -e $KERNEL_BINARY $addr | while read -ra RET
	do
		if [ "${RET[0]}" == "(inlined" ]; then
			# (inlined by) with spaces to line up with a 64 bit addr
			addr="   ${RET[0]} ${RET[1]}   "
			func="${RET[2]}"
			srcl="${RET[4]}"
		else
			addr="${RET[0]}"
			addr="${addr%?}"	# drop the trailing ':'
			func="${RET[1]}"
			srcl="${RET[3]}"
		fi
		# sed cleans out build paths.  All kernel files start with
		# '/kern', unlike arbitrary user binaries.
		srcl=`echo $srcl | sed 's/^.*\/kern\//kern\//'`
		echo "$frame_num [<$addr>] in ${func}() at $srcl"
	done
}

user_line() {
	line=$1

	binary=`echo $line | cut -f 6 -d ' '`
	lib_off=`echo $line | cut -f 9 -d ' '`
	app_off=`echo $line | cut -f 3 -d ' '`

	echo -n $line " "
	if [[ $binary =~ $SO_REGEX ]]; then
		# could also do addr=$(print_user_func $lib $off)
		print_user_func $SOLIBS_PREFIX/$binary $lib_off
	else
		print_user_func $BIN_PREFIX/$binary $app_off
	fi
}

while read line; do
	fifth_char=`echo $line | cut -c 5`

	if [[ $fifth_char == "[" ]]; then
		kernel_line "$line"
	elif [[ $fifth_char == "A" ]]; then
		user_line "$line"
	fi
done
