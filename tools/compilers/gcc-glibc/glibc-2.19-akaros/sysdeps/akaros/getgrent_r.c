#include <grp.h>

int getgrent_r(struct group *gbuf, char *buf, size_t buflen,
               struct group **gbufp)
{
	return 0;
}
stub_warning(getgrent_r);
