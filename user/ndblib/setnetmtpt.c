#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

void
setnetmtpt(char *net, int n, char *x)
{
	if(x == NULL)
		x = "/net";

	if(*x == '/'){
		strncpy(net, x, n);
		net[n-1] = 0;
	} else {
		snprintf(net, n, "/net%s", x);
	}
}
