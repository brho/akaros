#ifndef PARLIB_INC_DEBUG_H
#define PARLIB_INC_DEBUG_H

#include <ros/common.h>
#include <stdio.h>
#include <stdarg.h>

void debugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...);
void vdebugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, va_list);

int	debug(const char *fmt, ...);
int	vdebug(const char *fmt, va_list);

#ifndef __CONFIG_APPSERVER__
#undef printf
#define printf(...) debug(__VA_ARGS__)
#endif /* __CONFIG_APPSERVER__ */

#endif /* !PARLIB_INC_DEBUG_H */
