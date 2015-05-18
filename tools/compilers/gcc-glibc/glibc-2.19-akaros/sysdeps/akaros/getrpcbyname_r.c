#include <rpc/netdb.h>

int getrpcbyname_r(const char *name, struct rpcent *result_buf, char *buf,
                   size_t buflen, struct rpcent **result)
{
	return 0;
}
stub_warning(getrpcbyname_r);
