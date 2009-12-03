#ifndef PARLIB_INC_DEBUG_H
#define PARLIB_INC_DEBUG_H

#ifndef __va_list__
typedef __builtin_va_list va_list;
#endif

#define va_start(v,l)   __builtin_va_start(v,l)
#define va_end(v)	__builtin_va_end(v)
#define va_arg(v,l)     __builtin_va_arg(v,l)

size_t	strnlen(const char *NTS s, size_t size);
#ifdef __DEPUTY__
void debugfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *NTS fmt, ...);
void vdebugfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *NTS fmt, va_list);
#else
void debugfmt(void (*putch)(int, void**), void **putdat, const char *NTS fmt, ...);
void vdebugfmt(void (*putch)(int, void**), void **putdat, const char *NTS fmt, va_list);
#endif

int	debug(const char * NTS fmt, ...);
int	vdebug(const char * NTS fmt, va_list);

#endif /* !PARLIB_INC_DEBUG_H */
