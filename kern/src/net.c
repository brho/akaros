/* Copyright (c) 2010 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch independent networking infrastructure */

/* Computes an IP checksum over buf.  The checksum is the one's-comp of the
 * one's complement 16 bit sum of the payload.  Anything above 16 bits gets
 * added back to the lower 16 bits, which is what the mask and add is doing.
 * Check out http://www.netfor2.com/checksum.html for more info.
 *
 * If you want to start with a sum from something else (like the UDP
 * pseudo-header), pass it in as init_sum. */

#include <net.h>
#include <stdio.h>

uint16_t __ip_checksum(void *buf, unsigned int len, uint32_t sum)
{
	/* Knock out 2 bytes at a time */
	while (len > 1) {
		/* Careful of endianness.  The packet is in network ordering */
		sum += ntohs(*((uint16_t*)buf));
		buf += sizeof(uint16_t);
		/* In case we get close to overflowing while summing. */
		if (sum & 0x80000000)
			sum = (sum & 0xFFFF) + (sum >> 16);
		len -= 2;
	}
	/* Handle the last byte, if any */
	if (len)
		sum += *(uint8_t*)buf;
	/* Add the top 16 bytes to the lower ones, til it is done */
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return ~sum;
}

/* Computes the checksum for the IP header.  We could write it in, but for now
 * we'll return the checksum (in host-ordering) and have the caller store the
 * value.  The retval is in host ordering. */
uint16_t ip_checksum(struct ip_hdr *ip_hdr)
{
	unsigned int ip_hdr_len = ip_hdr->hdr_len * sizeof(uint32_t);
	ip_hdr->checksum = 0;
	return __ip_checksum(ip_hdr, ip_hdr_len, 0);
}

/* Computes the checksum for the UDP header.  We could write it in, but for now
 * we'll return the checksum (in host-ordering) and have the caller store the
 * value.  Note that the UDP header needs info from the IP header (strictly
 * speaking, just the src and destination IPs).  */
uint16_t udp_checksum(struct ip_hdr *ip_hdr, struct udp_hdr *udp_hdr)
{
	/* Add up the info for the UDP pseudo-header */
	uint32_t udp_pseudosum = 0;
	uint16_t udp_len = ntohs(udp_hdr->length);
	udp_hdr->checksum = 0;
	udp_pseudosum += ntohs(ip_hdr->src_addr & 0xffff);
	udp_pseudosum += ntohs(ip_hdr->src_addr >> 16);
	udp_pseudosum += ntohs(ip_hdr->dst_addr & 0xffff);
	udp_pseudosum += ntohs(ip_hdr->dst_addr >> 16);
	udp_pseudosum += ip_hdr->protocol;
	udp_pseudosum += udp_len;
	return __ip_checksum(udp_hdr, udp_len, udp_pseudosum);
}
/* Print out a network packet in the same format as tcpdump, making it easier 
 * to compare */
void dumppacket(unsigned char *buff, size_t len)
{
	int i;
	for (i=0; i<len; i++) {
		if (i%16 == 0)
			printk("0x%x\t", i/16);
		printk("%02x", buff[i]);
		if (i%2 != 0)
			printk(" ");
		if ((i+1)%16 == 0)
			printk("\n");
	}
}
