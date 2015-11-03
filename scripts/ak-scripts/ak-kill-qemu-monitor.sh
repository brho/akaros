#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

function short_description() {
	echo "Kill the qemu monitor"
}

function usage() {
	echo "Usage: ${cmd}"
}

function main() {
	local name="ak-qemu-monitor"
	screen -X -S ${name} kill
}

