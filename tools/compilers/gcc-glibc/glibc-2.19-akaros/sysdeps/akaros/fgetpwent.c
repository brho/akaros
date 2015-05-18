#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>

/* glibc has an implementation of this that might work */
struct passwd *fgetpwent(FILE *stream)
{
	return 0;
}
stub_warning(fgetpwent);
