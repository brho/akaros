#include <grp.h>

int getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen,
               struct group **result)
{
	return 0;
}
stub_warning(getgrnam_r);
