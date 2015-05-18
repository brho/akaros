#include <netdb.h>

void setprotoent(int stayopen)
{
}
stub_warning(setprotoent);

void endprotoent(void)
{
}
stub_warning(endprotoent);

int __getprotoent_r(struct protoent *result_buf, char *buf, size_t buflen,
                    struct protoent **result)
{
	return 0;
}
weak_alias(__getprotoent_r, getprotoent_r);
stub_warning(getprotoent_r);
