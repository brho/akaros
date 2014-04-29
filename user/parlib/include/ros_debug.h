#ifndef PARLIB_INC_DEBUG_H
#define PARLIB_INC_DEBUG_H

#include <ros/common.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void ros_debugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...);
void ros_vdebugfmt(void (*putch)(int, void**), void **putdat, const char *fmt, va_list);

int	ros_debug(const char *fmt, ...);
int	ros_vdebug(const char *fmt, va_list);

//#define PRINTD_DEBUG
#ifdef PRINTD_DEBUG
#define printd(args...) printf(args)
#else
#define printd(args...) {}
#endif

/* For a poor-mans function tracer (can add these with spatch) */
void __print_func_entry(const char *func, const char *file);
void __print_func_exit(const char *func, const char *file);
#define print_func_entry() __print_func_entry(__FUNCTION__, __FILE__)
#define print_func_exit() __print_func_exit(__FUNCTION__, __FILE__)

#ifdef __cplusplus
}
#endif

#endif /* !PARLIB_INC_DEBUG_H */
