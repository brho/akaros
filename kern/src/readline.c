#ifdef __SHARC__
#pragma nosharc
#endif

#include <error.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>

int readline(char *buf, size_t buf_l, const char *prompt, ...)
{
	static spinlock_t readline_lock = SPINLOCK_INITIALIZER;
	int i, c, echoing, retval;
	va_list ap;

	va_start(ap, prompt);
	if (prompt != NULL)
		vcprintf(prompt, ap);
	va_end(ap);

	i = 0;
	spin_lock_irqsave(&readline_lock);
	echoing = iscons(0);
	while (1) {
		c = getchar();
		if (c < 0) {
			printk("read error: %e\n", c);	/* %e! */
			retval = i;
			break;
		} else if (c >= ' ' && i < buf_l - 1) {
			if (echoing)
				cputchar(c);
			buf[i++] = c;
		} else if (c == '\b' && i > 0) {
			if (echoing)
				cputchar(c);
			i--;
		} else if (c == '\n' || c == '\r') {
			if (echoing)
				cputchar(c);
			assert(i <= buf_l - 1);	/* never write to buf_l - 1 til the end */
			buf[i++] = c;
			retval =  i;
			break;
		}
	}
	spin_unlock_irqsave(&readline_lock);
	return retval;
}

