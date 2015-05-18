#include <rpc/netdb.h>

int getrpcbynumber_r(int number, struct rpcent *result_buf, char *buf,
                     size_t buflen, struct rpcent **result)
{
	return 0;
}
stub_warning(getrpcbynumber_r);
