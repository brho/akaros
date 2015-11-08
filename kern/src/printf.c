// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#include <arch/arch.h>
#include <ros/common.h>

#include <atomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <smp.h>
#include <kprof.h>

spinlock_t output_lock = SPINLOCK_INITIALIZER_IRQSAVE;

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
	static char buf[buffered_putch_bufsize];
	static int buflen = 0;

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
	struct per_cpu_info *pcpui;
	extern int booting;
	int cnt = 0;
	int *cntp = &cnt;
	volatile int i;
	int8_t irq_state = 0;
	va_list args;

	va_copy(args, ap);
	trace_vprintk(false, fmt, args);
	va_end(args);

	/* this ktrap depth stuff is in case the kernel faults in a printfmt call.
	 * we disable the locking if we're in a fault handler so that we don't
	 * deadlock. */
	if (booting)
		pcpui = &per_cpu_info[0];
	else
		pcpui = &per_cpu_info[core_id()];
	/* lock all output.  this will catch any printfs at line granularity.  when
	 * tracing, we short-circuit the main lock call, so as not to clobber the
	 * results as we print. */
	if (!ktrap_depth(pcpui)) {
		#ifdef CONFIG_TRACE_LOCKS
		disable_irqsave(&irq_state);
		__spin_lock(&output_lock);
		#else
		spin_lock_irqsave(&output_lock);
		#endif
	}

	// do the buffered printf
	vprintfmt((void*)buffered_putch, (void*)&cntp, fmt, ap);

	// write out remaining chars in the buffer
	buffered_putch(-1,&cntp);

	if (!ktrap_depth(pcpui)) {
		#ifdef CONFIG_TRACE_LOCKS
		__spin_unlock(&output_lock);
		enable_irqsave(&irq_state);
		#else
		spin_unlock_irqsave(&output_lock);
		#endif
	}

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
