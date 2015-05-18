#include <netdb.h>

int __getnetbyaddr_r(uint32_t net, int type, struct netent *result_buf,
                     char *buf, size_t buflen, struct netent **result,
                     int *h_errnop)
{
	return 0;
}
weak_alias(__getnetbyaddr_r, getnetbyaddr_r);
stub_warning(getnetbyaddr_r);
