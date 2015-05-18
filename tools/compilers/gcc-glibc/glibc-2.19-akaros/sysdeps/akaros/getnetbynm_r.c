#include <netdb.h>

int __getnetbyname_r(const char *name, struct netent *result_buf, char *buf,
                     size_t buflen, struct netent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__getnetbyname_r, getnetbyname_r);
stub_warning(getnetbyname_r);
