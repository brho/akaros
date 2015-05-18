#include <grp.h>

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
               struct group **result)
{
	return 0;
}
stub_warning(getgrgid_r);
