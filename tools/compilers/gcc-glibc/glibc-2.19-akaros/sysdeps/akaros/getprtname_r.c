#include <netdb.h>

int __getprotobyname_r(const char *name, struct protoent *result_buf, char *buf,
                       size_t buflen, struct protoent **result)
{
	return 0;
}
weak_alias(__getprotobyname_r, getprotobyname_r);
stub_warning(getprotobyname_r);
