#include <shadow.h>

int __getspent_r(struct spwd *spbuf,
                 char *buf, size_t buflen, struct spwd **spbufp)
{
	return 0;
}
weak_alias(__getspent_r, getspent_r);
stub_warning(getspent_r);
