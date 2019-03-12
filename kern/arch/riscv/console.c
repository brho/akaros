#include <arch/console.h>
#include <atomic.h>
#include <kmalloc.h>
#include <monitor.h>
#include <pmap.h>
#include <process.h>
#include <smp.h>

int cons_get_any_char(void)
{
	assert(0);
}

void cons_init()
{
	mtpcr(PCR_SR, mfpcr(PCR_SR) | (1 << (IRQ_HOST + SR_IM_SHIFT)));
	while (mtpcr(PCR_TOHOST, 0x01L << 56))
		;
}

// `High'-level console I/O.  Used by readline and cprintf.

void cputbuf(const char *str, int len)
{
	for (int i = 0; i < len; i++)
		cputchar(str[i]);
}

void poll_keyboard()
{
}

// Low-level console I/O

void cputchar(int c)
{
	while (mtpcr(PCR_TOHOST, 0x0101000000000000 | (unsigned char)c))
		;
}

int getchar(void)
{
	char c = 'x';

#warning "implement me"
	/* maybe do a qio read */

	return c;
}

int iscons(int fdnum)
{
	// used by readline
	return 1;
}
