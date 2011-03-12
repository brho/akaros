#include <ros/common.h>
#include <assert.h>
#include <socket.h>
#include <bits/netinet.h>
#include <net.h>
#include <net/ip.h>
#include <net/udp.h>
#include <ros/errno.h>
#include <arch/nic_common.h>

/* statically configured next gateway */
const uint8_t GTWAY[6] = {0xda, 0x76, 0xe7, 0x4c, 0xca, 0x7e};

/* TODO: ip id unique for all ip packets? or is it unique for a flow? */
// can do atomic increment at a minimum
static uint16_t ip_id = 0;
struct in_addr global_ip = {IP_ADDR};

/* TODO: build arp table, and look up */
int eth_send(struct pbuf *p, struct in_addr *dest) {
	uint32_t bytes_sent; 
	if (pbuf_header(p, sizeof(struct ethernet_hdr)) != 0){
		warn("eth_send buffer ran out");
		/* unsuccessful, needs to allocate */	
		return -ENOBUFS;
	}

	struct ethernet_hdr *ethhdr = (struct ethernet_hdr *)p->payload; 
	// TODO: for now just forward to gateway
	memcpy(ethhdr->dst_mac, GTWAY, 6);
	memcpy(ethhdr->src_mac, device_mac, 6);
	ethhdr->eth_type = htons(IP_ETH_TYPE);
	/* The reason for not sending to send_nic for each pbuf in the chain
	 * is so that we can send from multi-buffer later.
	 */
	if (send_pbuf){
	#if ETH_PAD_SIZE
		pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
	#endif
		bytes_sent = send_pbuf(p);
		pbuf_header(p, ETH_PAD_SIZE); /* when do we deallocate p? */
		return bytes_sent;
	}
	else {
		warn("no pbuf send function \n");
		return -1;
	}
	/* is the address local , if no, search for MAC of the gateway and dest to gateway */
	/* if address is local, use arp etc */

}

/* while it would be nice to write a generic send_pbuf it is impossible to do so in
 * efficiently.
 */
/* Assume no ip options */
int ip_output(struct pbuf *p, struct in_addr *src, struct in_addr *dest, uint8_t proto) {
	struct pbuf *q;
	struct ip_hdr *iphdr; 	
	/* TODO: Check for IP_HDRINCL */
	if (dest->s_addr == IP_HDRINCL) {
		/*send right away since */
		warn("header included in the ip packets");
		return -1;
	}
	if (pbuf_header(p, IP_HDR_SZ)) {
		warn("buffer ran out");
		/* unsuccessful, needs to allocate */	
		return -ENOBUFS;
	}
	iphdr = (struct ip_hdr *) p->payload;

	/* successful */
	iphdr->version = IPPROTO_IPV4;
	/* assume no IP options */
	iphdr->hdr_len = IP_HDR_SZ >> 2;
	iphdr->tos = 0;
	iphdr->packet_len = htons(p->tot_len);
	// TODO: NET_LOCK
	iphdr->id = htons (ip_id); // 1
	ip_id++;
	iphdr->flags_frags = htons(0); // 4000  may fragment
	iphdr->protocol = proto;
	iphdr->ttl = htons(DEFAULT_TTL);
	/* Eventually if we support more than one device this may change */
	printk("src ip %x, dest ip %x \n", src->s_addr, dest->s_addr);
	iphdr->src_addr = htonl(src->s_addr);
	iphdr->dst_addr = (dest->s_addr);
	/* force hardware checksum
	 * TODO: provide option to do both hardware/software checksum
	 */
	/* Since the IP header is set already, we can compute the checksum. */
	/* TODO: Use the card to calculate the checksum */
	iphdr->checksum = htons(ip_checksum(iphdr)); //7ab6
	if (p->tot_len > DEFAULT_MTU) /*MAX MTU? header included */
		return -1;//ip_frag(p, dest);
	else
		return eth_send(p, dest);
}


