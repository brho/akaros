#include <netdb.h>

void sethostent(int stayopen)
{
}
stub_warning(sethostent);

void endhostent(void)
{
}
stub_warning(endhostent);

int __gethostent_r(struct hostent *ret, char *buf, size_t buflen,
                   struct hostent **result, int *h_errnop)
{
	return 0;
}
weak_alias(__gethostent_r, gethostent_r);
stub_warning(gethostent_r);
