#include <errno.h>
#include <netinet/ether.h>
#include <netinet/if_ether.h>
#include <string.h>

int ether_ntohost(char *hostname, const struct ether_addr *addr)
{
	return 0;
}
stub_warning(ether_ntohost);
