#include <netdb.h>

int __gethostbyname_r(const char *name, struct hostent *ret, char *buf,
                      size_t buflen, struct hostent **result, int *h_errnop)
{
	return __gethostbyname2_r(name, AF_INET, ret, buf, buflen, result,
	                          h_errnop);
}
weak_alias(__gethostbyname_r, gethostbyname_r);
