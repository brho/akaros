/* Virtio helper functions from linux/tools/lguest/lguest.c
 *
 * Copyright (C) 1991-2016, the Linux Kernel authors
 * Copyright (c) 2016 Google Inc.
 *
 * Author:
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Kyle Milka <kmilka@google.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * The code from lguest.c has been modified for Akaros.
 *
 * Original linux/tools/lguest/lguest.c:
 *   https://github.com/torvalds/linux/blob/v4.5/tools/lguest/lguest.c
 *   most recent hash on the file as of v4.5 tag:
 *     e523caa601f4a7c2fa1ecd040db921baf7453798
 */

#include <stdlib.h>
#include <unistd.h>
#include <parlib/stdio.h>
#include <fcntl.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_net.h>
#include <vmm/net.h>
#include <parlib/iovec.h>
#include <iplib/iplib.h>

#define VIRTIO_HEADER_SIZE	12

void virtio_net_set_mac(struct virtio_vq_dev *vqdev, uint8_t *guest_mac)
{
	memcpy(((struct virtio_net_config*)(vqdev->cfg))->mac, guest_mac,
	       ETH_ADDR_LEN);
	memcpy(((struct virtio_net_config*)(vqdev->cfg_d))->mac, guest_mac,
	       ETH_ADDR_LEN);
}

/* net_receiveq_fn receives packets for the guest through the virtio networking
 * device and the _vq virtio queue.
 */
void *net_receiveq_fn(void *_vq)
{
	struct virtio_vq *vq = _vq;
	uint32_t head;
	uint32_t olen, ilen;
	int num_read;
	struct iovec *iov;
	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;
	struct virtio_net_hdr_v1 *net_header;

	if (!vq)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"\n  %s:%d\n"
			"  Virtio device: (not sure which one): Error, device behavior.\n"
			"  The device must provide a valid virtio_vq as an argument to %s."
			, __FILE__, __LINE__, __func__);

	if (vq->qready == 0x0)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The service function for queue '%s' was launched before the driver set QueueReady to 0x1.",
			vq->name);

	iov = malloc(vq->qnum_max * sizeof(struct iovec));
	assert(iov != NULL);

	if (!dev->poke_guest) {
		free(iov);
		VIRTIO_DEV_ERRX(vq->vqdev,
		                "The 'poke_guest' function pointer was not set.");
	}

	for (;;) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);
		if (olen) {
			free(iov);
			VIRTIO_DRI_ERRX(vq->vqdev,
				"The driver placed a device-readable buffer in the net device's receiveq.\n"
				"  See virtio-v1.0-cs04 s5.3.6.1 Device Operation");
		}

		/* The virtio_net header comes first.  We'll assume they didn't
		 * break the header across IOVs, but earlier versions of Linux
		 * did break out the payload into the second IOV. */
		net_header = iov[0].iov_base;
		assert(iov[0].iov_len >= VIRTIO_HEADER_SIZE);
		iov_strip_bytes(iov, ilen, VIRTIO_HEADER_SIZE);

		num_read = vnet_receive_packet(iov, ilen);
		if (num_read < 0) {
			free(iov);
			VIRTIO_DEV_ERRX(vq->vqdev,
				"Encountered an error trying to read input from the ethernet device.");
		}

		/* See virtio spec virtio-v1.0-cs04 s5.1.6.3.2 Device
		 * Requirements: Setting Up Receive Buffers
		 *
		 * VIRTIO_NET_F_MRG_RXBUF is not currently negotiated.
		 * num_buffers will always be 1 if VIRTIO_NET_F_MRG_RXBUF is not
		 * negotiated.
		 */
		net_header->num_buffers = 1;
		net_header->flags = 0;
		net_header->gso_type = VIRTIO_NET_HDR_GSO_NONE;
		virtio_add_used_desc(vq, head, num_read + VIRTIO_HEADER_SIZE);

		virtio_mmio_set_vring_irq(dev);
		dev->poke_guest(dev->vec, dev->dest);
	}
	return 0;
}

/* net_transmitq_fn transmits packets from the guest through the virtio
 * networking device through the _vq virtio queue.
 */
void *net_transmitq_fn(void *_vq)
{
	struct virtio_vq *vq = _vq;
	uint32_t head;
	uint32_t olen, ilen;
	struct iovec *iov;
	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;
	void *stripped;

	iov = malloc(vq->qnum_max * sizeof(struct iovec));
	assert(iov != NULL);

	if (!dev->poke_guest) {
		free(iov);
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The 'poke_guest' function pointer was not set.");
	}

	for (;;) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);

		if (ilen) {
			free(iov);
			VIRTIO_DRI_ERRX(vq->vqdev,
			                "The driver placed a device-writeable buffer in the network device's transmitq.\n"
		                    "  See virtio-v1.0-cs04 s5.3.6.1 Device Operation");
		}

		/* Strip off the virtio header (the first 12 bytes), as it is
		 * not a part of the actual ethernet frame. */
		iov_strip_bytes(iov, olen, VIRTIO_HEADER_SIZE);
		vnet_transmit_packet(iov, olen);

		virtio_add_used_desc(vq, head, 0);

		virtio_mmio_set_vring_irq(dev);
		dev->poke_guest(dev->vec, dev->dest);
	}
	return 0;
}
