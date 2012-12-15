#include <arch/console.h>
#include <console.h>
#include <pmap.h>
#include <atomic.h>
#include <smp.h>
#include <kmalloc.h>
#include <monitor.h>
#include <process.h>

int cons_get_any_char(void)
{
	assert(0);
}

void
cons_init(void)
{
	while (mtpcr(PCR_TOHOST, 0x0180000000000000));
}

// `High'-level console I/O.  Used by readline and cprintf.

void
cputbuf(const char* str, int len)
{
	for (int i = 0; i < len; i++)
		cputchar(str[i]);
}

void poll_keyboard()
{
	uintptr_t fh = mtpcr(PCR_FROMHOST, 0);
	if (fh == 0)
		return;
	assert((fh >> 56) == 0x01);

	char c = fh;
	if (c == 'G')
		send_kernel_message(core_id(), __run_mon, 0, 0, 0, KMSG_ROUTINE);
	else
		send_kernel_message(core_id(), __cons_add_char, (long)&cons_buf,
		                    (long)c, 0, KMSG_ROUTINE);
	cons_init();
}

// Low-level console I/O

void
cputchar(int c)
{
	while (mtpcr(PCR_TOHOST, 0x0101000000000000 | (unsigned char)c));
}

int
getchar(void)
{
	char c;
	kb_get_from_buf(&cons_buf, &c, 1);
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
