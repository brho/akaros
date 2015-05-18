#include <netdb.h>

void setservent(int stayopen)
{
}
stub_warning(setservent);

void endservent(void)
{
}
stub_warning(endservent);
			  
int __getservent_r(struct servent *result_buf, char *buf, size_t buflen,
                   struct servent **result)
{
	return 0;
}
weak_alias(__getservent_r, getservent_r);
stub_warning(getservent_r);
