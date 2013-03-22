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
#include <arch/types.h>

#ifndef FOLD_U32T
#define FOLD_U32T(u)          (((u) >> 16) + ((u) & 0x0000ffffUL))
#endif

/* New version of ip_checksum, notice this version does not change it into 
 * network order. It is useful to keep it in host order for further processing
 */ 
uint16_t __ip_checksum(void *dataptr, unsigned int len, uint32_t sum)
{
  uint8_t *pb = (uint8_t *)dataptr;
  uint16_t *ps, t = 0;
  int odd = ((uintptr_t)pb & 1);

  /* Get aligned to uint16_t */
	// this means pb started on some weird address..
	// but in our world this should never happen.. since the payload should always be aligned..
	// right..

  if (odd && len > 0) {
		// change the second half of t to what pb is
		// and advance pb
    ((uint8_t *)&t)[1] = *pb++;
    len--;
  }

  /* Add the bulk of the data */
  ps = (uint16_t *)(void *)pb;
  while (len > 1) {
    sum += *ps++;
    len -= 2;
  }

  /* Consume left-over byte, if any */
  if (len > 0) {
    ((uint8_t *)&t)[0] = *(uint8_t *)ps;
  }

  /* Add end bytes */
  sum += t;

  /* Fold 32-bit sum to 16 bits
     calling this twice is propably faster than if statements... */
  sum = FOLD_U32T(sum);
  sum = FOLD_U32T(sum);

  /* Swap if alignment was odd */
  if (odd) {
    sum = byte_swap16(sum);
  }

  return (uint16_t)sum;
}

/* Computes the checksum for the IP header.  We could write it in, but for now
 * we'll return the checksum (in host-ordering) and have the caller store the
 * value.  The retval is in host ordering. */
uint16_t ip_checksum(struct ip_hdr *ip_hdr)
{
	unsigned int ip_hdr_len = ip_hdr->hdr_len * sizeof(uint32_t);
	return ~__ip_checksum(ip_hdr, ip_hdr_len, 0);
}

/* Computes the checksum for the UDP header.  We could write it in, but for now
 * we'll return the checksum (in host-ordering) and have the caller store the
 * value.  Note that the UDP header needs info from the IP header (strictly
 * speaking, just the src and destination IPs).  
 * LEGACY: only used for packet that are not in pbuf formats*/
uint16_t udp_checksum(struct ip_hdr *ip_hdr, struct udp_hdr *udp_hdr)
{
	/* Add up the info for the UDP pseudo-header */
	uint32_t udp_pseudosum = 0;
	uint16_t udp_len = ntohs(udp_hdr->length);
	udp_pseudosum += (ip_hdr->src_addr & 0xffff);
	udp_pseudosum += (ip_hdr->src_addr >> 16);
	udp_pseudosum += (ip_hdr->dst_addr & 0xffff);
	udp_pseudosum += (ip_hdr->dst_addr >> 16);
	udp_pseudosum += (ip_hdr->protocol);
	udp_pseudosum += udp_hdr->length;
	return ~(__ip_checksum(udp_hdr, udp_len, udp_pseudosum));
}

/* ip addresses need to be network order, protolen and proto are HO */
uint16_t inet_chksum_pseudo(struct pbuf *p, uint32_t src, uint32_t dest, uint8_t proto, uint16_t proto_len) {
  uint32_t acc;
  uint32_t addr;
  struct pbuf *q;
  uint8_t swapped;

  acc = 0;
  swapped = 0;
  /* iterate through all pbuf in chain */
  for(q = p; q != NULL; q = STAILQ_NEXT(q, next)) {
    acc += __ip_checksum(q->payload, q->len, 0);
    acc = FOLD_U32T(acc);
    if (q->len % 2 != 0) {
      swapped = 1 - swapped;
      acc = byte_swap16(acc);
    }
  }

  if (swapped) {
    acc = byte_swap16(acc);
  }

  addr = (src);
  acc += (addr & 0xffffUL);
  acc += ((addr >> 16) & 0xffffUL);
  addr = (dest);
  acc += (addr & 0xffffUL);
  acc += ((addr >> 16) & 0xffffUL);
  acc += (uint32_t)htons((uint16_t)proto);
  acc += (uint32_t)htons(proto_len);

  /* Fold 32-bit sum to 16 bits
     calling this twice is propably faster than if statements... */
  acc = FOLD_U32T(acc);
  acc = FOLD_U32T(acc);
  return (uint16_t)~(acc & 0xffffUL);
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
