#include <grp.h>

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups)
{
	return 0;
}
stub_warning(getgrouplist);

int initgroups(const char *user, gid_t group)
{
	return 0;
}
