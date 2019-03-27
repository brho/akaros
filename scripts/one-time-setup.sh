#!/bin/bash

if [ -z "$AKAROS_ROOT" ]
then
	echo Error: you need to set AKAROS_ROOT
	exit -1
fi

if [ -z "$AKAROS_XCC_ROOT" ]
then
	echo Error: you need to set AKAROS_ROOT
	exit -1
fi
