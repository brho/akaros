#include <shadow.h>

int __sgetspent_r(const char *s, struct spwd *spbuf,
                  char *buf, size_t buflen, struct spwd **spbufp)
{
	return 0;
}
weak_alias(__sgetspent_r, sgetspent_r);
stub_warning(sgetspent_r);
