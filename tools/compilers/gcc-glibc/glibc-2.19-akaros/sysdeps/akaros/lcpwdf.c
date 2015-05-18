#include <shadow.h>

/* glibc has a version of this that might work.  or we can ignore it */
int __lckpwdf(void)
{
	return 0;
}
weak_alias(__lckpwdf, lckpwdf);
stub_warning(lckpwdf);
