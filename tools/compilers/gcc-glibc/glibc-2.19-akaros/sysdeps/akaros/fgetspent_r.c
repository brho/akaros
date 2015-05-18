#include <shadow.h>

int __fgetspent_r(FILE *fp, struct spwd *spbuf,
                  char *buf, size_t buflen, struct spwd **spbufp)
{
	return 0;
}
weak_alias(__fgetspent_r, fgetspent_r);
stub_warning(fgetspent_r);
