#include <netdb.h>

int __getservbyport_r(int port, const char *proto, struct servent *result_buf,
                      char *buf, size_t buflen, struct servent **result)
{
	return 0;
}
weak_alias(__getservbyport_r, getservbyport_r);
stub_warning(getservbyport_r);
