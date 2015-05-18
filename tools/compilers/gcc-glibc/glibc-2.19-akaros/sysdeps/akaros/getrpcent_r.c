#include <netdb.h>

void setrpcent(int stayopen)
{
}
stub_warning(setrpcent);

void endrpcent(void)
{
}
stub_warning(endrpcent);


int getrpcent_r(struct rpcent *result_buf, char *buf, size_t buflen,
                struct rpcent **result)
{
	return 0;
}
stub_warning(getrpcent_r);
