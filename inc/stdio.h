#ifndef JOS_INC_STDIO_H
#define JOS_INC_STDIO_H

#include <inc/stdarg.h>

#ifndef NULL
#define NULL	((void *) 0)
#endif /* !NULL */

// lib/stdio.c
void	cputchar(int c);
int	getchar(void);
int	iscons(int fd);

// lib/printfmt.c
void	printfmt(void (*putch)(int, void*), void *putdat, const char *NTS fmt, ...);
void	vprintfmt(void (*putch)(int, TV(t)), TV(t) putdat, const char *NTS fmt, va_list);

// lib/printf.c
int	cprintf(const char * NTS fmt, ...);
int	vcprintf(const char * NTS fmt, va_list);

// lib/sprintf.c
int	snprintf(char *str, int size, const char *fmt, ...);
int	vsnprintf(char *COUNT(size) str, int size, const char *fmt, va_list);

// lib/fprintf.c
int	printf(const char *fmt, ...);
int	fprintf(int fd, const char *fmt, ...);
int	vfprintf(int fd, const char *fmt, va_list);

// lib/readline.c
char *NTS readline(const char *NTS prompt);

#endif /* !JOS_INC_STDIO_H */
