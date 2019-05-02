/* See COPYRIGHT for copyright information. */

#pragma once

#include <compiler.h>

void _warn(const char *, int, const char *, ...);
struct hw_trapframe;
void _panic(struct hw_trapframe *, const char *, int, const char *, ...)
	__attribute__((noreturn));

#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define warn_once(...) run_once_racy(warn(__VA_ARGS__))
#define warn_on(x) do { if (x) warn(#x);} while (0)
#define warn_on_once(x) do { if (x) warn_once(#x);} while (0)

#define warn_on_user_ptr(x) do { \
	uintptr_t _x = (uintptr_t)(x); \
	warn_on(_x && _x < ULIM); \
} while (0)

#define panic(...) _panic(NULL, __FILE__, __LINE__, __VA_ARGS__)
#define panic_hwtf(x, ...) _panic(x, __FILE__, __LINE__, __VA_ARGS__)
#define exhausted(...) _panic(NULL, __FILE__, __LINE__, __VA_ARGS__)

#define assert(x)		\
	do { if (unlikely(!(x))) panic("assertion failed: %s", #x); } while (0)

#define error_assert(e, x) \
	do { if (unlikely(!(x))) error(e, "Assertion failed: " #x); } while (0)

// static_assert(x) will generate a compile-time error if 'x' is false.
#define static_assert(x)	switch (x) case 0: case (x):

#ifdef CONFIG_DEVELOPMENT_ASSERTIONS
#define dassert(x) assert(x)
#else
#define dassert(x) ((void) (x))  // 'Use' value, stop compile warnings
#endif /* DEVELOPMENT_ASSERTIONS */
