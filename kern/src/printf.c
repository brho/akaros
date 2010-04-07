// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#ifdef __SHARC__
#pragma nosharc
#endif

#include <arch/arch.h>
#include <ros/common.h>

#include <atomic.h>
#include <stdio.h>
#include <stdarg.h>

spinlock_t output_lock = SPINLOCK_INITIALIZER;

void putch(int ch, int **cnt)
{
	cputchar(ch);
	**cnt = **cnt + 1;
}

// buffered putch to (potentially) speed up printing.
// static buffer is safe because output_lock must be held.
// ch == -1 flushes the buffer.
void buffered_putch(int ch, int **cnt)
{
	#define buffered_putch_bufsize 64
	static char LCKD(&output_lock) (RO buf)[buffered_putch_bufsize];
	static int LCKD(&output_lock) buflen = 0;

	if(ch != -1)
	{
		buf[buflen++] = ch;
		**cnt = **cnt + 1;
	}

	if(ch == -1 || buflen == buffered_putch_bufsize)
	{
		cputbuf(buf,buflen);
		buflen = 0;
	}
}

int vcprintf(const char *fmt, va_list ap)
{
	int cnt = 0;
	int *cntp = &cnt;
	volatile int i;

	// lock all output.  this will catch any printfs at line granularity
	spin_lock_irqsave(&output_lock);

	// do the buffered printf
	#ifdef __DEPUTY__
	vprintfmt(buffered_putch, &cntp, fmt, ap);
	#else
	vprintfmt((void*)buffered_putch, (void*)&cntp, fmt, ap);
	#endif

	// write out remaining chars in the buffer
	buffered_putch(-1,&cntp);

	spin_unlock_irqsave(&output_lock);

	return cnt;
}

int cprintf(const char *fmt, ...)
{
	va_list ap;
	int cnt;

	if (!fmt)
		return 0;

	va_start(ap, fmt);
	cnt = vcprintf(fmt, ap);
	va_end(ap);

	return cnt;
}
