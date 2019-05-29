#!/bin/bash

if [ -z "$AKAROS_ROOT" ]
then
	echo Error: you need to set AKAROS_ROOT
	exit -1
fi

# This feature may not be upstream yet - you can use brho's git if it isn't.
# This tells blame to ignore certain commits know to be uninteresting.

git config --local --replace-all blame.ignorerevsfile .git-blame-ignore-revs
git config --local --replace-all blame.markignoredlines true
