#include <sys/types.h>
#include <pwd.h>

int __getpwuid_r(uid_t uid, struct passwd *pwd,
                 char *buf, size_t buflen, struct passwd **result)
{
	return 0;
}
weak_alias(__getpwuid_r, getpwuid_r);
stub_warning(getpwuid_r);
