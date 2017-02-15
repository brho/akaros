#!/bin/bash
# Barret Rhoden <brho@cs.berkeley.edu>
# Builds a giant string of Kconfig options

if [ $# -ne 2 ]
then
	echo "Need your .config and output file args!"
	exit -1
fi

CONFIG_FILE=$1
KCONFIG_C=$2

echo "// This was automatically generated with $0, do not edit" > $KCONFIG_C

# Here's the guts of it.  Ignore the comments, ignore the empty lines, replace
# all " with \" (since we're making a string), and print a record at a time.
# It'll look like "CONFIG_foo\nCONFIG_bar\n".

echo -n "const char *__kconfig_str = \"" >> $KCONFIG_C

grep -v '^#' $CONFIG_FILE | grep -v '^$' \
                          | sed 's/"/\\"/g' \
                          | awk '{printf "%s\\n", $1}' \
                          >> $KCONFIG_C
echo "\";" >> $KCONFIG_C
