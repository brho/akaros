#pragma once

/* Linux calls it __ASSEMBLY__ */
#ifdef __ASSEMBLER__
#define __ASSEMBLY__ 1
#endif

/* This is a bit hokey.  It turns off the #define inline to include
 * always_inline, which breaks our uses of "extern inline". */
#define CONFIG_ARCH_SUPPORTS_OPTIMIZED_INLINING 1
#define CONFIG_OPTIMIZE_INLINING 1

/* Make sure Linux's compiler.h is only included here. */
#define __AKAROS_COMPILER_H 1
#include <linux/compiler.h>
#undef __AKAROS_COMPILER_H

/* Linux uses this as a tag for the __CHECKER__ and either defined it to
 * nothing or to some attribute.  We use it for the name of the pcpu variables
 * .section, so need it to not be #defined yet. */
#undef __percpu

/* If __VA_ARGS__ is empty, the ## will eat the comma to the left, so we call
 * __MACRO_NARG with 1 + NR_ARGS arguments, then the 6, 5, 4, etc.  __MACRO_NARG
 * will strip the 0 (the _0), then the args (if any), and then return the Nth
 * integer passed in, which will be the number of original args. */
#define __MACRO_NARG(_0, _1, _2, _3, _4, _5, _6, x, ...) x
#define MACRO_NR_ARGS(...) __MACRO_NARG(0, ##__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0)
