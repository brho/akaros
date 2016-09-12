/* Assembler macros for x86-64.
   Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef _AKAROS_X86_64_SYSDEP_H
#define _AKAROS_X86_64_SYSDEP_H 1

/* Various bits copied from Linux and its #included sysdep.h */

#include <sysdeps/generic/sysdep.h>
#include <sysdeps/x86_64/sysdep.h>
#include <tls.h>

#ifdef IS_IN_rtld
# include <dl-sysdep.h>		/* Defines RTLD_PRIVATE_ERRNO.  */
#endif

/* Pointer mangling support.  */
#if defined NOT_IN_libc && defined IS_IN_rtld
/* We cannot use the thread descriptor because in ld.so we use setjmp
   earlier than the descriptor is initialized.  */
# ifdef __ASSEMBLER__
#  define PTR_MANGLE(reg)	xor __pointer_chk_guard_local(%rip), reg;    \
				rol $2*LP_SIZE+1, reg
#  define PTR_DEMANGLE(reg)	ror $2*LP_SIZE+1, reg;			     \
				xor __pointer_chk_guard_local(%rip), reg
# else
#  define PTR_MANGLE(reg)	asm ("xor __pointer_chk_guard_local(%%rip), %0\n" \
				     "rol $2*" LP_SIZE "+1, %0"			  \
				     : "=r" (reg) : "0" (reg))
#  define PTR_DEMANGLE(reg)	asm ("ror $2*" LP_SIZE "+1, %0\n"		  \
				     "xor __pointer_chk_guard_local(%%rip), %0"   \
				     : "=r" (reg) : "0" (reg))
# endif
#else
# ifdef __ASSEMBLER__
#  define PTR_MANGLE(reg)	xor %fs:POINTER_GUARD, reg;		      \
				rol $2*LP_SIZE+1, reg
#  define PTR_DEMANGLE(reg)	ror $2*LP_SIZE+1, reg;			      \
				xor %fs:POINTER_GUARD, reg
# else
#  define PTR_MANGLE(var)	asm ("xor %%fs:%c2, %0\n"		      \
				     "rol $2*" LP_SIZE "+1, %0"		      \
				     : "=r" (var)			      \
				     : "0" (var),			      \
				       "i" (offsetof (tcbhead_t,	      \
						      pointer_guard)))
#  define PTR_DEMANGLE(var)	asm ("ror $2*" LP_SIZE "+1, %0\n"	      \
				     "xor %%fs:%c2, %0"			      \
				     : "=r" (var)			      \
				     : "0" (var),			      \
				       "i" (offsetof (tcbhead_t,	      \
						      pointer_guard)))
# endif
#endif

#endif /* akaros/x86_64/sysdep.h */
