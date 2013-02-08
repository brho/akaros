#!/bin/bash
# Just the essentials
#cp -P ../ros-gcc-glibc/install-i386-ros-gcc/i686-ros/lib/ld* kern/kfs/lib/
#cp -P ../ros-gcc-glibc/install-i386-ros-gcc/i686-ros/lib/libc{-,.}* kern/kfs/lib/

# Full libc SOs  careful with -u, it won't help you change between archs
cp -uP ../ros-gcc-glibc/install-i386-ros-gcc/i686-ros/lib/*.so* kern/kfs/lib
#cp -uP ../ros-gcc-glibc/install-sparc-ros-gcc/sparc-ros/lib/*.so* kern/kfs/lib

# common test progs
#cp -u obj/tests/idle obj/tests/block_test obj/tests/c3po/c3po_test obj/tests/appender obj/tests/file_test obj/tests/fork obj/tests/hello obj/tests/mhello obj/tests/pthread_test obj/tests/spawn obj/tests/syscall obj/tests/tlstest obj/tests/ucq kern/kfs/bin/

# all test progs, something like this to get them
# don't do this if we're using static binaries, since things will get really large
cp -u `find obj/tests/ -executable ! -type d` kern/kfs/bin/
