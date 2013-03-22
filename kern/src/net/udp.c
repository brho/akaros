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
#include <sys/queue.h>
#include <atomic.h>

#include <bits/netinet.h>
#include <net/ip.h>
#include <net/udp.h>
#include <slab.h>
#include <socket.h>
#include <debug.h>

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

typedef unsigned char u16;
typedef unsigned long u32;

u16 udp_sum_calc(u16 len_udp, u16 src_addr[],u16 dest_addr[],  int padding, u16 buff[])
{
u16 prot_udp=17;
u16 padd=0;
u16 word16;
u32 sum;
int i;
	
	// Find out if the length of data is even or odd number. If odd,
	// add a padding byte = 0 at the end of packet
	if ((padding&1)==1){
		padd=1;
		buff[len_udp]=0;
	}
	
	//initialize sum to zero
	sum=0;
	
	// make 16 bit words out of every two adjacent 8 bit words and 
	// calculate the sum of all 16 vit words
	for (i=0;i<len_udp+padd;i=i+2){
		word16 =((buff[i]<<8)&0xFF00)+(buff[i+1]&0xFF);
		sum = sum + (unsigned long)word16;
	}	
	// add the UDP pseudo header which contains the IP source and destinationn addresses
	for (i=0;i<4;i=i+2){
		word16 =((src_addr[i]<<8)&0xFF00)+(src_addr[i+1]&0xFF);
		sum=sum+word16;	
	}
	for (i=0;i<4;i=i+2){
		word16 =((dest_addr[i]<<8)&0xFF00)+(dest_addr[i+1]&0xFF);
		sum=sum+word16; 	
	}
	// the protocol number and the length of the UDP packet
	sum = sum + prot_udp + len_udp;

	// keep only the last 16 bits of the 32 bit calculated sum and add the carries
    	while (sum>>16)
		sum = (sum & 0xFFFF)+(sum >> 16);
		
	// Take the one's complement of sum
	sum = ~sum;

return ((u16) sum);
}

int udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                    struct in_addr *dst_ip, uint16_t dst_port){
    // we now have one netif to send to, otherwise we need to route
    // ip_route();
    struct udp_hdr *udphdr;
    struct pbuf *q;
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
		printd("src port %d, dst port %d \n, length %d ", pcb->local_port, ntohs(dst_port), q->tot_len);
    udphdr->src_port = htons(pcb->local_port);
    udphdr->dst_port = (dst_port);
    udphdr->length = htons(q->tot_len); 
		udphdr->checksum = 0; // just to be sure.
		// printd("checksum inet_chksum %x \n", udphdr->checksum);
		printd("params src addr %x, dst addr %x, length %x \n", LOCAL_IP_ADDR.s_addr, (dst_ip->s_addr), 
					  q->tot_len);

    udphdr->checksum = inet_chksum_pseudo(q, htonl(LOCAL_IP_ADDR.s_addr), dst_ip->s_addr,
											 IPPROTO_UDP, q->tot_len);
		printd ("method ours %x\n", udphdr->checksum);
		// 0x0000; //either use brho's checksum or use cards' capabilities
		ip_output(q, &LOCAL_IP_ADDR, dst_ip, pcb->ttl, pcb->tos, IPPROTO_UDP);
		// ip_output(q, &global_ip, dst_ip, IPPROTO_UDP);
    return 0;
}
/* TODO: use the real queues we have implemented... */
int udp_bind(struct udp_pcb *pcb, const struct in_addr *ip, uint16_t port){ 
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
		/* true right now */
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

/* port are in host order, ips are in network order */
/* Think: a pcb is here, if someone is waiting for a connection or the udp conn
 * has been established */
static struct udp_pcb* find_pcb(struct udp_pcb* list, uint16_t src_port, uint16_t dst_port,
								uint16_t srcip, uint16_t dstip) {
	struct udp_pcb* uncon_pcb = NULL;
	struct udp_pcb* pcb = NULL;
	uint8_t local_match = 0;

	for (pcb = list; pcb != NULL; pcb = pcb->next) {
		local_match = 0;
		if ((pcb->local_port == dst_port) 
			&& (pcb->local_ip.s_addr == dstip 
			|| ip_addr_isany(&pcb->local_ip))){
				local_match = 1;
        if ((uncon_pcb == NULL) && 
            ((pcb->flags & UDP_FLAGS_CONNECTED) == 0)) {
          /* the first unconnected matching PCB */
          uncon_pcb = pcb;
        }
		}

		if (local_match && (pcb->remote_port == src_port) &&
				(ip_addr_isany(&pcb->remote_ip) ||
				 pcb->remote_ip.s_addr == srcip))
			/* perfect match */
			return pcb;
	}
	return uncon_pcb;
}

#if 0 // not working yet
// need to have pbuf queue support
int udp_attach(struct pbuf *p, struct sock *socket) {
	// pretend the attaching of packet is succesful
	/*
	recv_q->last->next = p;
	recv_q->last=p->last
	*/ 
}

#endif 

/** Process an incoming UDP datagram. 
 * Given an incoming UDP datagram, this function finds the right PCB
 * which links to the right socket buffer, and attaches the datagram
 * to the right socket. 
 * If no appropriate PCB is found, the pbuf is freed.
 */ 

/** TODO: think about combining udp_input and ip_input together */
// TODO: figure out if we even need a PCB? or just socket buff. 
// TODO: test out looking up pcbs.. since matching function may fail

int udp_input(struct pbuf *p){
	struct udp_hdr *udphdr;

	struct udp_pcb *pcb, uncon_pcb;
	struct ip_hdr *iphdr;
	uint16_t src, dst;
	bool local_match = 0;
	iphdr = (struct ip_hdr *)p->payload;
	/* Move the header to where the udp header is */
	if (pbuf_header(p, -((int16_t)((iphdr->hdr_len) * 4)))) {
		warn("udp_input: Did not find a matching PCB for a udp packet\n");
		pbuf_free(p);
		return -1;
	}
	printk("start of udp %p\n", p->payload);
	udphdr = (struct udp_hdr *)p->payload;
	/* convert the src port and dst port to host order */
	src = ntohs(udphdr->src_port);
	dst = ntohs(udphdr->dst_port);
	pcb = find_pcb(udp_pcbs, src, dst, iphdr->src_addr, iphdr->dst_addr);
	/* TODO: Possibly adjust the pcb to the head of the queue? */
	/* TODO: Linux uses a set of hashtables to lookup PCBs 
	 * Look at __udp4_lib_lookup function in Linux kernel 2.6.21.1
	 */
	/* Anything that is not directed at this pcb should have been dropped */
	if (pcb == NULL){
		warn("udp_input: Did not find a matching PCB for a udp packet\n");
		pbuf_free(p);
		return -1;
	}

	/* checksum check */
  if (udphdr->checksum != 0) {
    if (inet_chksum_pseudo(p, (iphdr->src_addr), (iphdr->dst_addr), 
		                 IPPROTO_UDP, p->tot_len) != 0){
			warn("udp_input: UPD datagram discarded due to failed chksum!");
			pbuf_free(p);
			return -1;
    }
	}
  /* ignore SO_REUSE */
	if (pcb != NULL && pcb->pcbsock != NULL){
		/* For each in the pbuf chain, disconnect from the chain and add it to the
		 * recv_buff of the correct socket 
		 */ 
		struct socket *sock = pcb->pcbsock;
		attach_pbuf(p, &sock->recv_buff);
		struct kthread *kthread;
		int8_t irq_state = 0;
		/* First notify any blocking recv calls,
		 * then notify anyone who might be waiting in a select
		 */ 
		// multiple people might be waiting on the socket here..
		/* TODO: consider a helper with this and tcp.c */
		if (!sem_up_irqsave(&sock->sem, &irq_state)) {
			// wake up all waiters
			struct semaphore_entry *sentry, *sentry_tmp;
			spin_lock(&sock->waiter_lock);
			LIST_FOREACH_SAFE(sentry, &sock->waiters, link, sentry_tmp) {
				//should only wake up one waiter
				sem_up_irqsave(&sentry->sem, &irq_state);
				LIST_REMOVE(sentry, link);
				/* do not need to free since all the sentry are stack-based vars
				 * */
			}
			spin_unlock(&sock->waiter_lock);
		}
		// the attaching of pbuf should have increfed pbuf ref, so free is simply a decref
		pbuf_free(p);
	}
	return 0;
}
