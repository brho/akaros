#include <netdb.h>

int __gethostbyaddr_r(const void *addr, socklen_t len, int type,
                      struct hostent *ret, char *buf, size_t buflen,
                      struct hostent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__gethostbyaddr_r, gethostbyaddr_r);
stub_warning(gethostbyaddr_r);
