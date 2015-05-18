#include <shadow.h>

/* glibc has a reasonably complicated wrapper that calls the reent version */

int putspent(const struct spwd *p, FILE *fp)
{
	return 0;
}
stub_warning(putspent);
