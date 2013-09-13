#include <stdlib.h>
#include <stdio.h>
#include <parlib.h>
#include <unistd.h>
#include <signal.h>
#include "ip.h"

static uint8_t loopbacknet[IPaddrlen] = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0xff, 0xff,
	127, 0, 0, 0
};
static uint8_t loopbackmask[IPaddrlen] = {
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0xff, 0xff, 0xff,
	0xff, 0, 0, 0
};

// find first ip addr that isn't the friggin loopback address
// unless there are no others
int
myipaddr(uint8_t *ip, char *net)
{
	struct ipifc *nifc;
	struct iplifc *lifc;
	static struct ipifc *ifc;
	uint8_t mynet[IPaddrlen];

	ifc = readipifc(net, ifc, -1);
	for(nifc = ifc; nifc; nifc = nifc->next)
		for(lifc = nifc->lifc; lifc; lifc = lifc->next){
			maskip(lifc->ip, loopbackmask, mynet);
			if(ipcmp(mynet, loopbacknet) == 0){
				continue;
			}
			if(ipcmp(lifc->ip, IPnoaddr) != 0){
				ipmove(ip, lifc->ip);
				return 0;
			}
		}
	ipmove(ip, IPnoaddr);
	return -1;
}
