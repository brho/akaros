// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#include <inc/types.h>
#include <inc/stdio.h>
#include <inc/stdarg.h>
#include <inc/atomic.h>

uint32_t output_lock = 0;

static void
putch(int ch, int *cnt)
{
	cputchar(ch);
	*cnt = *cnt + 1;
}

int
vcprintf(const char *fmt, va_list ap)
{
	int cnt = 0;

	// lock all output.  this will catch any printfs at line granularity
	spin_lock_irqsave(&output_lock);
	vprintfmt(putch, &cnt, fmt, ap);
	spin_unlock_irqsave(&output_lock);

	return cnt;
}

int
cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	va_start(ap, fmt);
	cnt = vcprintf(fmt, ap);
	va_end(ap);

	return cnt;
}

