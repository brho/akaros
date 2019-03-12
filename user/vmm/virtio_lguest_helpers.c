/* Virtio helper functions from linux/tools/lguest/lguest.c
 *
 * Copyright (C) 1991-2016, the Linux Kernel authors
 * Copyright (c) 2016 Google Inc.
 *
 * Author:
 *  Rusty Russell <rusty@rustcorp.com.au>
 *  Michael Taufen <mtaufen@gmail.com>
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


#include <sys/eventfd.h>
#include <sys/uio.h>
#include <ros/common.h>
#include <ros/arch/membar.h>
#include <ros/arch/mmu.h>
#include <vmm/virtio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

// The purpose is to make sure the addresses provided by the guest are not
// outside the bounds of the guest's memory.
// Based on _check_pointer in Linux's lguest.c, which came with
// the following comment:
/*L:200
 * Device Handling.
 *
 * When the Guest gives us a buffer, it sends an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which checks it and exits with an error message
 * if something funny is going on:
 */
void *virtio_check_pointer(struct virtio_vq *vq, uint64_t addr,
                           uint32_t size, char *file, uint32_t line)
{
	// We check that the pointer + the size doesn't wrap around, and that
	// the pointer + the size isn't past the top of the guest's address
	// space.  UVPT is the top of the guest's address space, and is included
	// from ros/arch/mmu64.h via ros/arch/mmu.h.
	if ((addr + size) < addr || (addr + size) > UVPT)
		VIRTIO_DRI_ERRX(vq->vqdev,
			"Driver provided an invalid address or size (addr:0x%x sz:%u).\n"
			"  Location: %s:%d", addr, size, file, line);

	return (void *)addr;
}


// For traversing the chain of descriptors
// Based on next_desc Linux's lguest.c, which came with the following comment:
/*
 * Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or vq->vring.num if we're
 * at the end.
 */
static uint32_t next_desc(struct vring_desc *desc, uint32_t i, uint32_t max,
		struct virtio_vq *vq) // The vq is just for the error message.
{
	uint32_t next;

	// No more in the chain, so return max to signal that we reached the end
	if (!(desc[i].flags & VRING_DESC_F_NEXT))
		return max;

	// TODO: Closely audit the decision to use ACCESS_ONCE() instead of
	// wmb().
	//       If we encounter strange problems in the future, it might be
	//       worth changing it back to a wmb() to see what happens. For the
	//       record, the virtio console doesn't seem to have any issues
	//       running without the ACCESS_ONCE() or the wmb(). But they had a
	//       barrier here, so I'm hesitant to do anything less than
	//       ACCESS_ONCE().

	// Based on the original comment from lguest:
	// "Make sure compiler knows to grab that: we don't want it changing!",
	// it seems that they used a memory barrier after `next = desc[i].next`
	// to prevent the compiler from returning a `next` that differs from
	// the `next` that is compared to max. An ACCESS_ONCE() should suffice
	// to prevent this (see ros/common.h and
	// http://lwn.net/Articles/508991/).
	next = ACCESS_ONCE(desc[i].next);

	if (next >= max)
		VIRTIO_DRI_ERRX(vq->vqdev,
			"The next descriptor index in the chain provided by the driver is outside the bounds of the maximum allowed size of its queue.");

	return next;
}

// Adds descriptor chain to the used ring of the vq
// Based on add_used in Linux's lguest.c, which came with the following comment:
/*
 * After we've used one of their buffers, we tell the Guest about it.  Sometime
 * later we'll want to send them an interrupt using trigger_irq(); note that
 * wait_for_vq_desc() does that for us if it has to wait.
 */
void virtio_add_used_desc(struct virtio_vq *vq, uint32_t head, uint32_t len)
{
	// virtio-v1.0-cs04 s4.2.2.1 MMIO Device Register Layout
	if (!vq->qready)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The device may not process queues with QueueReady set to 0x0.\n"
			"  See virtio-v1.0-cs04 s4.2.2.1 MMIO Device Register Layout");

	// NOTE: len is the total length of the descriptor chain (in bytes)
	//       that was written to.
	//       So you should pass 0 if you didn't write anything, and pass
	//       the number of bytes you wrote otherwise.
	vq->vring.used->ring[vq->vring.used->idx % vq->vring.num].id = head;
	vq->vring.used->ring[vq->vring.used->idx % vq->vring.num].len = len;

	// virtio-v1.0-cs04 s2.4.8.2 The Virtqueue Used Ring
	// TODO: Take note, Barret is thinking about renaming our wmb() to
	// wwmb()
	// The device MUST set len prior to updating the used idx, hence wmb()
	wmb();
	vq->vring.used->idx++;
}

