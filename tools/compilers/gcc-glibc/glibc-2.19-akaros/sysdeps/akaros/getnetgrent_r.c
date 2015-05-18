#include <netdb.h>

int __getnetgrent_r(char **__restrict host, char **__restrict user,
                    char **__restrict domain, char *__restrict buf,
                    size_t buflen)
{
	return 0;
}
weak_alias(__getnetgrent_r, getnetgrent_r);
stub_warning(getnetgrent_r);

int innetgr(const char *netgroup, const char *host, const char *user,
            const char *domain)
{
	return 0;
}
stub_warning(innetgr);
libc_hidden_def(innetgr);
