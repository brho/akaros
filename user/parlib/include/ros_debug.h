#ifndef PARLIB_INC_DEBUG_H
#define PARLIB_INC_DEBUG_H

#include <ros/common.h>
#include <stdio.h>
#include <stdarg.h>

void ros_debugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...);
void ros_vdebugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, va_list);

int	ros_debug(const char *fmt, ...);
int	ros_vdebug(const char *fmt, va_list);

#ifndef __CONFIG_APPSERVER__
#undef printf
#define printf(...) ros_debug(__VA_ARGS__)
#endif /* __CONFIG_APPSERVER__ */

//#define PRINTD_DEBUG
#ifdef PRINTD_DEBUG
#define printd(args...) printf(args)
#else
#define printd(args...) {}
#endif

#endif /* !PARLIB_INC_DEBUG_H */
