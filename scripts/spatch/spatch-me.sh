#!/bin/bash
# Helper to run spatch on multiple files

if [ $# -lt 3 ]
then
	echo Usage: $0 cocci_file yes/no dir
	exit -1
fi

COCCI=$1

INPLACE=""

if [ $2 = "yes" ]
then
	INPLACE="-in-place"
fi

DIR=$3


FILES=`find $DIR -name '*.[ch]'`

for i in $FILES
do
	spatch -sp-file $COCCI $i $INPLACE
done
