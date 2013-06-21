/* Definitions for systems using the Linux kernel, with or without
   MMU, using ELF at the compiler level but possibly FLT for final
   linked executables and shared libraries in some no-MMU cases, and
   possibly with a choice of libc implementations.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2003, 2004, 2005, 2006,
   2007, 2009, 2010, 2011 Free Software Foundation, Inc.
   Contributed by Eric Youngdale.
   Modified for stabs-in-ELF by H.J. Lu (hjl@lucon.org).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.

You should have received a copy of the GNU General Public License and
a copy of the GCC Runtime Library Exception along with this program;
see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
<http://www.gnu.org/licenses/>.  */

#define ROS_TARGET_OS_CPP_BUILTINS()				\
    do {							\
	builtin_define ("__gnu_ros__");			\
	builtin_define_std ("ros");				\
	builtin_define_std ("unix");				\
	builtin_assert ("system=ros");			\
	builtin_assert ("system=unix");				\
	builtin_assert ("system=posix");			\
    } while (0)

#undef LINK_GCC_C_SEQUENCE_SPEC
#define LINK_GCC_C_SEQUENCE_SPEC \
  "--whole-archive -lparlib --no-whole-archive " \
  "%{static:--start-group} %G %L %{static:--end-group}%{!static:%G}"
