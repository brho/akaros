#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

function short_description() {
	echo "Launch a Go 9pserver for Akaros"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} -h | --help"
	echo "    ${cmd} [ --gopath=<gp> ]"
	echo "    ${cmd//?/ } [ --akaros-9p-root=<mnt> ]"
	echo "    ${cmd//?/ } [ --ufs-port=<port> ]"
	echo "    ${cmd//?/ } [ --clear-mount ]"
	echo "    ${cmd//?/ } [ --rebuild-server ]"
	echo ""
	echo "Options:"
	echo "    -h --help               Display this screen and exit"
	echo "    --gopath=<gp>           The path to the go workspace"
	echo "                            [default: \$GOPATH]"
	echo "    --akaros-9p-root=<mnt>  The location of the akaros 9p mount point"
	echo "                            [default: \$AKAROS_9P_ROOT]"
	echo "    --ufs-port=<port>       Port to connect the server on"
	echo "                            [default: 1025]"
	echo "    --clear-mount           Clear the 9p mount folder before mounting"
	echo "    --rebuild-server        Download and rebuild the 9pserver"
}

function main() {
	# Check the sanity of our incoming variables
	check_vars gopath akaros_9p_root ufs_port clear_mount rebuild_server
	check_dirs gopath akaros_9p_root

	# Set up the go environment variables
	eval $(go env)

	# If we don't have a server at all, force a rebuild
	if [ ! -f ${gopath}/bin/ufs ]; then
		rebuild_server=true
	fi

	# Get the latest 9p server which supports akaros
	if [ ${rebuild_server} = true ]; then
		echo "Downloading and installing the latest supported 9p server"
		export GOOS=${GOHOSTOS}
		export GOARCH=${GOHOSTARCH}
		go get -d -u github.com/rminnich/go9p
		go get -d -u github.com/rminnich/go9p/ufs
		go install github.com/rminnich/go9p/ufs
	fi

	# Clear out the ${akaros_9p_root} directory
	if [ ${clear_mount} = true ]; then
		echo "Clearing out ${akaros_9p_root}"
		rm -rf ${akaros_9p_root}
	fi
	mkdir -p ${akaros_9p_root}

	# Kill any old instances of the ufs server on ${ufs_port}
	local ufs_pid=$(ps aux | grep "ufs" | grep "\-addr=:${ufs_port}" \
	                       | head -1 | awk '{print $2}' )
	if [ "${ufs_pid}" != "" ]; then
		echo "Killing old 9p server instance on port=${ufs_port} (pid ${ufs_pid})"
		echo "${ufs_pid}" | xargs kill
	fi

	# Start a new ufs instance on ${ufs_port}
	nohup ${gopath}/bin/ufs -akaros=true -addr=:${ufs_port} \
	                        -root=${akaros_9p_root} >/dev/null 2>&1 &
	echo "Started 9p server port=${ufs_port} root=${akaros_9p_root} (pid ${!})"
}
