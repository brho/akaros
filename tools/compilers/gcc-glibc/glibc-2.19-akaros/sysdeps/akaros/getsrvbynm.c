#include <netdb.h>
#include <errno.h>
#include <assert.h>

struct servent *getservbyname(const char *name, const char *proto)
{
	static struct servent s;
	static char buf[1024];
	int ret_r;
	struct servent *ret;

	ret_r = getservbyname_r(name, proto, &s, buf, sizeof(buf), &ret);
	if (ret_r) {
		/* _r method returns -ERROR on error.  not sure who wants it. */
		__set_errno(-ret_r);
		return 0;
	}
	assert(ret == &s);
	return &s;
}
