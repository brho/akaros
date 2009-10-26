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
	int i;
	for(i = 0; i < len; i++)
		cputchar(buf[i]);
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
	while(sys_nbputch(c));
}

int
cons_getc()
{
	int ret = sys_nbgetch();
	return ret < 0 ? 0 : ret;
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
