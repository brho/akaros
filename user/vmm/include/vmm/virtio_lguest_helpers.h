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

#pragma once

// Validates memory regions provided by the guest's virtio driver
void *virtio_check_pointer(struct virtio_vq *vq, uint64_t addr,
                           uint32_t size, char *file, uint32_t line);

// Adds descriptor chain to the used ring of the vq
// Based on add_used in Linux's lguest.c
void virtio_add_used_desc(struct virtio_vq *vq, uint32_t head, uint32_t len);

// Waits for the next available descriptor chain and writes the addresses
// and sizes of the buffers it describes to an iovec to make them easy to use.
// Based on wait_for_vq_desc in Linux lguest.c
uint32_t virtio_next_avail_vq_desc(struct virtio_vq *vq, struct iovec iov[],
				   uint32_t *olen, uint32_t *ilen);

// After the driver tells us that a queue is ready for processing,
// we use this to validate the addresses on the vring it gave us.
void virtio_check_vring(struct virtio_vq *vq);
