#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <nixip.h>
#include <ndb.h>

#warning "setnetmtpt uses /9/net"
void
setnetmtpt(char *net, int n, char *x)
{
	if(x == NULL)
		x = "/9/net";

	if(*x == '/'){
		strncpy(net, x, n);
		net[n-1] = 0;
	} else {
		snprintf(net, n, "/9/net%s", x);
	}
}
