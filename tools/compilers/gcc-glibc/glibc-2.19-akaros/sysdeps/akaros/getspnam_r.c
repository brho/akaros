#include <shadow.h>

int __getspnam_r(const char *name, struct spwd *spbuf,
                 char *buf, size_t buflen, struct spwd **spbufp)
{
	return 0;
}
weak_alias(__getspnam_r, getspnam_r);
stub_warning(getspnam_r);
