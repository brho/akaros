/**
 * Contains shamelessly stolen code from BSD & lwip, both have
 * BSD-style licenses
 *
 */
#include <ros/common.h>
#include <string.h>
#include <kmalloc.h>
#include <socket.h>
#include <net.h>
#include <sys/queue.h> //TODO: use sys/queue.h rather than implementing
#include <atomic.h>

#include <bits/netinet.h>
#include <net/ip.h>
#include <net/udp.h>
#include <slab.h>
#include <socket.h>

struct udp_pcb *udp_pcbs;
uint16_t udp_port_num = SOCKET_PORT_START;


struct udp_pcb* udp_new(void){
	struct udp_pcb *pcb = kmem_cache_alloc(udp_pcb_kcache, 0);
    // if pcb is only tracking ttl, then no need!
	if (pcb!= NULL){
		pcb->ttl = UDP_TTL;
    	memset(pcb, 0, sizeof(struct udp_pcb));
	}
	return pcb;
}

int udp_send(struct udp_pcb *pcb, struct pbuf *p)
{
  /* send to the packet using remote ip and port stored in the pcb */
  // rip and rport should be in socket not pcb?
  return udp_sendto(pcb, p, &pcb->remote_ip, pcb->remote_port);
}


int udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                    struct in_addr *dst_ip, uint16_t dst_port){
    // we now have one netif to send to, otherwise we need to route
    // ip_route();
    struct udp_hdr *udphdr;
    struct pbuf *q;
		/* TODO: utility to peform inet_ntop and 	inet_pton for debugging*/
		printd("udp_sendto ip %x, port %d\n", dst_ip->s_addr, dst_port); 
    // broadcast?
    if (pcb->local_port == 0) {
				/* if the PCB not bound to a port, bind it and give local ip */
        if (udp_bind(pcb, &pcb->local_ip, pcb->local_port)!=0)
					warn("udp binding failed \n");
    }
    if (pbuf_header(p, UDP_HLEN)){ // we could probably save this check for block.
        // CHECK: should allocate enough for the other headers too
        q = pbuf_alloc(PBUF_IP, UDP_HLEN, PBUF_RAM);
        if (q == NULL)
           panic("out of memory");
        // if the original packet is not empty
        if (p->tot_len !=0) {
            pbuf_chain(q,p);
            // check if it is chained properly ..
        }
    } else {
				/* Successfully padded the header*/
				q = p;
    }
    udphdr = (struct udp_hdr *) q->payload;
		printd("src port %x, dst port %x \n, length %x ", pcb->local_port, dst_port, q->tot_len);
    udphdr->src_port = htons(pcb->local_port);
    udphdr->dst_port = (dst_port);
    udphdr->checksum = 0x0000; //either use brho's checksum or use cards' capabilities
    udphdr->length = htons(q->tot_len); // 630
		// ip_output(q, src_ip, dst_ip, pcb->ttl, pcb->tos, IP_PROTO_UDP);
		ip_output(q, &global_ip, dst_ip, IPPROTO_UDP);

    return 0;

    // generate checksum if we need it.. check net.c
    //src_ip = GLOBAL_IP;
    // ip_output(blah);

    // check if there is space to operate in place (likely not)
    // allocate additional pbuf
    // chain the two bufs together
    // add udp headers
    // call ip layer

    // checksum calculation?
}
/* TODO: use the real queues we have implemented... */
int udp_bind(struct udp_pcb *pcb, struct in_addr *ip, uint16_t port){ 
    int rebind = pcb->local_port;
    struct udp_pcb *ipcb;
		assert(pcb);
		/* trying to assign port */
    if (port != 0)
        pcb->local_port = port;

    /* no lock needed since we are just traversing/reading */
    /* Check for double bind and rebind of the same pcb */
    for (ipcb = udp_pcbs; ipcb != NULL; ipcb = ipcb->next) {
        /* is this UDP PCB already on active list? */
        if (pcb == ipcb) {
            rebind = 1; //already on the list
        } else if (ipcb->local_port == port){
            warn("someone else is using the port %d\n" , port); 
            return -1;
        }
    }
    /* accept data for all interfaces */
    if (ip == NULL || (ip->s_addr == INADDR_ANY.s_addr))
        pcb->local_ip = INADDR_ANY;
    /* assign a port */
    if (port == 0) {
        port = SOCKET_PORT_START; 
        ipcb = udp_pcbs;
        while ((ipcb != NULL) && (port != SOCKET_PORT_END)) {
            if (ipcb->local_port == port) {
                /* port is already used by another udp_pcb */
                port++;
                /* restart scanning all udp pcbs */
                ipcb = udp_pcbs;
            } else {
                /* go on with next udp pcb */
                ipcb = ipcb->next;
            }
        }
        if (ipcb != NULL){
            warn("No more udp ports available!");
        }
    }
    if (rebind == 0) {
        /* place the PCB on the active list if not already there */
				pcb->next = udp_pcbs;
				udp_pcbs = pcb;
    }
		printk("local port bound to 0x%x \n", port);
    pcb->local_port = port;
    return 0;
}




