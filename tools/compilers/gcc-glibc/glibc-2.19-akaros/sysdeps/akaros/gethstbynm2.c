#include <netdb.h>
#include <errno.h>
#include <assert.h>

struct hostent *gethostbyname2(const char *name, int af)
{
	static struct hostent h;
	static char buf[1024];
	int herrno_p, ret_r;
	struct hostent *ret;

	ret_r = gethostbyname2_r(name, af, &h, buf, sizeof(buf), &ret,
				 &herrno_p);
	if (ret_r) {
		/* _r method returns -ERROR on error.  not sure who wants it. */
		__set_errno(-ret_r);
		h_errno = herrno_p;
		return 0;
	}
	assert(ret == &h);
	return &h;
}
