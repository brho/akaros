#include <shadow.h>

/* glibc has a version of this that might work.  or we can ignore it */
int __ulckpwdf(void)
{
	return 0;
}
weak_alias(__ulckpwdf, ulckpwdf);
stub_warning(ulckpwdf);
