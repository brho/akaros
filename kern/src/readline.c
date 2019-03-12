#include <error.h>
#include <stdio.h>
#include <assert.h>
#include <atomic.h>

int readline(char *buf, size_t buf_l, const char *prompt, ...)
{
	static spinlock_t readline_lock = SPINLOCK_INITIALIZER_IRQSAVE;
	int i, c, echoing, retval;
	va_list ap;

	spin_lock_irqsave(&readline_lock);
	va_start(ap, prompt);
	if (prompt != NULL)
		vcprintf(prompt, ap);
	va_end(ap);

	i = 0;
	echoing = iscons(0);
	while (1) {
		c = getchar();
		if (c < 0) {
			printk("read error: %d\n", c);
			retval = i;
			break;
		} else if (c == '\b' || c == 0x7f) {
			if (i > 0) {
				if (echoing)
					cputchar(c);
				i--;
			}
			continue;
		} else if (c == '\n' || c == '\r') {
			/* sending a \n regardless, since the serial port gives
			 * us a \r for carriage returns. (probably won't get a
			 * \r anymore) */
			if (echoing)
				cputchar('\n');
			assert(i <= buf_l - 1);
			/* never write to buf_l - 1 til the end */
			buf[i++] = c;
			retval =  i;
			break;
		} else if (c >= ' ' && i < buf_l - 1) {
			if (echoing)
				cputchar(c);
			buf[i++] = c;
		}
	}
	spin_unlock_irqsave(&readline_lock);
	return retval;
}

