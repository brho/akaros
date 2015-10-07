#!/bin/bash

ERRLIST="tools/compilers/gcc-glibc/glibc-2.19-akaros/sysdeps/akaros/errlist.c"
ERRNO_FILE="kern/include/ros/errno.h"

echo "// This was automatically generated with make_errlist.sh, do not edit" > $ERRLIST
echo ""                                   >> $ERRLIST
echo "#include <stddef.h>"                >> $ERRLIST
echo ""                                   >> $ERRLIST
echo "const char *const _sys_errlist[] =" >> $ERRLIST
echo "{"                                  >> $ERRLIST

# here's the guts of it.  Get the #define E's, compress the extra tabs, cut to
# get from the numbers to the end, remove the aliases (number is EWHATEVER),
# then awk it.  The awk script does some fancy formatting.  NF is the number of
# items on the line.  The last item on the line is */, which we never print.
# The second to last is printed without a trailing space.

cat $ERRNO_FILE | ./scripts/parse_errno.sh >> $ERRLIST

echo "};"                                 >> $ERRLIST
echo ""                                   >> $ERRLIST
echo "const int _sys_nerr = sizeof (_sys_errlist) / sizeof (_sys_errlist[0]);" >> $ERRLIST
echo ""                                   >> $ERRLIST
echo "strong_alias(_sys_errlist,_sys_errlist_internal);"                       >> $ERRLIST
echo "strong_alias(_sys_nerr,_sys_nerr_internal);"                             >> $ERRLIST
