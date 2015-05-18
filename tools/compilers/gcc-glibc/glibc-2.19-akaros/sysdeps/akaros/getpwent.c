#include <sys/types.h>
#include <pwd.h>

struct passwd *getpwent(void)
{
	return 0;
}
stub_warning(getpwent);

void setpwent(void)
{
}
stub_warning(setpwent);

void endpwent(void)
{
}
stub_warning(endpwent);
