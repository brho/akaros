#!/bin/bash
# Barret Rhoden 2012-03-07

# Builds/updates a gtags database for all directories/paths listed in
# GTAGS_INC_FILE (.gtagsinclude).  If you are in a gtags-managed directory
# (subdir), it will update from the rootdir.  If not, it will attempt to build
# a new gtags database, if you have the whitelist file.
#
# This will also do the incremental update, so run this when you have new files
# in the system.  Don't run global -u, since it takes forever, and is probably
# doing something I don't like.

GTAGS_INC_FILE=.gtagsinclude
# If we're already in a gtags-managed directory, cd into the root
ROOTDIR=`global -p 2> /dev/null`
RETVAL=$?
if [ $RETVAL == 0 ]
then
	cd $ROOTDIR
# otherwise, we assume we're in the (new) root
fi
# Find our include file (the whitelist)
if [ ! -f "$GTAGS_INC_FILE" ]
then
	echo "Could not find gtags include file, aborting"
	exit -1
fi
# get our directory list, send the list of all files in those paths to gtags
DIRS=`cat $GTAGS_INC_FILE`
find -L $DIRS -type f | gtags -f - -i
