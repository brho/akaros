#pragma once

#include <ros/common.h>
#include <compiler.h>

/* Force a rebuild of the whole kernel if 64BIT-ness changed */
#ifdef CONFIG_64BIT
#endif

#define SIZE_MAX        (~(size_t)0)
