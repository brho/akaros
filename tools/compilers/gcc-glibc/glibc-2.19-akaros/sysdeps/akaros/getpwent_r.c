#include <pwd.h>

int __getpwent_r(struct passwd *pwbuf, char *buf,
                 size_t buflen, struct passwd **pwbufp)
{
	return 0;
}
weak_alias(__getpwent_r, getpwent_r);
stub_warning(getpwent_r);
