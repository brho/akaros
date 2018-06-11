/* Initialization code run first thing by the ELF startup code.  Linux version.
   Copyright (C) 1995-2004, 2005, 2007 Free Software Foundation, Inc.
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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sysdep.h>
#include <fpu_control.h>
#include <sys/param.h>
#include <sys/types.h>
#include <libc-internal.h>
#include <malloc.h>
#include <assert.h>
#include <ldsodefs.h>
#include <locale/localeinfo.h>
#include <sys/time.h>
#include <elf.h>
#include <ctype.h>
#include <errno.h>

/* Set nonzero if we have to be prepared for more then one libc being
   used in the process.  Safe assumption if initializer never runs.  */
int __libc_multiple_libcs attribute_hidden = 1;

/* Remember the command line argument and enviroment contents for
   later calls of initializers for dynamic libraries.  */
int __libc_argc attribute_hidden;
char **__libc_argv attribute_hidden;

struct timeval __t0;

void
__libc_init_first (int argc, char **argv, char **envp)
{
#ifdef SHARED
  /* For DSOs we do not need __libc_init_first but instead _init.  */
}

void
attribute_hidden
_init (int argc, char **argv, char **envp)
{
#endif
#ifdef USE_NONOPTION_FLAGS
  extern void __getopt_clean_environment (char **);
#endif

  __libc_multiple_libcs = &_dl_starting_up && !_dl_starting_up;

  /* Make sure we don't initialize twice.  */
  if (!__libc_multiple_libcs)
    {
      /* Set the FPU control word to the proper default value if the
	 kernel would use a different value.  (In a static program we
	 don't have this information.)  */
#ifdef SHARED
      if (__fpu_control != GLRO(dl_fpu_control))
#endif
	__setfpucw (__fpu_control);
    }

  /* Save the command-line arguments.  */
  __libc_argc = argc;
  __libc_argv = argv;
  __environ = envp;

#ifndef SHARED
  extern const ElfW(Phdr) *_dl_phdr;
  extern size_t _dl_phnum;

  void** auxp = (void**)envp;
  while(*auxp)
    auxp++;
  ElfW(auxv_t) *av = (ElfW(auxv_t)*)(auxp+1);

  for ( ; av->a_type != AT_NULL; av++)
  {
    switch (av->a_type)
    {
      case AT_PHDR:
        _dl_phdr = (void *) av->a_un.a_val;
        break;
      case AT_PHNUM:
        _dl_phnum = av->a_un.a_val;
        break;
      case AT_PAGESZ:
        _dl_pagesize = av->a_un.a_val;
        break;
      case AT_ENTRY:
        /* user_entry = av->a_un.a_val; */
        break;
      case AT_PLATFORM:
        _dl_platform = (void *) av->a_un.a_val;
        break;
      case AT_HWCAP:
        _dl_hwcap = (unsigned long int) av->a_un.a_val;
        break;
    }
  }

  extern void __libc_setup_tls (size_t tcbsize, size_t tcbalign);
  __libc_setup_tls (TLS_INIT_TCB_SIZE, TLS_INIT_TCB_ALIGN);

  __libc_init_secure ();

  /* First the initialization which normally would be done by the
     dynamic linker.  */
  extern void _dl_non_dynamic_init (void) internal_function;
  _dl_non_dynamic_init ();
#endif

#ifdef VDSO_SETUP
  VDSO_SETUP ();
#endif

  __init_misc (argc, argv, envp);

#ifdef USE_NONOPTION_FLAGS
  /* This is a hack to make the special getopt in GNU libc working.  */
  __getopt_clean_environment (envp);
#endif

  /* Initialize ctype data.  */
  __ctype_init ();

#if defined SHARED && !defined NO_CTORS_DTORS_SECTIONS
  __libc_global_ctors ();
#endif

  gettimeofday(&__t0,0);
}


/* This function is defined here so that if this file ever gets into
   ld.so we will get a link error.  Having this file silently included
   in ld.so causes disaster, because the _init definition above will
   cause ld.so to gain an init function, which is not a cool thing. */

extern void _dl_start (void) __attribute__ ((noreturn));

void
_dl_start (void)
{
  abort ();
}

/* There are issues using stdio as part of rtld.  You'll get errors like: 
 * 		multiple definition of `__libc_multiple_libcs'
 * Some info: https://sourceware.org/ml/libc-hacker/2000-01/msg00170.html
 * For this reason, I couldn't put this in sysdeps/akaros/errno.c and still use
 * snprintf.  init-first is a reasonable dumping ground, and is one of the
 * sources of the multiple_libcs. */
void werrstr(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(errstr(), MAX_ERRSTR_LEN, fmt, ap);
	va_end(ap);
}
