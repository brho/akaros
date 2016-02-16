#pragma once

#define PARLIB_ARCH_VCORE_H

#ifdef __x86_64__
#include <parlib/arch/vcore64.h>
#else
#include <parlib/arch/vcore32.h>
#endif

__BEGIN_DECLS

__END_DECLS
