#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

# Some global variables
origin="brho"

function short_description() {
	echo "Prepare the message body for an akaros code review"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} [ -h | --help ]"
	echo "    ${cmd} [ -p ] <base> <remote> [ <head> ]"
	echo ""
	echo "Options:"
	echo "    -h --help  Display this screen and exit"
	echo "    -p         Show patch text as well"
	echo ""
	echo "Description:"
	echo "    This tool takes the same parameters as the standard"
	echo "    git request-pull command but formats the output"
	echo "    in a more convenient format for akaros code-reviews"
	echo "    Please copy the contents of the output into an email"
	echo "    and send it to akaros@googlegroups.com for review"
}

function gen_request()
{
	# Set some local variables
	local base_sha1=$(git rev-parse ${base})
	local head_sha1=${remote}:${head}
	base_sha1=${base_sha1:0:7}

	# Get the text from a git request-pull
	request=$(git request-pull ${patch} ${base} ${remote} ${head});
	ret=${?};
	if [ "${ret}" != "0" ]; then
		kill -s TERM $TOP_PID
	else
		echo "The changes in this request can be viewed online at:"
		echo ""
		echo "    https://github.com/brho/akaros/compare/${base_sha1}...${head_sha1}"
		echo ""
		echo "${request}"

	fi
}

function main() {
	# Set so functions can exit from entire program if desired
	trap "exit 1" TERM
	export TOP_PID=$$

	# Verify cmd-line options
	if [ "${head}" = "" ]; then
		head=$(git rev-parse --abbrev-ref HEAD)
	fi
	if [ "${_p}" = "true" ]; then
		local patch="-p"
	fi

	gen_request
}
