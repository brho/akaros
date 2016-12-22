/*
 * Virtio platform device driver
 *
 * Copyright 2011, ARM Ltd.
 *
 * Based on Virtio PCI driver by Anthony Liguori, copyright IBM Corp. 2007
 *
 * This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#pragma once

/*
 * Control registers
 */

/* Magic value ("virt" string) - Read Only */
#define VIRTIO_MMIO_MAGIC_VALUE		0x000

/* Virtio device version - Read Only */
#define VIRTIO_MMIO_VERSION		0x004

/* Virtio device ID - Read Only */
#define VIRTIO_MMIO_DEVICE_ID		0x008

/* Virtio vendor ID - Read Only */
#define VIRTIO_MMIO_VENDOR_ID		0x00c

/* Bitmask of the features supported by the device (host)
 * (32 bits per set) - Read Only */
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010

/* Device (host) features set selector - Write Only */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL	0x014

/* Bitmask of features activated by the driver (guest)
 * (32 bits per set) - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020

/* Activated features set selector - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL	0x024


#ifndef VIRTIO_MMIO_NO_LEGACY /* LEGACY DEVICES ONLY! */

/* Guest's memory page size in bytes - Write Only */
#define VIRTIO_MMIO_GUEST_PAGE_SIZE	0x028

#endif


/* Queue selector - Write Only */
#define VIRTIO_MMIO_QUEUE_SEL		0x030

/* Maximum size of the currently selected queue - Read Only */
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034

/* Queue size for the currently selected queue - Write Only */
#define VIRTIO_MMIO_QUEUE_NUM		0x038


#ifndef VIRTIO_MMIO_NO_LEGACY /* LEGACY DEVICES ONLY! */

/* Used Ring alignment for the currently selected queue - Write Only */
#define VIRTIO_MMIO_QUEUE_ALIGN		0x03c

/* Guest's PFN for the currently selected queue - Read Write */
#define VIRTIO_MMIO_QUEUE_PFN		0x040

#endif


/* Ready bit for the currently selected queue - Read Write */
#define VIRTIO_MMIO_QUEUE_READY		0x044

/* Queue notifier - Write Only */
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050

/* Interrupt status - Read Only */
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060

/* Interrupt acknowledge - Write Only */
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064

/* Device status register - Read Write */
#define VIRTIO_MMIO_STATUS		0x070

/* Selected queue's Descriptor Table address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084

/* Selected queue's Available Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094

/* Selected queue's Used Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4

/* Configuration atomicity value */
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc

/* The config space is defined by each driver as
 * the per-driver configuration space - Read Write */
#define VIRTIO_MMIO_CONFIG		0x100



/*
 * Interrupt flags (re: interrupt status & acknowledge registers)
 */

#define VIRTIO_MMIO_INT_VRING		(1 << 0)
#define VIRTIO_MMIO_INT_CONFIG		(1 << 1)

// Code below this line was added for Akaros and is released
// under the following copyrights and license:

/* Virtio MMIO bindings
 *
 * Copyright (c) 2011 Linaro Limited
 * Copyright (C) 1991-2016, the Linux Kernel authors
 * Copyright (c) 2016 Google Inc.
 *
 * Author:
 *  Peter Maydell <peter.maydell@linaro.org>
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
 * Akaros's virtio_mmio (items in this file following this notice)
 * is inspired by QEMU's virtio-mmio.c and Linux's lguest.c.
 * Both of QEMU's virtio-mmio.c and Linux's lguest.c are released under the
 * GNU General Public License version 2 or later.
 * Their original files were heavily modified for Akaros.
 *
 * Original linux/tools/lguest/lguest.c:
 *   https://github.com/torvalds/linux/blob/v4.5/tools/lguest/lguest.c
 *   most recent hash on the file as of v4.5 tag:
 *     e523caa601f4a7c2fa1ecd040db921baf7453798
 *
 * Original virtio-mmio.c:
 *   https://github.com/qemu/qemu/blob/v2.5.0/hw/virtio/virtio-mmio.c
 *   most recent hash on the file as of v2.5.0 tag:
 *     ab223c9518e8c7eb542ef3133de1a34475b69790
 */

#include <stdint.h>
#include <vmm/virtio.h>

// The virtio mmio transport device. Wraps a virtio_vq_dev.
struct virtio_mmio_dev {
	// The base address of the virtio mmio device
	// we save the same value here as we report to guest via kernel cmd line
	uint64_t addr;

	// Reads from vqdev.dev_feat are performed starting at bit 32 * dev_feat_sel
	uint32_t dev_feat_sel;

	// Writes to vqdev.dri_feat are performed starting at bit 32 * dri_feat_sel
	uint32_t dri_feat_sel;

	// Reads and writes to queue-specific registers target vqdev->vqs[qsel]
	uint32_t qsel;

	// Interrupt status register
	uint32_t isr;

	// This utility function will be called when the device needs to interrupt
	// the guest. You can have it do whatever you want, but it is required.
	void (*poke_guest)(uint8_t vec, uint32_t dest);

	// Status flags for the device
	uint8_t status;

	// ConfigGeneration, used to check that access to device-specific
	// configuration space was atomic
	uint32_t cfg_gen;

	// The generic vq device contained by this mmio transport
	struct virtio_vq_dev *vqdev;

	// The specific irq number for this device.
	uint64_t irq;

	// Actual vector the device will inject.
	uint8_t vec;

	// Destination the interrupt is routed to.
	uint32_t dest;
};

// Sets the VIRTIO_MMIO_INT_VRING bit in the interrupt status
// register for the device
void virtio_mmio_set_vring_irq(struct virtio_mmio_dev *mmio_dev);

// Sets the VIRTIO_MMIO_INT_CONFIG bit in the interrupt status
// register for the device
void virtio_mmio_set_cfg_irq(struct virtio_mmio_dev *mmio_dev);

// virtio_mmio_rd and virtio_mmio_wr:
// Used to read and write to the mmio device registers.
// - gpa is the guest physical address that the driver tried to write to.
//   It is used to calculate the offset from the mmio device's base address,
//   and thus the target register of the access operation.
// - size is the width of the access operation in bytes.
uint32_t virtio_mmio_rd(struct virtual_machine *unused_vm,
                        struct virtio_mmio_dev *mmio_dev,
                        uint64_t gpa, uint8_t size);
void     virtio_mmio_wr(struct virtual_machine *vm,
                        struct virtio_mmio_dev *mmio_dev,
                        uint64_t gpa, uint8_t size, uint32_t *value);
