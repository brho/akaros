#pragma once

#include <ros/common.h>
#include <stdarg.h>
#include <kdebug.h>

#ifndef NULL
#define NULL	((void *) 0)
#endif /* !NULL */

//#define DEBUG
#ifdef DEBUG
#define printd(args...) cprintf(args)
#else
#define printd(args...) {}
#endif

#define printk(args...) cprintf(args)
#define __I_AM_HERE_GUTS "Core %d is in %s() at %s:%d\n", core_id(), \
                         __func__, __FILE__, __LINE__

#define I_AM_HERE printk(__I_AM_HERE_GUTS)
#define I_AM_HERE_x printx(__I_AM_HERE_GUTS)
#define I_AM_HERE_t trace_printk(__I_AM_HERE_GUTS)
#define I_AM_HERE_tx trace_printx(__I_AM_HERE_GUTS)

// lib/stdio.c
void	cputchar(int c);
void	cputbuf(const char *buf, int len);
int	getchar(void);
int	iscons(int fd);

// lib/printfmt.c
void	printfmt(void (*putch)(int, void**), void **putdat, const char *fmt, ...);
void	vprintfmt(void (*putch)(int, void**), void **putdat, const char *fmt, va_list);

// lib/printf.c
extern bool panic_skip_print_lock;
void print_lock(void);
void print_unlock(void);
void print_unlock_force(void);
int	( cprintf)(const char *fmt, ...);
int	vcprintf(const char *fmt, va_list);

// lib/sprintf.c

static inline bool snprintf_error(int ret, size_t buf_len)
{
	return ret < 0 || ret >= buf_len;
}

int snprintf(char *str, size_t size, const char *fmt, ...);
int vsnprintf(char *str, size_t size, const char *fmt, va_list);

// lib/fprintf.c
int	printf(const char *fmt, ...);
int	fprintf(int fd, const char *fmt, ...);
int	vfprintf(int fd, const char *fmt, va_list);

// lib/readline.c
int readline(char *buf, size_t buf_l, const char *prompt, ...);

char *seprintf(char *buf, char *end, const char *fmt, ...);

// kern/src/net/eipconv.c
void printemac(void (*putch)(int, void**), void **putdat, uint8_t *mac);
void printip(void (*putch)(int, void**), void **putdat, uint8_t *ip);
void printipmask(void (*putch)(int, void**), void **putdat, uint8_t *ip);
void printipv4(void (*putch)(int, void**), void **putdat, uint8_t *ip);

/* #K */
void trace_printk(const char *fmt, ...);

/* vsprintf.c (linux) */
int vsscanf(const char *buf, const char *fmt, va_list args);
int sscanf(const char *buf, const char *fmt, ...);
