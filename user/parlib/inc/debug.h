#ifndef PARLIB_INC_DEBUG_H
#define PARLIB_INC_DEBUG_H

typedef void * TRUSTED va_list;

#define __va_size(type) \
    (((sizeof(type) + sizeof(long) - 1) / sizeof(long)) * sizeof(long))

#define va_start(ap, last) \
    ((ap) = (va_list)&(last) + __va_size(last))

#define va_arg(ap, type) \
    (*(type *)((ap) += __va_size(type), (ap) - __va_size(type)))

#define va_end(ap)  ((void)0)

int	strnlen(const char *s, size_t size);
void debugfmt(void (*putch)(int, void**), void **putdat, const char *NTS fmt, ...);
void vdebugfmt(void (*putch)(int, void**), void **putdat, const char *NTS fmt, va_list);

int	debug(const char * NTS fmt, ...);
int	vdebug(const char * NTS fmt, va_list);

#endif /* !PARLIB_INC_DEBUG_H */
