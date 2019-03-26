#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
# Copyright 2016 Google Inc
#
# Converts an mbox to a patch set.  It's like git am, but it generates patches
# instead of applying them to your tree.
#
# Mostly, it is just git mailsplit and mailinfo, but it also handles rewritten
# 'From' fields that Gmail or Google Groups seem to generate.

PATCHDIR="${PATCHDIR:-../patches}"

usage()
{
	echo "$0 <mbox>"
	exit -1
}

if [ $# -ne 1 ]
then
	usage
fi

MBOX=$1

ls $PATCHDIR/*.patch 2>/dev/null

if [ $? -eq 0 ]
then
	echo "$PATCHDIR has patches, remove and try again"
	exit -1
fi

git mailsplit -o$PATCHDIR $MBOX > /dev/null
cd $PATCHDIR
for i in `ls`
do
	# Gmail rewrites some Froms.  We'll catch it and replace From with
	# X-Original-From
	ORIGFROM=`grep "^X-Original-From" $i`
	if [ $? -eq 0 ]
	then
		ORIGFROM=`echo $ORIGFROM | sed 's/^X-Original-From:/From:/g'`
		sed -i "/^From:.*/c$ORIGFROM" $i
	fi

	git mailinfo MI_msg MI_patch < $i > MI_header

	# We need a From: field, synthesized from Author and Email
	AUTHOR=`grep "^Author:" MI_header | cut -f 2- -d' '`
	EMAIL=`grep "^Email:" MI_header | cut -f 2- -d' '`

	# Determine the subject for naming the patch, replace spaces and weird
	# chars
	SUBJECT=`grep "^Subject:" MI_header | cut -f 2- -d' ' |
	         sed 's/[^[:alnum:]]/-/g'`

	echo "From: $AUTHOR <$EMAIL>" > $i-$SUBJECT.patch
	cat MI_header MI_msg MI_patch >> $i-$SUBJECT.patch

	rm MI_header MI_msg MI_patch $i
done
cd - > /dev/null
