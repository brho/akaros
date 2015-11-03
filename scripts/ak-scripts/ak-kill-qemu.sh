#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

function short_description() {
	echo "Kill any Akaros instances of qemu that are running"
}

function usage() {
	echo "Usage: ${cmd}"
}

function main() {
	local instance=$(ps aux | grep "qemu" | grep akaros-kernel)
	kill -9 $(echo ${instance} | cut -d" " -f 2)
}

