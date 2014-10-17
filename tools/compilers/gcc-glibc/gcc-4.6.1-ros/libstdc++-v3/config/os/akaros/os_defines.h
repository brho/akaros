/** @file bits/os_defines.h
 *  This is an internal header file, included by other library headers.
 *  Do not attempt to use it directly. @headername{iosfwd}
 */

#ifndef _GLIBCXX_OS_DEFINES
#define _GLIBCXX_OS_DEFINES 1

/* Try to avoid ioctls in the c++ build - I don't plan on supporting them in
 * the OS, and don't want a shim layer in glibc yet either. */
#define _GLIBCXX_NO_IOCTL 1

/* This is an alternate way to get showmanyc() to work, since we disabled the
 * IOCTL style. */
#define _GLIBCXX_HAVE_S_ISREG 1

#endif /* _GLIBCXX_OS_DEFINES */
