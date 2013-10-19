#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

/* bsd extensions */
#include <sys/uio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

struct servent *getservbyport (int port, __const char *proto)
{
	char buf[32];

	snprintf(buf, sizeof buf, "%d", port);
	return getservbyname(buf, proto);
}
