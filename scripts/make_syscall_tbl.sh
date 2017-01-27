#!/bin/bash
# Barret Rhoden <brho@cs.berkeley.edu>
# Builds a syscall table, an array of strings of syscall names.
# Used by parlib.

if [ $# -ne 2 ]
then
	echo "Need syscall.h and output file args!"
	exit -1
fi

SYSLIST=$1
SYSCALL_TBL=$2

echo "// This was automatically generated with make_syscall_tbl.sh, do not edit" > $SYSCALL_TBL
echo ""                                    >> $SYSCALL_TBL
echo "const char *const __syscall_tbl[] =" >> $SYSCALL_TBL
echo "{"                                   >> $SYSCALL_TBL

# Read lines formatted as:
#
#  #define	SYS_foo		1
#  #define	SYS_bar		3
#
# And output them as:
#
#  [ 1 ] = "foo",
#  [ 3 ] = "bar",
#
# Here's the guts of it.  Get the #define SYS_'s, compress the whitespace, cut
# to drop the #define, drop the SYS_, then awk it.

cat $SYSLIST | grep "^#define SYS_" \
             | sed 's/\s\+/\t/g' \
             | cut -f 2- \
             | sed 's/SYS_//' \
             | awk '{printf "\t[ %s ] = \"%s\",\n", $2, $1}' \
             >> $SYSCALL_TBL

echo "};"                                 >> $SYSCALL_TBL
echo ""                                   >> $SYSCALL_TBL
echo "const int __syscall_tbl_sz = sizeof (__syscall_tbl) / sizeof (__syscall_tbl[0]);" >> $SYSCALL_TBL
