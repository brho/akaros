#!/bin/bash

# Read from stdin lines formatted as:
#
#  #define	EPERM		1	/* Operation not permitted */
#  #define	ENOENT		2	/* No such file or directory */
#  #define	ESRCH		3	/* No such process */
#  #define	EINTR		4	/* Interrupted system call */
#  #define	EIO		5	/* I/O error */
#
# And output them as:
#
#  [ 1 ] = "Operation not permitted",
#  [ 2 ] = "No such file or directory",
#  [ 3 ] = "No such process",
#  [ 4 ] = "Interrupted system call",
#  [ 5 ] = "I/O error",
#

grep "^#define\sE" | sed 's/\t\+/\t/g' | cut -f 3- | grep -v "^E" | awk '{printf "\t[ %s ] = \"", $1; for (i=3; i<NF-1; i++) printf "%s ", $i; printf "%s", $(NF-1); printf "\",\n"}'
