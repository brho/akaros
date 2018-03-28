#pragma once

#include <arch/types.h>

/* Limits of integral types.  */

/* Minimum of signed integral types.  */
# define INT8_MIN		(-128)
# define INT16_MIN		(-32767-1)
# define INT32_MIN		(-2147483647-1)
# define INT64_MIN		(-__INT64_C(9223372036854775807)-1)
/* Maximum of signed integral types.  */
# define INT8_MAX		(127)
# define INT16_MAX		(32767)
# define INT32_MAX		(2147483647)
# define INT64_MAX		(__INT64_C(9223372036854775807))

/* Maximum of unsigned integral types.  */
# define UINT8_MAX		(255)
# define UINT16_MAX		(65535)
# define UINT32_MAX		(4294967295U)
# define UINT64_MAX		(__UINT64_C(18446744073709551615))

#if NUM_ADDR_BITS == 64
# define SIZE_MAX UINT64_MAX
# define LONG_MAX INT64_MAX
# define ULONG_MAX UINT64_MAX
#elif NUM_ADDR_BITS == 32
# define SIZE_MAX UINT32_MAX
# define LONG_MAX INT32_MAX
# define ULONG_MAX UINT32_MAX
#else
# error "NUM_ADDR_BITS is not set"
#endif
