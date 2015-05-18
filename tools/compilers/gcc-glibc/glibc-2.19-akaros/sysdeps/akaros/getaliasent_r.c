#include <aliases.h>

void setaliasent(void)
{
}
stub_warning(setaliasent);

void endaliasent(void)
{
}
stub_warning(endaliasent);

int getaliasent_r(struct aliasent *result, char *buffer, size_t buflen,
                  struct aliasent **res)
{
	return 0;
}
stub_warning(getaliasent_r);
