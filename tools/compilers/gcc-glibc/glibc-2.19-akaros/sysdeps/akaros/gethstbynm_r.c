#include <netdb.h>

int __gethostbyname_r(const char *name, struct hostent *ret, char *buf,
                      size_t buflen, struct hostent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__gethostbyname_r, gethostbyname_r);
stub_warning(gethostbyname_r);
