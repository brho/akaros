#!/bin/bash
# This script should be called from Jenkins when a new commit has been pushed 
# to the repo. 
# It analyzes what parts of the codebase have been modified, compiles everything
# that is needed, and reports on the results. 

set -e

readonly TMP_DIR=tmp
readonly DIFF_FILE=$TMP_DIR/changes.txt
readonly AKAROS_OUTPUT_FILE=$TMP_DIR/akaros_out.txt
readonly TEST_OUTPUT_DIR=output-tests
readonly TEST_DIR=tools/jenkins
readonly SCR_DIR=tools/jenkins/utils
readonly DOWNLOADS_DIR=dl

# Config files
readonly CONF_DIR=tools/jenkins/config
readonly CONF_COMP_COMPONENTS_FILE=$CONF_DIR/compilation_components.json

# Utility scripts
readonly SCR_WAIT_UNTIL=$SCR_DIR/wait_until.py
readonly SCR_GIT_CHANGES=$SCR_DIR/changes.py
readonly SCR_GEN_TEST_REPORTS=$SCR_DIR/test_reporter.py

# Busybox settings
readonly BUSYBOX_VERSION=1.17.3
readonly BUSYBOX_DL_URL=http://www.busybox.net/downloads/busybox-1.17.3.tar.bz2
readonly BUSYBOX_CONF_FILE=tools/patches/busybox/busybox-1.17.3-config

################################################################################
###############                   INITIAL SETUP                  ###############
################################################################################

if [ "$INITIAL_SETUP" == true ]; then
	echo -e "\n[INITIAL_SETUP]: Begin"
	# Create directory for tests and other temp files.
	mkdir -p $TMP_DIR
	mkdir -p $TEST_OUTPUT_DIR
	mkdir -p $DOWNLOADS_DIR

	# Compile QEMU launcher
	mkdir -p $WORKSPACE/install/qemu_launcher/
	gcc $SCR_DIR/qemu_launcher.c -o install/qemu_launcher/qemu_launcher

	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *"
	echo "Set up finished succesfully."
	echo "Please run sudo chown root:root install/qemu_launcher/qemu_launcher"
	echo "Please run sudo chmod 4755 install/qemu_launcher/qemu_launcher"
	echo "* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *"
	echo ""
	echo -e "[INITIAL_SETUP]: End\n"
	exit 0
fi



################################################################################
###############                 PRE BUILD SETUP                  ###############
################################################################################

function add_cross_compiler_to_path() {
	export PATH=$WORKSPACE/install/riscv-ros-gcc/bin:$PATH
	export PATH=$WORKSPACE/install/i686-ros-gcc/bin:$PATH
	export PATH=$WORKSPACE/install/x86_64-ros-gcc/bin:$PATH
}

