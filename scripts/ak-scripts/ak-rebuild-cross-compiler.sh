#!/usr/bin/env bash
#
# Copyright (c) 2015 Google Inc.
# Kevin Klues <klueska@cs.berkeley.edu>
# See LICENSE for details.

inst_dir_default="\"\${ARCH}_INSTDIR based on the configured architecture in akaros_root\""

function short_description() {
	echo "Rebuild the Akaros cross compiler and all of its dependencies"
}

function usage() {
	echo "Usage:"
	echo "    ${cmd} -h | --help"
	echo "    ${cmd} [ --akaros-root=<ak> ]"
	echo "    ${cmd//?/ } [ --inst-dir=<dir> ]"
	echo ""
	echo "Options:"
	echo "    -h --help                Display this screen and exit"
	echo "    --akaros-root=<ak>       The path to the root of the akaros tree"
	echo "                             [default: \$AKAROS_ROOT]"
	echo "    --inst-dir=<dir>         The installation path of the cross compiler"
	echo "                             [default: ${inst_dir_default} ]"
}

function main() {
	# Check the sanity of our incoming variables
	check_vars akaros_root inst_dir
	check_dirs akaros_root

	# Set some local variables
	local arch="$(basename $(readlink ${akaros_root}/kern/include/arch))"
	if [ "${arch}" = "x86" ]; then
		arch="x86_64"
	fi
	local make_jobs=$(expr `cat /proc/cpuinfo | grep processor | wc -l` - 1)

	# Set real default of $inst_dir
	if [ "${inst_dir}" != "${inst_dir_default}" ]; then
		inst_dir="$(eval echo \$${arch^^}_INSTDIR)"
	fi

	# Rebuild the cross compiler
	cd "${akaros_root}"
	eval ${arch^^}_INSTDIR=${inst_dir} \
		make -j ${make_jobs} xcc-upgrade-from-scratch
	cd - > /dev/null
}
