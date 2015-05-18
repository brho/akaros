#include <netdb.h>

int __gethostbyname2_r(const char *name, int af, struct hostent *ret,
                       char *buf, size_t buflen,
                       struct hostent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__gethostbyname2_r, gethostbyname2_r);
stub_warning(gethostbyname2_r);
