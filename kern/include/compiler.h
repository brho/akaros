#pragma once

#ifdef __GNUC__

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define __weak __attribute__((weak))

#else /* #ifdef __GNUC__ */

#define likely(x) (x)
#define unlikely(x) (x)
#define __weak

#endif /* #ifdef __GNUC__ */

#define __always_inline inline __attribute__((always_inline))

#ifdef __GNUC__

#define uninitialized_var(x) x = x

#elif defined(__clang__)

#define uninitialized_var(x) x = *(&(x))

#endif
