/* Virtio helper functions from linux/tools/lguest/lguest.c
 *
 * Copyright (C) 1991-2016, the Linux Kernel authors
 *
 * Author:
 *  Rusty Russell <rusty@rustcorp.com.au>
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
 * Original linux/tools/lguest/lguest.c:
 *   https://github.com/torvalds/linux/blob/v4.5/tools/lguest/lguest.c
 *   most recent hash on the file as of v4.5 tag:
 *     e523caa601f4a7c2fa1ecd040db921baf7453798
 */


/*L:200
 * Device Handling.
 *
 * When the Guest gives us a buffer, it sends an array of addresses and sizes.
 * We need to make sure it's not trying to reach into the Launcher itself, so
 * we have a convenient routine which checks it and exits with an error message
 * if something funny is going on:
 */
static void *_check_pointer(struct device *d,
			    unsigned long addr, unsigned int size,
			    unsigned int line)
{
	/*
	 * Check if the requested address and size exceeds the allocated memory,
	 * or addr + size wraps around.
	 */
	if ((addr + size) > guest_limit || (addr + size) < addr)
		bad_driver(d, "%s:%i: Invalid address %#lx",
			   __FILE__, line, addr);
	/*
	 * We return a pointer for the caller's convenience, now we know it's
	 * safe to use.
	 */
	return from_guest_phys(addr);
}


/*
 * Each buffer in the virtqueues is actually a chain of descriptors.  This
 * function returns the next descriptor in the chain, or vq->vring.num if we're
 * at the end.
 */
static unsigned next_desc(struct device *d, struct vring_desc *desc,
			  unsigned int i, unsigned int max)
{
	unsigned int next;

	/* If this descriptor says it doesn't chain, we're done. */
	if (!(desc[i].flags & VRING_DESC_F_NEXT))
		return max;

	/* Check they're not leading us off end of descriptors. */
	next = desc[i].next;
	/* Make sure compiler knows to grab that: we don't want it changing! */
	wmb();

	if (next >= max)
		bad_driver(d, "Desc next is %u", next);

	return next;
}

/*
 * After we've used one of their buffers, we tell the Guest about it.  Sometime
 * later we'll want to send them an interrupt using trigger_irq(); note that
 * wait_for_vq_desc() does that for us if it has to wait.
 */
static void add_used(struct virtqueue *vq, unsigned int head, int len)
{
	struct vring_used_elem *used;

	/*
	 * The virtqueue contains a ring of used buffers.  Get a pointer to the
	 * next entry in that used ring.
	 */
	used = &vq->vring.used->ring[vq->vring.used->idx % vq->vring.num];
	used->id = head;
	used->len = len;
	/* Make sure buffer is written before we update index. */
	wmb();
	vq->vring.used->idx++;
	vq->pending_used++;
}

/*
 * This looks in the virtqueue for the first available buffer, and converts
 * it to an iovec for convenient access.  Since descriptors consist of some
 * number of output then some number of input descriptors, it's actually two
 * iovecs, but we pack them into one and note how many of each there were.
 *
 * This function waits if necessary, and returns the descriptor number found.
 */
