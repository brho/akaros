#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <net/ip.h>

/* normally automatically generated on plan 9. Move to ldscripts soon. */
extern void tcpinit(struct Fs *);
extern void udpinit(struct Fs *);
extern void ipifcinit(struct Fs *);
extern void icmpinit(struct Fs *);
extern void icmp6init(struct Fs *);
void (*ipprotoinit[]) (struct Fs *) = {
tcpinit, udpinit, ipifcinit, icmpinit, icmp6init, NULL,};
