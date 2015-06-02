#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
#
# Resolves functions from an Akaros user backtrace.
# Pipe a backtrace (echo "huge-copy-paste" | ./thisfile.sh) to it.
# Be sure to set your environment paths for the SOLIBS and BIN
#
# You can also source this (hit ctrl-D, to abort the while read line loop) and
# then call print_glb_func directly.

S0LIBS_PREFIX=~/akaros/ros-kernel/kern/kfs/lib/
SO_REGEX=.*so$ 
BIN_PREFIX=~/akaros/ros-kernel/kern/kfs/bin/

# takes the path to the binary and offset (offset in hex), prints the 'greatest
# lower bound' of the functions, which is probably the function where the
# offset is within the binary.  be careful with hex (target should be in hex,
# with a leading "0x").
function print_glb_func()
{
	# remove the 2>/dev/null for debugging.  it helps at runtime in case it
	# can't find a file (e.g. solibs in multiple places)
	readelf -s $1 2>/dev/null | awk -v target=$2 '
	BEGIN { glb = 0; name = "LOOKUP_FAILED"}
	{
		addr = strtonum("0x"$2)
		tgt = strtonum(target)
		if (($4 == "FUNC") && (addr > glb) && (addr <= tgt)) {
			glb = addr; name = $8
		}
	}
	END { printf("%s+0x%x\n", name, tgt - glb) }'
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
		# could also do addr=$(print_glb_func $lib $off)
		print_glb_func $S0LIBS_PREFIX/$binary $lib_off
	else
		print_glb_func $BIN_PREFIX/$binary $app_off
	fi
done
