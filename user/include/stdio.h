#ifndef __PARLIB_STDIO_H__
#define __PARLIB_STDIO_H__

#include_next <stdio.h>

#ifndef __CONFIG_APPSERVER__
#include <debug.h>
#define printf(...) debug(__VA_ARGS__)
#endif

#endif
