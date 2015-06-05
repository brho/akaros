#include <netdb.h>

struct hostent *gethostbyname(const char *name)
{
	/* not sure if we are supposed to restrict to AF_INET for normal
	 * gethostbyname or not. */
	return gethostbyname2(name, AF_INET);
}
