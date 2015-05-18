#include <netdb.h>

int __getservbyname_r(const char *name, const char *proto,
                      struct servent *result_buf, char *buf, size_t buflen,
                      struct servent **result)
{
	return 0;
}
weak_alias(__getservbyname_r, getservbyname_r);
stub_warning(getservbyname_r);
