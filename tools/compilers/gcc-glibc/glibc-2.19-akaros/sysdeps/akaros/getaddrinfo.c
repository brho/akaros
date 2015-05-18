/* stub from posix getaddrinfo.c */

#include <errno.h>
#include <netdb.h>

int
getaddrinfo (const char *name, const char *service, const struct addrinfo *req,
	     struct addrinfo **pai)
{
  __set_errno (ENOSYS);
  return EAI_SYSTEM;
}
stub_warning (getaddrinfo)
libc_hidden_def (getaddrinfo)

void
freeaddrinfo (struct addrinfo *ai)
{
  /* Nothing.  */
}
stub_warning (freeaddrinfo)
libc_hidden_def (freeaddrinfo)
