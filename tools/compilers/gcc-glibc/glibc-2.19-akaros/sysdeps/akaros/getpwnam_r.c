#include <sys/types.h>
#include <pwd.h>

int __getpwnam_r(const char *name, struct passwd *pwd,
                 char *buf, size_t buflen, struct passwd **result)
{
	return 0;
}
weak_alias(__getpwnam_r, getpwnam_r);
stub_warning(getpwnam_r);
