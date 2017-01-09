/* Copyright (c) 2016 Google Inc
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Networking for VMMs. */

#pragma once

#include <sys/uio.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_net.h>

/***** Global control variables */

/* Mirror virtio_net traffic to #srv/snoop-PID, default off. */
extern bool vnet_snoop;

/* Style of IP addressing, default off (qemu)
 *
 * For qemu-style networking:
 * 		guest_ip = 10.0.2.15, mask = 255.255.255.0, router = 10.0.2.2.
 * For real-addr networking:
 * 		guest_ip = host_v4, mask = host_mask, router = host_router
 * In either case, the guest sees the *real* DNS server. */
extern bool vnet_real_ip_addrs;

/* Have "notify 9" print NAT mappings to stderr.  Default off. */
extern bool vnet_map_diagnostics;

/* Timeout controls when we drop NAT mappings.  The value is a minimum, in
 * seconds.  Max is 2 * timeout.  E[X] is 1.5 * timeout.  Default 200.*/
extern unsigned long vnet_nat_timeout;


/***** Functional interface */

/* Control variables must be set before calling vnet_init() */
void vnet_init(struct virtual_machine *vm, struct virtio_vq_dev *vqdev);
void vnet_port_forward(char *protocol, char *host_port, char *guest_port);


/***** Glue between virtio and NAT */
int vnet_transmit_packet(struct iovec *iov, int iovcnt);
int vnet_receive_packet(struct iovec *iov, int iovcnt);
