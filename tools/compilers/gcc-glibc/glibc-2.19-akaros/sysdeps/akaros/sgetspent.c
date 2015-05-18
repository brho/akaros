#include <shadow.h>

/* glibc has a reasonably complicated wrapper that calls the reent version */
struct spwd *sgetspent(const char *s)
{
	return 0;
}
stub_warning(sgetspent);
