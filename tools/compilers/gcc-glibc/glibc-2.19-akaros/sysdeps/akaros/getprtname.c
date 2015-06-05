#include <netdb.h>
#include <errno.h>
#include <assert.h>

struct protoent *getprotobyname(const char *name)
{
	static struct protoent r;
	static char buf[1024];
	int ret_r;
	struct protoent *ret;

	ret_r = getprotobyname_r(name, &r, buf, sizeof(buf), &ret);
	if (ret_r) {
		/* _r method returns -ERROR on error.  not sure who wants it. */
		__set_errno(-ret_r);
		return 0;
	}
	assert(ret == &r);
	return &r;
}