# Clean these two directories
rm $TMP_DIR/* $TEST_OUTPUT_DIR/* -f
add_cross_compiler_to_path


################################################################################
###############                    COMPILATION                   ###############
################################################################################

function build_config() {
	echo -e "\n[SET_MAKE_CONFIG]: Begin"

	# Begin with default configuration.
	case "$COMPILATION_ARCH" in
	RISCV)  make ARCH=riscv defconfig
	    ;;
	I686)  make ARCH=x86 defconfig
		   sed -i -e 's/CONFIG_64BIT=y/# CONFIG_64BIT is not set/' \
		          -e 's/# CONFIG_X86_32 is not set/CONFIG_X86_32=y/' \
		          -e 's/CONFIG_X86_64=y/# CONFIG_X86_64 is not set/' \
		          .config
	    ;;
	X86_64)  make ARCH=x86 defconfig
	    ;;
	esac

	# Enable postboot kernel tests to run.
	# These don't take much to execute so we can run them always and just parse
	# results if needed.
	echo "CONFIG_POSTBOOT_KERNEL_TESTING=y" >> .config

	echo -e "[SET_MAKE_CONFIG]: End\n"
}

function build_cross_compiler() {
	declare -A ARCH_SUBDIRS=( ["RISCV"]="riscv-ros-gcc" \
	                          ["I686"]="i686-ros-gcc" \
	                          ["X86_64"]="x86_64-ros-gcc" )

	echo -e "\n[BUILD_CROSS_COMPILER]: Begin"

	cd tools/compilers/gcc-glibc

	# Clean everything up
	# TODO: Possibly down the line try to optimize this to only clean the 
	# architecture that we need to rebuild.
	make clean

	# Define cross compiler Makelocal.
	echo "# Number of make jobs to spawn.  
MAKE_JOBS := 3
RISCV_INSTDIR         := $WORKSPACE/install/${ARCH_SUBDIRS["RISCV"]}/
I686_INSTDIR          := $WORKSPACE/install/${ARCH_SUBDIRS["I686"]}/
X86_64_INSTDIR        := $WORKSPACE/install/${ARCH_SUBDIRS["X86_64"]}/
" > Makelocal

	# Create / clean directory where the cross compiler will be installed.
	CROSS_COMP_DIR=$WORKSPACE/install/${ARCH_SUBDIRS["$COMPILATION_ARCH"]}/
	mkdir -p CROSS_COMP_DIR
	rm -rf CROSS_COMP_DIR*

	# Compile cross compiler.
	case "$COMPILATION_ARCH" in
	RISCV)  make riscv
	    ;;
	I686)  make i686
	    ;;
	X86_64)  make x86_64
	    ;;
	esac

	# Go back to root directory.
	cd ../../..
	echo -e "[BUILD_CROSS_COMPILER]: End\n"
}

function build_kernel() {
	echo -e "\n[BUILD_KERNEL]: Begin"
	make clean
	make
	echo -e "[BUILD_KERNEL]: End\n"
}

function build_userspace() {
	echo -e "\n[BUILD_USERSPACE]: Begin"
	# This is needed because of a bug that won't let tests to be compiled
	# unless the following files are present.
	cd kern/kfs/bin
	touch busybox
	touch chmod
	cd -

	# Build and install user libs.
	make userclean
	make install-libs

	# Compile tests.
	make testclean
	make tests

	# Fill memory with tests.
	make fill-kfs

	# Recompile kernel.
	make
	echo -e "[BUILD_USERSPACE]: End\n"
}

function build_busybox() {
	echo -e "\n[BUILD_BUSYBOX]: Begin"
	
	BUSYBOX_DIR=busybox-$BUSYBOX_VERSION
	
	cd $DOWNLOADS_DIR
	
	# Download busybox if we do not have it yet.
	if [[ ! -d "$BUSYBOX_DIR" ]]; then
		echo "Trying to download from $BUSYBOX_DL_URL ..."
		
		wget $BUSYBOX_DL_URL -O busybox-$BUSYBOX_VERSION.tar.bz2
		tar -jxvf busybox-$BUSYBOX_VERSION.tar.bz2
		rm busybox-$BUSYBOX_VERSION.tar.bz2
		cp ../$BUSYBOX_CONF_FILE $BUSYBOX_DIR/.config
	fi

	# Build busybox and copy it into kfs
	cd $BUSYBOX_DIR
	make
	cp busybox_unstripped ../../kern/kfs/bin/busybox
	cd ../../

	# Recompile kernel to include busybox
	make

	echo -e "[BUILD_BUSYBOX]: End\n"
}

# TODO: This won't work for RISCV, it must be changed to whatever is used.
function run_qemu() {
	echo -e "\n[RUN_AKAROS_IN_QEMU]: Begin"

	echo "-include $CONF_DIR/Makelocal_qemu" > Makelocal
	export PATH=$WORKSPACE/install/qemu_launcher/:$PATH
	make qemu > $AKAROS_OUTPUT_FILE &
	MAKE_PID=$!

	# TODO: Rather than finishing after Kernel PB Tests, put a generic 
	#       "C'est fini" statement somewhere and look for it
	WAIT_RESULT=`$SCR_WAIT_UNTIL $AKAROS_OUTPUT_FILE END_KERNEL_POSTBOOT_TESTS \
	    ${MAX_RUN_TIME:-100}`

	# Extract Qemu_launcher PID
	QEMU_PID=`ps --ppid $MAKE_PID | grep qemu_launcher | sed -e 's/^\s*//' | \
	          cut -d' ' -f1`

	# To kill qemu we need to send a USR1 signal to Qemu_launcher.
	kill -10 $QEMU_PID

	wait $MAKE_PID

	echo -e "[RUN_AKAROS_IN_QEMU]: End\n"

	# If the run was terminated via a timeout, then we finish with an error.
	if [[ "$WAIT_RESULT" == TIMEOUT ]]; then
		echo "AKAROS was terminated after running for $MAX_RUN_TIME seconds."
		exit 1
	fi
}



if [ "$COMPILE_ALL" == true ]; then
	echo "Building all AKAROS"
	build_config
	
	build_cross_compiler
	build_kernel
	build_userspace
	build_busybox

	run_qemu

	AFFECTED_COMPONENTS="cross-compiler kernel userspace busybox"
else
	# Save changed files between last tested commit and current one.
	git diff --stat $GIT_PREVIOUS_COMMIT $GIT_COMMIT > $DIFF_FILE

	# Extract build targets by parsing diff file.
	AFFECTED_COMPONENTS=`$SCR_GIT_CHANGES $DIFF_FILE $CONF_COMP_COMPONENTS_FILE`
	# Can contain {cross-compiler, kernel, userspace, busybox}

	if [[ -n $AFFECTED_COMPONENTS ]]; 
	then
		echo "Detected changes in "$AFFECTED_COMPONENTS
		build_config

		if [[ $AFFECTED_COMPONENTS == *cross-compiler* ]]
		then
			build_cross_compiler
			build_kernel
			build_userspace
			build_busybox
		else 
			if [[ $AFFECTED_COMPONENTS == *kernel* ]]
			then
				build_kernel
			fi

			if [[ $AFFECTED_COMPONENTS == *userspace* ]]
			then
				build_userspace
			fi

			if [[ $AFFECTED_COMPONENTS == *busybox* ]]
			then
				build_busybox
			fi
		fi
	else
		echo "Skipping build. No changes detected."
	fi

	run_qemu
fi


################################################################################
###############                  TEST REPORTING                  ###############
################################################################################

echo -e "\n[TEST_REPORTING]: Begin"

TESTS_TO_RUN="KERNEL_POSTBOOT" # TODO(alfongj): Remove this when not needed.
# for COMPONENT in "${AFFECTED_COMPONENTS_ARRAY[@]}"; 
# do
# 	# TODO(alfongj): Add to tests to run the name of the test suites to be ran.
# 	# TESTS_TO_RUN="$TESTS_TO_RUN SOMETHING"
# done

# Generate test report
$SCR_GEN_TEST_REPORTS $AKAROS_OUTPUT_FILE $TEST_OUTPUT_DIR $TESTS_TO_RUN
echo "Tests generated in $TEST_OUTPUT_DIR"

echo -e "[TEST_REPORTING]: End\n"
