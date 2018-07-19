// Simple implementation of cprintf console output for the kernel,
// based on printfmt() and the kernel console's cputchar().

#include <arch/arch.h>
#include <ros/common.h>

#include <atomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <smp.h>
#include <kprof.h>
#include <init.h>

/* Recursive lock.  Would like to avoid spreading these in the kernel. */
static spinlock_t output_lock = SPINLOCK_INITIALIZER_IRQSAVE;
static int output_lock_holder = -1;	/* core_id. */
static int output_lock_count;

void print_lock(void)
{
	if (output_lock_holder == core_id_early()) {
		output_lock_count++;
		return;
	}
	pcpui_var(core_id_early(), __lock_checking_enabled)--;
	spin_lock_irqsave(&output_lock);
	output_lock_holder = core_id_early();
	output_lock_count = 1;
}

void print_unlock(void)
{
	output_lock_count--;
	if (output_lock_count)
		return;
	output_lock_holder = -1;
	spin_unlock_irqsave(&output_lock);
	pcpui_var(core_id_early(), __lock_checking_enabled)++;
}

/* Regardless of where we are, unlock.  This is dangerous, and only used when
 * you know you will never unwind your stack, such as for a panic. */
void print_unlock_force(void)
{
	output_lock_holder = -1;
	output_lock_count = 0;
	spin_unlock_irqsave(&output_lock);
	pcpui_var(core_id_early(), __lock_checking_enabled)++;
}

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
	int cnt = 0;
	int *cntp = &cnt;
	volatile int i;
	va_list args;

	print_lock();

	va_copy(args, ap);
	trace_vprintk(fmt, args);
	va_end(args);

	// do the buffered printf
	vprintfmt((void*)buffered_putch, (void*)&cntp, fmt, ap);

	// write out remaining chars in the buffer
	buffered_putch(-1,&cntp);

	print_unlock();

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
