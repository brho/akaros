/**
 * Ethernet Wrapper
 *  
 * Based on the skelton from the lwip source.
 * Ported by Paul Pearce
 */

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include "netif/etharp.h"
#include "netif/ppp_oe.h"



static void
low_level_init(struct netif *netif);

static err_t
low_level_output(struct netif *netif, struct pbuf *p);

static struct pbuf *low_level_input(struct netif *netif);

static void
ethernetif_input(struct netif *netif);

err_t
ethernetif_init(struct netif *netif);
