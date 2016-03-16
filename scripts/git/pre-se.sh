#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
# Copyright 2016 Google Inc
#
# Spits out a blob of text describing a patchset.  I'll use this with
# send-email's cover-letter for large patchsets.

usage()
{
	echo "$0 <from>..<to>"
	exit -1
}

if [ $# -ne 1 ]
then
	usage
fi

FROM=`echo $1 | cut -f 1 -d '.'`
TO=`echo $1 | cut -f 3 -d '.'`

FROM_SHA=`git log --format=format:%h -1 $FROM`
TO_SHA=`git log --format=format:%h -1 $TO`

echo ""
echo "------------"
echo "You can also find this patch set at:"
echo ""
echo "git@github.com:brho/akaros.git"
echo "From: $FROM_SHA"
echo "To: $TO_SHA $TO"
echo ""
echo "And view them at: "
echo ""
echo "https://github.com/brho/akaros/compare/$FROM_SHA...$TO_SHA"
echo ""
echo "------------"
