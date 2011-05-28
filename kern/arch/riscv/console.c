#include <arch/console.h>
#include <pmap.h>
#include <atomic.h>

long
fesvr_syscall(long n, long a0, long a1, long a2, long a3)
{
  static volatile uint64_t magic_mem[8];

  static spinlock_t lock = SPINLOCK_INITIALIZER;
  spin_lock_irqsave(&lock);

  magic_mem[0] = n;
  magic_mem[1] = a0;
  magic_mem[2] = a1;
  magic_mem[3] = a2;
  magic_mem[4] = a3;

  asm volatile ("cflush; fence");

  mtpcr(PCR_TOHOST, PADDR(magic_mem));
  while(mfpcr(PCR_FROMHOST) == 0);

  long ret = magic_mem[0];

  spin_unlock_irqsave(&lock);
  return ret;
}

void
fesvr_die()
{
	fesvr_syscall(FESVR_SYS_exit, 0, 0, 0, 0);
}

void
cons_init(void)
{
}

// `High'-level console I/O.  Used by readline and cprintf.

void
cputbuf(const char* buf, int len)
{
	fesvr_syscall(FESVR_SYS_write, 1, PADDR((uintptr_t)buf), len, 0);
}

// Low-level console I/O

void
cons_putc(int c)
{
	if(c == '\b' || c == 0x7F)
	{
		char buf[3] = {'\b', ' ', '\b'};
		cputbuf(buf,3);
	}
	else
	{
		char ch = c;
		cputbuf(&ch,1);
	}
}

void
cputchar(int c)
{
	char ch = c;
	cputbuf(&ch,1);
}

int
cons_getc()
{
	char ch;
	uintptr_t paddr = PADDR((uintptr_t)&ch);
	long ret = fesvr_syscall(FESVR_SYS_read, 0, paddr, 1, 0);
	if(ch == 0x7F)
		ch = '\b';
	return ret <= 0 ? 0 : ch;
}

int
getchar(void)
{
	int c;

	while ((c = cons_getc()) == 0)
		/* do nothing */;
	return c;
}

int
iscons(int fdnum)
{
	// used by readline
	return 1;
}
