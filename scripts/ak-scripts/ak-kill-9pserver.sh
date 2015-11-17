#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# See LICENSE for details.

function short_description() {
	echo "Kill the Go 9pserver for Akaros with the specified port number"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} -h | --help"
	echo "    ${cmd} [ --ufs-port=<port> ]"
	echo ""
	echo "Options:"
	echo "    -h --help               Display this screen and exit"
	echo "    --ufs-port=<port>       Port the ufs server is on"
	echo "                            [default: 1025]"
}

function main() {
	# Check the sanity of our incoming variables
	check_vars ufs_port

	# Kill any old instances of the ufs server on ${ufs_port}
	local ufs_pid=$(ps aux | grep "ufs" | grep "\-addr=:${ufs_port}" \
	                       | head -1 | awk '{print $2}' )
	if [ "${ufs_pid}" != "" ]; then
		echo "Killing old 9p server instance on port=${ufs_port} (pid ${ufs_pid})"
		echo "${ufs_pid}" | xargs kill
	fi
}
