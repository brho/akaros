#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include <lwip/tcpip.h>
#include <netif/ethernetif.h>

int network_init()
{
	printf("Starting up network stack....\n");

	/* Network interface variables */
	struct ip_addr ipaddr, netmask, gw;
	struct netif netif;
	/* Set network address variables */

	IP4_ADDR(&gw, 192,168,0,1);
	IP4_ADDR(&ipaddr, 192,168,0,2);
	IP4_ADDR(&netmask, 255,255,255,0);

	tcpip_init(NULL, NULL);

	netif_add(&netif, &ipaddr, &netmask, &gw, NULL, ethernetif_init, ethernet_input);
	/* ethhw_init() is user-defined */
	/* use ip_input instead of ethernet_input for non-ethernet hardware */
	/* (this function is assigned to netif.input and should be called by the hardware driver) */

	netif_set_default(&netif);
	netif_set_up(&netif);

	printf("Going into a while loop...\n");

	while(1);

}
