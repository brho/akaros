#include <pwd.h>

int __fgetpwent_r(FILE *fp, struct passwd *pwbuf, char *buf,
                  size_t buflen, struct passwd **pwbufp)
{
	return 0;
}
weak_alias(__fgetpwent_r, fgetpwent_r);
stub_warning(fgetpwent_r);
