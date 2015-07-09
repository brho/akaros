/* Machine-dependent ELF dynamic relocation inline functions.  x86-64 version.
   Copyright (C) 2001-2014 Free Software Foundation, Inc.
   This file is part of the GNU C Library.
   Contributed by Andreas Jaeger <aj@suse.de>.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#include_next "dl-machine.h"

#undef RTLD_START

/* Initial entry point code for the dynamic linker.
 *    The C function `_dl_start' is the real entry point;
 *	   its return value is the user program's entry point.  */
#define RTLD_START \
static ElfW(Addr) __attribute_used__ internal_function _dl_start (void *arg);\
extern char **_environ attribute_hidden;\
void __attribute__((noreturn)) _start(void)\
{\
	/* Pass our stack pointer to _dl_start. */\
	/* Retreive our user entry point. */\
	void *rsp, *user_entry;\
	asm volatile("movq %%rsp, %0" : "=r" (rsp));\
	user_entry = (void*)_dl_start(rsp);\
	\
	/* Clear %rbp to mark outermost frame even for initializers. */\
	asm volatile("xorq %rbp, %rbp");\
	\
	/* Call _dl_init to run the initializers. */\
	_dl_init(GL(dl_ns[0]._ns_loaded), _dl_argc, INTUSE(_dl_argv), _environ);\
	\
	/* Pass our finalizer function to the user in %rdx, as per ELF ABI. */\
	/* And jump to the user! */\
	asm volatile(\
		"movq %0, %%rdx;"\
		"jmp *%1;"\
		: : "r" (_dl_fini), "r" (user_entry) : "rdx"\
	);\
	__builtin_unreachable();\
}

