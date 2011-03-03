/* See COPYRIGHT for copyright information. */

#ifndef ROS_INC_ASSERT_H
#define ROS_INC_ASSERT_H

#include <assert.h>
#include <vcore.h>
#undef assert

void _warn(const char*, int, const char*, ...);
void _panic(const char*, int, const char*, ...) __attribute__((noreturn));

#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define panic(...) _panic(__FILE__, __LINE__, __VA_ARGS__)

#define assert(x)		\
	do { if (!(x)) panic("assertion failed - vcore %d: %s", vcore_id(), #x); } while (0)

// static_assert(x) will generate a compile-time error if 'x' is false.
#define static_assert(x)	switch (x) case 0: case (x):

#endif /* !ROS_INC_ASSERT_H */
