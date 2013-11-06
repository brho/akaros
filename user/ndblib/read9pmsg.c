#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <dir.h>
#include <fcall.h>
#include <ndb.h>

long
readn(int f, void *av, long n)
{
	char *a;
	long m, t;

	a = av;
	t = 0;
	while(t < n){
		m = read(f, a+t, n-t);
		if(m <= 0){
			if(t == 0)
				return m;
			break;
		}
		t += m;
	}
	return t;
}

int
read9pmsg(int fd, void *abuf, unsigned int n)
{
	int m, len;
	uint8_t *buf;

	buf = abuf;

	/* read count */
	m = readn(fd, buf, BIT32SZ);
	if(m != BIT32SZ){
		if(m < 0)
			return -1;
		return 0;
	}

	len = GBIT32(buf);
	if(len <= BIT32SZ || len > n){
#warning "implement werrstr in user mode"
		/*werrstr(*/fprintf(stderr,"bad length in 9P2000 message header");
		return -1;
	}
	len -= BIT32SZ;
	m = readn(fd, buf+BIT32SZ, len);
	if(m < len)
		return 0;
	return BIT32SZ+m;
}
