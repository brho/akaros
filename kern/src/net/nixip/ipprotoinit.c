#include <vfs.h>
#include <kfs.h>
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

/* normally automatically generated on plan 9. Move to ldscripts soon. */
extern void tcpinit(struct fs*);
extern void udpinit(struct fs*);
extern void ipifcinit(struct fs*);
extern void icmpinit(struct fs*);
extern void icmp6init(struct fs*);
void (*ipprotoinit[])(struct fs*) = {
	tcpinit,
	udpinit,
	ipifcinit,
	icmpinit,
	icmp6init,
	NULL,
};
