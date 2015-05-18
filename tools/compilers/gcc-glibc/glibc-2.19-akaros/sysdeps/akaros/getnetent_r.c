#include <netdb.h>

void setnetent(int stayopen)
{
}
stub_warning(setnetent);

void endnetent(void)
{
}
stub_warning(endnetent);

int __getnetent_r(struct netent *result_buf, char *buf, size_t buflen,
                  struct netent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__getnetent_r, getnetent_r);
stub_warning(getnetent_r);
