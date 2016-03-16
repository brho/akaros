#!/bin/bash
# Barret Rhoden (brho@cs.berkeley.edu)
# Copyright 2016 Google Inc
#
# Converts an mbox to a patch set.
#
# Mostly, it is just git mailsplit, but it also processes the patches to remove
# a lot of the email headers and to rewrite any From: headers that were mangled
# by the mail server.

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
	# Remove all the header crap above From:
	FROMLINE=`grep -n "^From:" $i | cut -f 1 -d':' | head -1`
	FROMLINESUB=$(( ${FROMLINE} - 1 ))
	sed -i -e "1,${FROMLINESUB}d" $i

	# Gmail sucks and rewrites some Froms.  We'll catch it and replace From
	# with X-Original-From
	ORIGFROM=`grep "^X-Original-From" $i`
	if [ $? -eq 0 ]
	then
		ORIGFROM=`echo $ORIGFROM | sed 's/^X-Original-From:/From:/g'`
		sed -i "/^From:.*/c$ORIGFROM" $i
	fi

	# Remove header crap before the first blank
	# X- stuff
	SPACELINE=`grep -n "^$" $i | cut -f 1 -d':' | head -1`
	sed -i -e "1,${SPACELINE}{ /^X-.*/d }" $i

	# List stuff
	SPACELINE=`grep -n "^$" $i | cut -f 1 -d':' | head -1`
	sed -i -e "1,${SPACELINE}{ /^List-.*/d }" $i

	# space-indented stuff for the X and List headers
	SPACELINE=`grep -n "^$" $i | cut -f 1 -d':' | head -1`
	sed -i -e "1,${SPACELINE}{ /^ .*/d }" $i

	# grep subject, remove [akaros], remove " [PATCH xxx] ", (matching anything
	# other than a ], so we get only the first ]), then changes
	# non-letters/nums to -
	SUBJECT=`grep "^Subject:" $i | cut -f 2- -d':' | sed 's/\[akaros\]//' |
	         sed 's/^ *\[[^]]*\] //' | sed 's/[^[:alnum:]]/-/g'`

	mv $i $i-$SUBJECT.patch
done
cd - > /dev/null