// Based on wait_for_vq_desc in Linux's'lguest.c, which came with
// the following comment:
/*
 * This looks in the virtqueue for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function waits if necessary, and returns the descriptor number found.
 */
uint32_t virtio_next_avail_vq_desc(struct virtio_vq *vq, struct iovec iov[],
                            uint32_t *olen, uint32_t *ilen)
{
// TODO: Need to make sure we don't overflow iov. Right now we're just kind of
//       trusting that whoever provided the iov made it at least as big as
//       qnum_max, but maybe we shouldn't be that trusting.
	uint32_t i, head, max;
	struct vring_desc *desc;
	eventfd_t event;

	while (vq->last_avail == vq->vring.avail->idx) {
		// We know the ring has updated when idx advances. We check ==
		// because idx is allowed to wrap around eventually.

		// NOTE: lguest kicks the guest with an irq before they wait on
		// the eventfd. Instead, I delegate that responsibility to the
		// queue service functions.

		// We're about to wait on the eventfd, so we need to tell the
		// guest that we want a notification when it adds new buffers
		// for us to process.
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;

		// If the guest added an available buffer while we were
		// unsetting the VRING_USED_F_NO_NOTIFY flag, we'll break out
		// here and process the new buffer.
		wrmb();
		if (vq->last_avail != vq->vring.avail->idx) {
			vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
			break;
		}

		if (eventfd_read(vq->eventfd, &event))
			VIRTIO_DEV_ERRX(vq->vqdev,
				"eventfd read failed while waiting for available descriptors\n");

		// We don't need the guest to notify us about new buffers unless
		// we're waiting on the eventfd, because we will detect the
		// updated vq->vring.avail->idx.
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
	}

	// NOTE: lguest is a bit cryptic about why they check for this.
	//       They just say, "Check it isn't doing very strange things with
	//       descriptor numbers."
	//       I added the reason I believe they do it in this comment and
	//       the below error message.
	// The guest can't have incremented idx more than vring.num times since
	// we last incremented vq->last_avail, because it would have run out of
	// places to put descriptors after incrementing exactly vring.num times
	// (prior to our next vq->last_avail++)
	if ((uint16_t)(vq->vring.avail->idx - vq->last_avail) > vq->vring.num)
		VIRTIO_DRI_ERRX(vq->vqdev,
		                "vq index increased from %u to %u, exceeded capacity %u\n",
				vq->last_avail, vq->vring.avail->idx,
				vq->vring.num);

	// lguest says here:
	/*
	 * Make sure we read the descriptor number *after* we read the ring
	 * update; don't let the cpu or compiler change the order.
	 */
	rmb();

	// Mod because it's a *ring*. lguest said:
	/*
	 * Grab the next descriptor number they're advertising, and increment
	 * the index we've seen.
	 */
	head = vq->vring.avail->ring[vq->last_avail % vq->vring.num];
	vq->last_avail++;

	if (head >= vq->vring.num)
		VIRTIO_DRI_ERRX(vq->vqdev,
			"The index of the head of the descriptor chain provided by the driver is after the end of the queue.");

	// Don't know how many output buffers or input buffers there are yet,
	// this depends on the descriptor chain.
	*olen = *ilen = 0;

	// Since vring.num is the size of the queue, max is the max descriptors
	// that should be in a descriptor chain. If we find more than that, the
	// driver is doing something wrong.
	max = vq->vring.num;
	desc = vq->vring.desc;
	i = head;

	// NOTE: (from lguest)
	/*
	 * We have to read the descriptor after we read the descriptor number,
	 * but there's a data dependency there so the CPU shouldn't reorder
	 * that: no rmb() required.
	 */
	// Mike: The descriptor number is stored in i; what lguest means is
	//       that data must flow from avail_ring to head to i before i
	//       is used to index into desc.

	do {
		// If it's an indirect descriptor, it points at a table of
		// descriptors provided by the guest driver. The descriptors in
		// that table are still chained, so we can ultimately handle
		// them the same way as direct descriptors.
		if (desc[i].flags & VRING_DESC_F_INDIRECT) {

			// virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors
			if (!(vq->vqdev->dri_feat &
			      (1<<VIRTIO_RING_F_INDIRECT_DESC)))
				VIRTIO_DRI_ERRX(vq->vqdev,
					"The driver must not set the INDIRECT flag on a descriptor if the INDIRECT_DESC feature was not negotiated.\n"
					"  See virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors");

			// NOTE: desc is only modified when we detect an
			// indirect descriptor, so our implementation works
			// whether there is an indirect descriptor at the very
			// beginning OR at the very end of the chain
			// (virtio-v1.0-cs04 s2.4.5.3.2 compliant)
			//
			// virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors
			if (desc != vq->vring.desc)
				VIRTIO_DRI_ERRX(vq->vqdev,
					"The driver must not set the INDIRECT flag on a descriptor within an indirect descriptor.\n"
					"  See virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors");

			// virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors
			if (desc[i].flags & VRING_DESC_F_NEXT)
				VIRTIO_DRI_ERRX(vq->vqdev,
					"The driver must not set both the INDIRECT and NEXT flags on a descriptor.\n"
					"  See virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors");

			// nonzero mod indicates wrong table size
			if (desc[i].len % sizeof(struct vring_desc))
				VIRTIO_DRI_ERRX(vq->vqdev,
					"The size of a vring descriptor does not evenly divide the length of the indirect table provided by the driver. Bad table size.");

			// NOTE: virtio-v1.0-cs04 s2.4.5.3.2 Indirect
			// Descriptors says that the device MUST ignore the
			// write-only flag in the descriptor that refers to an
			// indirect table. So we ignore.

			max = desc[i].len / sizeof(struct vring_desc);
			desc = virtio_check_pointer(vq, desc[i].addr,
						    desc[i].len, __FILE__,
						    __LINE__);

			// Now that desc is pointing at the table of indirect
			// descriptors, we set i to 0 so that we can start
			// walking that table
			i = 0;

			// virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors
			if (max > vq->vring.num) {
				VIRTIO_DRI_ERRX(vq->vqdev,
					"The driver must not create a descriptor chain longer than the queue size of the device.\n"
					"  See virtio-v1.0-cs04 s2.4.5.3.1 Indirect Descriptors");
			}
		}

		// Now build the scatterlist of buffers for the device to
		// process
		iov[*olen + *ilen].iov_len = desc[i].len;
		iov[*olen + *ilen].iov_base = virtio_check_pointer(vq,
								   desc[i].addr,
								   desc[i].len,
								   __FILE__,
								   __LINE__);

		if (desc[i].flags & VRING_DESC_F_WRITE) {
			// input descriptor, increment *ilen
			(*ilen)++;
		}
		else {
			// virtio-v1.0-cs04 s2.4.4.2 Message Framing
			if (*ilen) {
				VIRTIO_DRI_ERRX(vq->vqdev,
					"Device detected an output descriptor after an input descriptor. The driver must place any device-writeable descriptor elements after all device-readable descriptor elements.\n"
					"  See virtio-v1.0-cs04 s2.4.4.2 Message Framing");
			}

			(*olen)++;
		}

		// virtio-v1.0-cs04 s2.4.5.2 The Virtqueue Descriptor Table
		if (*olen + *ilen > max) {
			VIRTIO_DRI_ERRX(vq->vqdev,
				"The driver must ensure that there are no loops in the descriptor chain it provides! The combined length of readable and writeable buffers was greater than the number of elements in the queue.\n"
				"  See virtio-v1.0-cs04 s2.4.5.2 The Virtqueue Descriptor Table");
		}


	} while ((i = next_desc(desc, i, max, vq)) != max);

	return head;

}

// Based on check_virtqueue from lguest.c
// We call this when the driver writes 0x1 to QueueReady
void virtio_check_vring(struct virtio_vq *vq)
{
	// First make sure that the pointers on the vring are all valid:
	virtio_check_pointer(vq, (uint64_t)vq->vring.desc,
	                     sizeof(*vq->vring.desc) * vq->vring.num,
	                     __FILE__, __LINE__);
	virtio_check_pointer(vq, (uint64_t)vq->vring.avail,
	                     sizeof(*vq->vring.avail) * vq->vring.num,
	                     __FILE__, __LINE__);
	virtio_check_pointer(vq, (uint64_t)vq->vring.used,
	                     sizeof(*vq->vring.used) * vq->vring.num,
	                     __FILE__, __LINE__);


	// virtio-v1.0-cs04 s2.4.9.1 Virtqueue Notification Suppression
	if (vq->vring.used->flags != 0)
		VIRTIO_DRI_ERRX(vq->vqdev,
			"The driver must initialize the flags field of the used ring to 0 when allocating the used ring.\n"
			"  See virtio-v1.0-cs04 s2.4.9.1 Virtqueue Notification Suppression");
}
