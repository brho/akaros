#ifndef PARLIB_ARCH_VCORE_H
#define PARLIB_ARCH_VCORE_H

#ifdef __x86_64__
#include <arch/vcore64.h>
#else
#include <arch/vcore32.h>
#endif

#endif /* PARLIB_ARCH_VCORE_H */
