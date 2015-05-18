#include <netdb.h>

int __getprotobynumber_r(int proto, struct protoent *result_buf, char *buf,
                         size_t buflen, struct protoent **result)
{
	return 0;
}
weak_alias(__getprotobynumber_r, getprotobynumber_r);
stub_warning(getprotobynumber_r);
