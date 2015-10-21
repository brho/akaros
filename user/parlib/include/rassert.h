/* See COPYRIGHT for copyright information. */

#ifndef PARLIB_RASSERT_H
#define PARLIB_RASSERT_H

#include <assert.h>
#include <parlib/vcore.h>
#include <parlib/ros_debug.h>

__BEGIN_DECLS

#undef assert
#undef static_assert

void _warn(const char*, int, const char*, ...);
void _panic(const char*, int, const char*, ...) __attribute__((noreturn));

#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define panic(...) _panic(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)	                                                           \
	do {                                                                       \
		if (!(x)) {                                                            \
			ros_debug("[user] %s:%d, vcore %d, Assertion failed: %s\n",        \
			          __FILE__, __LINE__, vcore_id(), #x);                     \
			breakpoint();                                                      \
			abort();                                                           \
		}                                                                      \
	} while (0)

// static_assert(x) will generate a compile-time error if 'x' is false.
#define static_assert(x)	switch (x) case 0: case (x):

__END_DECLS

#endif /* PARLIB_RASSERT_H */
