#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>

/* glibc has an implementation of this that might work */

int putpwent(const struct passwd *p, FILE *stream)
{
	return 0;
}
stub_warning(putpwent);
