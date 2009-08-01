#include <arch/frontend.h>

void
cons_init(void)
{
}

// `High'-level console I/O.  Used by readline and cprintf.

void
cputbuf(const char*COUNT(len) buf, int len)
{
	frontend_syscall(RAMP_SYSCALL_write,1,buf,len);
}

// Low-level console I/O

inline void
cons_putc(int c)
{
	if(c == '\b')
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
        cons_putc(c);
}

int
cons_getc()
{
	return frontend_syscall(RAMP_SYSCALL_getch,0,0,0);
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
