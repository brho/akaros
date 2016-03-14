#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

init_script_default="\"Read from Akaross .config\""

function short_description() {
	echo "Launch qemu with a running instance of Akaros"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} -h | --help"
	echo "    ${cmd} [ --akaros-root=<ak> ]"
	echo "    ${cmd//?/ } [ --init-script=<script> ]"
	echo "    ${cmd//?/ } [ --qemu-cmd=<cmd> ]"
	echo "    ${cmd//?/ } [ --cpu-type=<cpu> ]"
	echo "    ${cmd//?/ } [ --num-cores=<nc> ]"
	echo "    ${cmd//?/ } [ --memory-size=<ms> ]"
	echo "    ${cmd//?/ } [ --network-card=<nc> ]"
	echo "    ${cmd//?/ } [ --host-tcp-port=<htp> ]"
	echo "    ${cmd//?/ } [ --akaros-tcp-port=<atp> ]"
	echo "    ${cmd//?/ } [ --host-udp-port=<hup> ]"
	echo "    ${cmd//?/ } [ --akaros-udp-port=<aup> ]"
	echo "    ${cmd//?/ } [ --disable-kvm ]"
	echo ""
	echo "Options:"
	echo "    -h --help               Display this screen and exit"
	echo "    --akaros-root=<ak>      The path to the root of the akaros tree"
	echo "                            [default: \$AKAROS_ROOT]"
	echo "    --init-script=<script>  The path to a custom init script to run after boot"
	echo "                            [default: ${init_script_default} ]"
	echo "    --qemu-cmd=<cmd>        The actual qemu command to use"
	echo "                            [default: qemu-system-x86_64]"
	echo "    --cpu-type=<cpu>        The type of cpu to launch qemu with"
	echo "                            [default: kvm64,+vmx]"
	echo "    --num-cores=<nc>        The number of cores to launch qemu with"
	echo "                            [default: 8]"
	echo "    --memory-size=<ms>      The amount of memory to launch qemu with (kB)"
	echo "                            [default: 4096]"
	echo "    --network-card=<nc>     The network card to launch qemu qith"
	echo "                            [default: e1000]"
	echo "    --host-tcp-port=<htp>   The host TCP port to forward network traffic"
	echo "                            [default: 5555]"
	echo "    --akaros-tcp-port=<atp> The Akaros TCP port to receive network traffic"
	echo "                            [default: 5555]"
	echo "    --host-udp-port=<hup>   The host UDP port to forward network traffic"
	echo "                            [default: 5555]"
	echo "    --akaros-udp-port=<aup> The Akaros UDP port to receive network traffic"
	echo "                            [default: 5555]"
	echo "    --disable-kvm           Disable kvm for qemu"
}

function main() {
	# Set these command line arguments before invoking main
	# Check the sanity of our environment variables
	check_vars akaros_root qemu_cmd init_script cpu_type num_cores \
	           memory_size network_card host_tcp_port akaros_tcp_port \
	           host_udp_port akaros_udp_port
	check_dirs akaros_root
	check_execs qemu_cmd
	if [ "\"${init_script}\"" != "${init_script_default}" ]; then
		check_files init_script
		local init_script_set="true"
	fi

	# Set some local variables
	local akaros_bin=${akaros_root}/kern/kfs/bin
	local akaros_kernel=${akaros_root}/obj/kern/akaros-kernel
	local akaros_config=${akaros_root}/.config
	local akaros_config_backup=${akaros_root}/.config.backup
	local akinit_script="/bin/ak-init.sh"
	local akinit_script_path=${akaros_root}/kern/kfs/${akinit_script}
	local qemu_network="-net nic,model=${network_card} \
	                    -net user,hostfwd=tcp::${host_tcp_port}-:${akaros_tcp_port},hostfwd=udp::${host_udp_port}-:${akaros_udp_port}"

	# Launch the monitor if not launched yet and set the monitor tty
	local monitor_tty="$(ak launch-qemu-monitor --print-tty-only)"
	if [ "${monitor_tty}" != "" ]; then
		local qemu_monitor="-monitor ${monitor_tty}"
	fi

	# Output a warning if we are trying to enable kvm for qemu, but we are not
	# part of the kvm group
	if [ "${disable_kvm}" == "false" ]; then
		groups ${USER} | grep &>/dev/null '\bkvm\b'
		if [ "${?}" != "0" ]; then
			echo "You are not part of the kvm group!"
			echo "    This may cause problems with running qemu with kvm enabled."
			echo "    To disable kvm, rerun this script with --disable-kvm."
		fi
		local qemu_kvm="-enable-kvm"
	fi

	# Make a backup of ${akaros_config} and set the init script in it
	if [ "${init_script_set}" == "true" ]; then
		echo "Setting custom init script"
		cp ${akaros_config} ${akaros_config_backup}
		cp ${init_script} ${akinit_script_path}
		if [ "$(grep 'CONFIG_RUN_INIT_SCRIPT=' ${akaros_config})" = "" ]; then
			echo "CONFIG_RUN_INIT_SCRIPT=y" >> ${akaros_config}
			echo "CONFIG_INIT_SCRIPT_PATH_AND_ARGS=\"${akinit_script}\"" >> ${akaros_config}
		else
			sed -ie 's#CONFIG_INIT_SCRIPT_PATH_AND_ARGS=.*#CONFIG_INIT_SCRIPT_PATH_AND_ARGS="'${akinit_script}'"#' ${akaros_config}
		fi
	fi

	# Rebuild akaros
	echo "Rebuilding akaros"
	cd ${akaros_root}
	touch ${akaros_config}
	make -j
	cd - > /dev/null

	# Restore the original ${akaros_config} and delete the init script
	if [ "${init_script_set}" == "true" ]; then
		mv ${akaros_config_backup} ${akaros_config}
		rm ${akinit_script_path}
	fi

	# Rebuild akaros test and libs
	echo "Rebuilding tests and libs"
	cd ${akaros_root}
	make -j install-libs
	make -j tests
	make fill-kfs
	cd - > /dev/null

	# Launching qemu
	echo "Launching qemu"
	local stty_state=$(stty -g)
	stty raw
	${qemu_cmd} -s ${qemu_kvm} ${qemu_network} ${qemu_monitor} -cpu ${cpu_type} \
	            -smp ${num_cores} -m ${memory_size} -kernel ${akaros_kernel} \
	            -nographic
	stty ${stty_state}
}

