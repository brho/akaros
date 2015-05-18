#include <grp.h>

void setgrent(void)
{
	return 0;
}
stub_warning(setgrent);

void endgrent(void)
{
}
stub_warning(endgrent);

struct group *getgrent(void)
{
	return 0;
}
stub_warning(getgrent);