static unsigned wait_for_vq_desc(struct virtqueue *vq,
				 struct iovec iov[],
				 unsigned int *out_num, unsigned int *in_num)
{
	unsigned int i, head, max;
	struct vring_desc *desc;
	u16 last_avail = lg_last_avail(vq);

	/*
	 * 2.4.7.1:
	 *
	 *   The driver MUST handle spurious interrupts from the device.
	 *
	 * That's why this is a while loop.
	 */

	/* There's nothing available? */
	while (last_avail == vq->vring.avail->idx) {
		u64 event;

		/*
		 * Since we're about to sleep, now is a good time to tell the
		 * Guest about what we've used up to now.
		 */
		trigger_irq(vq);

		/* OK, now we need to know about added descriptors. */
		vq->vring.used->flags &= ~VRING_USED_F_NO_NOTIFY;

		/*
		 * They could have slipped one in as we were doing that: make
		 * sure it's written, then check again.
		 */
		mb();
		if (last_avail != vq->vring.avail->idx) {
			vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
			break;
		}

		/* Nothing new?  Wait for eventfd to tell us they refilled. */
		if (read(vq->eventfd, &event, sizeof(event)) != sizeof(event))
			errx(1, "Event read failed?");

		/* We don't need to be notified again. */
		vq->vring.used->flags |= VRING_USED_F_NO_NOTIFY;
	}

	/* Check it isn't doing very strange things with descriptor numbers. */
	if ((u16)(vq->vring.avail->idx - last_avail) > vq->vring.num)
		bad_driver_vq(vq, "Guest moved used index from %u to %u",
			      last_avail, vq->vring.avail->idx);

	/*
	 * Make sure we read the descriptor number *after* we read the ring
	 * update; don't let the cpu or compiler change the order.
	 */
	rmb();

	/*
	 * Grab the next descriptor number they're advertising, and increment
	 * the index we've seen.
	 */
	head = vq->vring.avail->ring[last_avail % vq->vring.num];
	lg_last_avail(vq)++;

	/* If their number is silly, that's a fatal mistake. */
	if (head >= vq->vring.num)
		bad_driver_vq(vq, "Guest says index %u is available", head);

	/* When we start there are none of either input nor output. */
	*out_num = *in_num = 0;

	max = vq->vring.num;
	desc = vq->vring.desc;
	i = head;

	/*
	 * We have to read the descriptor after we read the descriptor number,
	 * but there's a data dependency there so the CPU shouldn't reorder
	 * that: no rmb() required.
	 */

	do {
		/*
		 * If this is an indirect entry, then this buffer contains a
		 * descriptor table which we handle as if it's any normal
		 * descriptor chain.
		 */
		if (desc[i].flags & VRING_DESC_F_INDIRECT) {
			/* 2.4.5.3.1:
			 *
			 *  The driver MUST NOT set the VIRTQ_DESC_F_INDIRECT
			 *  flag unless the VIRTIO_F_INDIRECT_DESC feature was
			 *  negotiated.
			 */
			if (!(vq->dev->features_accepted &
			      (1<<VIRTIO_RING_F_INDIRECT_DESC)))
				bad_driver_vq(vq, "vq indirect not negotiated");

			/*
			 * 2.4.5.3.1:
			 *
			 *   The driver MUST NOT set the VIRTQ_DESC_F_INDIRECT
			 *   flag within an indirect descriptor (ie. only one
			 *   table per descriptor).
			 */
			if (desc != vq->vring.desc)
				bad_driver_vq(vq, "Indirect within indirect");

			/*
			 * Proposed update VIRTIO-134 spells this out:
			 *
			 *   A driver MUST NOT set both VIRTQ_DESC_F_INDIRECT
			 *   and VIRTQ_DESC_F_NEXT in flags.
			 */
			if (desc[i].flags & VRING_DESC_F_NEXT)
				bad_driver_vq(vq, "indirect and next together");

			if (desc[i].len % sizeof(struct vring_desc))
				bad_driver_vq(vq,
					      "Invalid size for indirect table");
			/*
			 * 2.4.5.3.2:
			 *
			 *  The device MUST ignore the write-only flag
			 *  (flags&VIRTQ_DESC_F_WRITE) in the descriptor that
			 *  refers to an indirect table.
			 *
			 * We ignore it here: :)
			 */

			max = desc[i].len / sizeof(struct vring_desc);
			desc = check_pointer(vq->dev, desc[i].addr, desc[i].len);
			i = 0;

			/* 2.4.5.3.1:
			 *
			 *  A driver MUST NOT create a descriptor chain longer
			 *  than the Queue Size of the device.
			 */
			if (max > vq->pci_config.queue_size)
				bad_driver_vq(vq,
					      "indirect has too many entries");
		}

		/* Grab the first descriptor, and check it's OK. */
		iov[*out_num + *in_num].iov_len = desc[i].len;
		iov[*out_num + *in_num].iov_base
			= check_pointer(vq->dev, desc[i].addr, desc[i].len);
		/* If this is an input descriptor, increment that count. */
		if (desc[i].flags & VRING_DESC_F_WRITE)
			(*in_num)++;
		else {
			/*
			 * If it's an output descriptor, they're all supposed
			 * to come before any input descriptors.
			 */
			if (*in_num)
				bad_driver_vq(vq,
					      "Descriptor has out after in");
			(*out_num)++;
		}

		/* If we've got too many, that implies a descriptor loop. */
		if (*out_num + *in_num > max)
			bad_driver_vq(vq, "Looped descriptor");
	} while ((i = next_desc(vq->dev, desc, i, max)) != max);

	return head;
}

/*
 * 4.1.4.3.2:
 *
 *  The driver MUST configure the other virtqueue fields before
 *  enabling the virtqueue with queue_enable.
 *
 * When they enable the virtqueue, we check that their setup is valid.
 */
static void check_virtqueue(struct device *d, struct virtqueue *vq)
{
	/* Because lguest is 32 bit, all the descriptor high bits must be 0 */
	if (vq->pci_config.queue_desc_hi
	    || vq->pci_config.queue_avail_hi
	    || vq->pci_config.queue_used_hi)
		bad_driver_vq(vq, "invalid 64-bit queue address");

	/*
	 * 2.4.1:
	 *
	 *  The driver MUST ensure that the physical address of the first byte
	 *  of each virtqueue part is a multiple of the specified alignment
	 *  value in the above table.
	 */
	if (vq->pci_config.queue_desc_lo % 16
	    || vq->pci_config.queue_avail_lo % 2
	    || vq->pci_config.queue_used_lo % 4)
		bad_driver_vq(vq, "invalid alignment in queue addresses");

	/* Initialize the virtqueue and check they're all in range. */
	vq->vring.num = vq->pci_config.queue_size;
	vq->vring.desc = check_pointer(vq->dev,
				       vq->pci_config.queue_desc_lo,
				       sizeof(*vq->vring.desc) * vq->vring.num);
	vq->vring.avail = check_pointer(vq->dev,
					vq->pci_config.queue_avail_lo,
					sizeof(*vq->vring.avail)
					+ (sizeof(vq->vring.avail->ring[0])
					   * vq->vring.num));
	vq->vring.used = check_pointer(vq->dev,
				       vq->pci_config.queue_used_lo,
				       sizeof(*vq->vring.used)
				       + (sizeof(vq->vring.used->ring[0])
					  * vq->vring.num));

	/*
	 * 2.4.9.1:
	 *
	 *   The driver MUST initialize flags in the used ring to 0
	 *   when allocating the used ring.
	 */
	if (vq->vring.used->flags != 0)
		bad_driver_vq(vq, "invalid initial used.flags %#x",
			      vq->vring.used->flags);
}
