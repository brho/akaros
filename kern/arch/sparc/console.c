#include <arch/frontend.h>
#include <pmap.h>

void
cons_init(void)
{
}

// `High'-level console I/O.  Used by readline and cprintf.

void
cputbuf(const char*COUNT(len) buf, int len)
{
	int32_t errno;
	frontend_syscall(0,RAMP_SYSCALL_write,1,PADDR((int32_t)buf),len,0,&errno);
}

// Low-level console I/O

inline void
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
	int32_t errno;
	int32_t ret = frontend_syscall(0,RAMP_SYSCALL_read,0,PADDR((int32_t)&ch),1,0,&errno);
	if(ch == 0x7F)
		ch = '\b';
	return ret <= 0 ? 0 : ch;
	//int ret = sys_nbgetch();
	//return ret < 0 ? 0 : ret;
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
