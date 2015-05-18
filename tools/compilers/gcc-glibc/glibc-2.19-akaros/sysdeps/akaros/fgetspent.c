#include <shadow.h>

/* glibc has a reasonably complicated wrapper that calls the reent version */
struct spwd *fgetspent(FILE *fp)
{
	return 0;
}
stub_warning(fgetspent);
