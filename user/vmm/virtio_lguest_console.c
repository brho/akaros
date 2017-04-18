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

#include <stdlib.h>
#include <err.h>
#include <sys/eventfd.h>
#include <sys/uio.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <parlib/stdio.h>

void *cons_receiveq_fn(void *_vq) // host -> guest
{
	struct virtio_vq *vq = _vq;
	uint32_t head;
	uint32_t olen, ilen;
	uint32_t i, j;
	int num_read;
	struct iovec *iov;
	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;

	if (!vq)
		errx(1,
			"\n  %s:%d\n"
			"  Virtio device: (not sure which one): Error, device behavior.\n"
			"  The device must provide a valid virtio_vq as an argument to %s."
			, __FILE__, __LINE__, __func__);

	// NOTE: The virtio_next_avail_vq_desc will not write more than
	//       vq->vring.num entries to iov, and the device implementation
	//       (virtio_mmio.c) will not allow the driver to set vq->vring.num to a
	//       value greater than QueueNumMax (vq->qnum_max), so you are safe as
	//       long as your iov is at least vq->qnum_max iovecs in size.
	iov = malloc(vq->qnum_max * sizeof(struct iovec));

	if (vq->qready == 0x0)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The service function for queue '%s' was launched before the driver set QueueReady to 0x1."
			, vq->name);

	// NOTE: This will block in 2 places:
	//       - reading from stdin
	//       - reading from eventfd in virtio_next_avail_vq_desc
	while (1) {
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);

		if (olen) {
			// virtio-v1.0-cs04 s5.3.6.1 Device Operation (console section)
			VIRTIO_DRI_ERRX(vq->vqdev,
				"The driver placed a device-readable buffer in the console device's receiveq.\n"
				"  See virtio-v1.0-cs04 s5.3.6.1 Device Operation");
		}

		// TODO: We may want to add some sort of console abort
		//       (e.g. type q and enter to quit)
		// readv from stdin as much as we can (to end of bufs or end of input)
		num_read = readv(0, iov, ilen);
		if (num_read < 0)
			VIRTIO_DEV_ERRX(vq->vqdev,
				"Encountered an error trying to read input from stdin (fd 0).");

		// You pass the number of bytes written to virtio_add_used_desc
		virtio_add_used_desc(vq, head, num_read);

		// Poke the guest however the mmio transport prefers
		// NOTE: assuming that the mmio transport was used for now.
		virtio_mmio_set_vring_irq(dev);
		if (dev->poke_guest)
			dev->poke_guest(dev->vec, dev->dest);
		else
			VIRTIO_DEV_ERRX(vq->vqdev,
				"The host MUST provide a way for device interrupts to be sent to the guest. The 'poke_guest' function pointer on the vq->vqdev->transport_dev (assumed to be a struct virtio_mmio_dev) was not set.");
	}
	free(iov);
	return 0;
}

void *cons_transmitq_fn(void *_vq) // guest -> host
{
	struct virtio_vq *vq = _vq;
	uint32_t head;
	uint32_t olen, ilen;
	uint32_t i, j;
	struct iovec *iov;
	struct virtio_mmio_dev *dev = vq->vqdev->transport_dev;

	if (!vq)
		errx(1,
			"\n  %s:%d\n"
			"  Virtio device: (not sure which one): Error, device behavior.\n"
			"  The device must provide a valid virtio_vq as an argument to %s."
			, __FILE__, __LINE__, __func__);

	// NOTE: The virtio_next_avail_vq_desc will not write more than
	//       vq->vring.num entries to iov, and the device implementation
	//       (virtio_mmio.c) will not allow the driver to set vq->vring.num to a
	//       value greater than QueueNumMax (vq->qnum_max), so you are safe as
	//       long as your iov is at least vq->qnum_max iovecs in size.
	iov = malloc(vq->qnum_max * sizeof(struct iovec));

	if (vq->qready == 0x0)
		VIRTIO_DEV_ERRX(vq->vqdev,
			"The service function for queue '%s' was launched before the driver set QueueReady to 0x1."
			, vq->name);

	while (1) {
		// Get the buffers:
		head = virtio_next_avail_vq_desc(vq, iov, &olen, &ilen);

		if (ilen) {
			// virtio-v1.0-cs04 s5.3.6.1 Device Operation (console section)
			VIRTIO_DRI_ERRX(vq->vqdev,
				"The driver placed a device-writeable buffer in the console device's transmitq.\n"
				"  See virtio-v1.0-cs04 s5.3.6.1 Device Operation");
		}
		// Process the buffers:
		for (i = 0; i < olen; ++i) {
			for (j = 0; j < iov[i].iov_len; ++j)
				printf("%c", ((char *)iov[i].iov_base)[j]);
		}
		fflush(stdout);

		// Add all the buffers to the used ring.
		// Pass 0 because we wrote nothing.
		virtio_add_used_desc(vq, head, 0);

		// Poke the guest however the mmio transport prefers
		// NOTE: assuming that the mmio transport was used for now
		virtio_mmio_set_vring_irq(dev);
		if (dev->poke_guest)
			dev->poke_guest(dev->vec, dev->dest);
		else
			VIRTIO_DEV_ERRX(vq->vqdev,
				"The host MUST provide a way for device interrupts to be sent to the guest. The 'poke_guest' function pointer on the vq->vqdev->transport_dev (assumed to be a struct virtio_mmio_dev) was not set.");
	}
	free(iov);
	return 0;
}
