#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

sleep_cmd="sleep 0xda39a3ee5e6b4b0d3255bfef95601890afd80709"
welcome_cmd="echo -ne \"Welcome to the qemu monitor!\nPress 'Ctrl-a d' to detach and return to your shell.\n\""

function short_description() {
	echo "Launch a qemu monitor to attach to a qemu instance"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} -h | --help"
	echo "    ${cmd} [ --print-tty-only ]"
	echo ""
	echo "Options:"
	echo "    -h --help         Display this screen and exit"
	echo "    --print-tty-only  Launch the monitor, print its tty and exit."
	echo "                      Dont actually enter the monitor."
}

function get_qemu_monitor_tty() {
	while [ "${tty_dev}" = "" ]; do
		local ps_cmd="ps -a -o tty=TTY -o args | grep \"${sleep_cmd}\" | grep -v grep"
		local ps_info="$(eval "${ps_cmd}")"
		local tty_dev="$(echo ${ps_info} | cut -d" " -f 1)"
	done
	echo "/dev/${tty_dev}"
}

function main() {
	# Create the monitor if there isn't one yet
	local name="ak-qemu-monitor"
	local list="$(screen -list | grep ${name})"
	if [ "${list}" == "" ]; then
		screen -d -m -S ${name} /bin/bash -c "${welcome_cmd};${sleep_cmd}"
	fi

	# If ${print_tty_only} is set, print the tty and exit
	if [ "${print_tty_only}" = "true" ]; then
		get_qemu_monitor_tty
		exit 0
	fi

	# Otherwise...
	# Print some info about using the monitor
	echo ""
	echo "You are about to enter the qemu monitor for Akaros!"
	echo "We use 'screen' to create a tty device to host the monitor."
	echo "Once attached, you can detach from the monitor at anytime"
	echo "using the normal screen command:"
	echo ""
	echo "    Ctrl-a d"
	echo ""
	echo "To reattach from a shell, simply rerun this script:"
	echo ""
	echo "    ak launch-qemu-monitor"
	echo ""
	echo "While in the monitor, you should be able to run all of"
	echo "the normal qemu monitor commands. See the following link"
	echo "for more information:"
	echo ""
	echo "    https://en.wikibooks.org/wiki/QEMU/Monitor"
	echo ""

	# Wait for any key to be pressed
	echo "Press any key to continue..."
	(tty_state="$(stty -g)"
	stty -icanon
	LC_ALL=C dd bs=1 count=1 > /dev/null 2>&1
	stty "$tty_state"
	) < /dev/tty

	# Attach the monitor
	screen -d -r ${name}
}

