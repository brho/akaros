#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
# Copyright 2016 Google Inc
#
# I run this after doing a git am and after I am OK with merging.
# 
# Prints a blob of text describing the changes from (FROM..TO].
# By default, it applies to master..origin/master, but you can override that.

FROM="origin/master"
TO="master"

usage()
{
	echo "$0 [<from>..<to>]"
	exit -1
}

if [ $# -gt 1 ]
then
	usage
fi

if [ $# -eq 1 ]
then
	FROM=`echo $1 | cut -f 1 -d '.'`
	TO=`echo $1 | cut -f 3 -d '.'`
fi

FROM_SHA=`git log --format=format:%h -1 $FROM`
TO_SHA=`git log --format=format:%h -1 $TO`

echo "Merged to master at $FROM_SHA..$TO_SHA (from, to]"
echo ""
echo "You can see the entire diff with 'git diff' or at" 
echo "https://github.com/brho/akaros/compare/$FROM_SHA...$TO_SHA"
echo ""
