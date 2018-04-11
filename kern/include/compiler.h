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
