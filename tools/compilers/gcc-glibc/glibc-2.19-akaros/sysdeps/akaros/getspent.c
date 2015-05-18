#include <shadow.h>

struct spwd *getspent(void)
{
	return 0;
}
stub_warning(getspent);

void setspent(void)
{
}
stub_warning(setspent);

void endspent(void)
{
}
stub_warning(endspent);
