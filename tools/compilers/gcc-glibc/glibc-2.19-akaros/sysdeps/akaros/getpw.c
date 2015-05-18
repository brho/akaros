#include <sys/types.h>
#include <pwd.h>

/* glibc has an implementation of this that might work */
int getpw(uid_t uid, char *buf)
{
	return 0;
}
stub_warning(getpw);
