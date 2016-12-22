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
 * Akaros's virtio_mmio (this file) is inspired by QEMU's virtio-mmio.c
 * and Linux's lguest.c.  Both of QEMU's virtio-mmio.c and Linux's
 * lguest.c are released under the GNU General Public License version 2
 * or later.  Their original files were heavily modified for Akaros.
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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <vmm/virtio_config.h>
#include <vmm/virtio_mmio.h>

#define VIRT_MAGIC 0x74726976 /* 'virt' */

#define VIRT_MMIO_VERSION 0x2

#define VIRT_MMIO_VENDOR 0x52414B41 /* 'AKAR' */

void virtio_mmio_set_vring_irq(struct virtio_mmio_dev *mmio_dev)
{
	mmio_dev->isr |= VIRTIO_MMIO_INT_VRING;
}

void virtio_mmio_set_cfg_irq(struct virtio_mmio_dev *mmio_dev)
{
	mmio_dev->isr |= VIRTIO_MMIO_INT_CONFIG;
}

static void virtio_mmio_reset_cfg(struct virtio_mmio_dev *mmio_dev)
{
	if (!mmio_dev->vqdev->cfg || mmio_dev->vqdev->cfg_sz == 0)
		VIRTIO_DEV_WARNX(mmio_dev->vqdev,
			"Attempt to reset the device-specific configuration space, but the device does not provide it. Generally, this region is required, so you should probably do something about that.");

	// If a default device-specific configuration is provided, copy that
	// into the device-specific configuration space. Otherwise, clear the
	// device-specific configuration space.
	if (mmio_dev->vqdev->cfg_d)
		memcpy(mmio_dev->vqdev->cfg, mmio_dev->vqdev->cfg_d,
			   mmio_dev->vqdev->cfg_sz);
	else
		memset(mmio_dev->vqdev->cfg, 0x0, mmio_dev->vqdev->cfg_sz);

	// Increment the ConfigGeneration, since the config space just got reset.
	// We can't simply set it to 0, because we must ensure that it changes when
	// the config space changes and it might currently be set to 0.
	mmio_dev->cfg_gen++;
}

// TODO: virtio_mmio_reset could use a careful audit. We have not yet
//       encountered a scenario where the driver resets the device
//       while lots of things are in-flight; thus far we have only seen
//       device resets prior to the first initialization sequence.
static void virtio_mmio_reset(struct virtio_mmio_dev *mmio_dev)
{
	int i;

	if (!mmio_dev->vqdev)
		return;

	fprintf(stderr, "virtio mmio device reset: %s\n", mmio_dev->vqdev->name);

	// Clear any driver-activated feature bits
	mmio_dev->vqdev->dri_feat = 0;

	// virtio-v1.0-cs04 s2.1.2 Device Status Field
	// The device MUST initialize device status to 0 upon reset
	mmio_dev->status = 0;

	// virtio-v1.0-cs04 s4.2.2.1 MMIO Device Register Layout
	// Upon reset, the device MUST clear all bits in InterruptStatus
	mmio_dev->isr = 0;

	// virtio-v1.0-cs04 s4.2.2.1 MMIO Device Register Layout
	// Upon reset, the device MUST clear...ready bits in the QueueReady
	// register for all queues in the device.
	for (i = 0; i < mmio_dev->vqdev->num_vqs; ++i) {
		if (mmio_dev->vqdev->vqs[i].srv_th) {
		// FIXME! PLEASE, FIXME!
		// TODO: For now we are going to make device resets an error
		//       once service threads exist on the queues. This is obviously
		//       not sustainable, because the driver needs to be able
		//       to reset the device after certain errors occur.
		//
		//       In the future, when we actually decide how we want
		//       to clean up the threads, the sequence might look
		//       something like this:
		//       1. Ask the queue's service thread to exit and wait
		//          for it to finish and exit.
		//       2. Once it has exited, close the queue's eventfd
		//          and set both the eventfd and srv_th fields to 0.
			VIRTIO_DEV_ERRX(mmio_dev->vqdev,
				"The driver reset the device after queue service threads had started running. This is NOT a restriction imposed by virtio! We just haven't implemented something that will kill service threads yet.");
		}

		mmio_dev->vqdev->vqs[i].qready = 0;
		mmio_dev->vqdev->vqs[i].last_avail = 0;
	}

	virtio_mmio_reset_cfg(mmio_dev);
}

uint32_t virtio_mmio_rd(struct virtual_machine *unused_vm,
                        struct virtio_mmio_dev *mmio_dev,
                        uint64_t gpa, uint8_t size)
{
	uint64_t offset = gpa - mmio_dev->addr;
	uint8_t *target; // target of read from device-specific config space
	const char *err; // returned err strings

	// Return 0 for all registers except the magic number,
	// the mmio version, and the device vendor when either
	// there is no vqdev or no vqs on the vqdev.
	if (!mmio_dev->vqdev || mmio_dev->vqdev->num_vqs == 0) {
		switch (offset) {
		case VIRTIO_MMIO_MAGIC_VALUE:
			return VIRT_MAGIC;
		case VIRTIO_MMIO_VERSION:
			return VIRT_MMIO_VERSION;
		case VIRTIO_MMIO_VENDOR_ID:
			return VIRT_MMIO_VENDOR;
		default:
			return 0;
		}
	}

	// virtio-v1.0-cs04 s4.2.3.1.1 Device Initialization (MMIO section)
	if (mmio_dev->vqdev->dev_id == 0
		&& offset != VIRTIO_MMIO_MAGIC_VALUE
		&& offset != VIRTIO_MMIO_VERSION
		&& offset != VIRTIO_MMIO_DEVICE_ID)
		VIRTIO_DRI_ERRX(mmio_dev->vqdev,
			"Attempt to read from a register not MagicValue, Version, or DeviceID on a device whose DeviceID is 0x0\n"
			"  See virtio-v1.0-cs04 s4.2.3.1.1 Device Initialization");

	// Now we know that the host provided a vqdev. As soon as the driver tries
	// to read the magic number, we know it's considering the device. This is
	// a great time to validate the features the host is providing. The host
	// must provide a valid combination of features, or we crash here
	// until the offered feature combination is made valid.
	if (offset == VIRTIO_MMIO_MAGIC_VALUE) {
		// NOTE: If you ever decide to change this to a warning instead of an
		//       error, you might want to return an invalid magic value here
		//       to tell the driver that it is poking at a bad device.
		err = virtio_validate_feat(mmio_dev->vqdev,
		                           mmio_dev->vqdev->dev_feat);
		if (err)
			VIRTIO_DEV_ERRX(mmio_dev->vqdev,
				"The feature combination offered by the device is not valid. This must be fixed before the device can be used.\n"
				"  Validation Error: %s", err);
	}


	// Warn if FAILED status bit is set.
	// virtio-v1.0-cs04 s2.1.1 Device Status Field
	if (mmio_dev->status & VIRTIO_CONFIG_S_FAILED)
		VIRTIO_DRI_WARNX(mmio_dev->vqdev,
			"The FAILED status bit is set. The driver should probably reset the device before continuing.\n"
			"  See virtio-v1.0-cs04 s2.1.1 Device Status Field");

	// TODO: I could only do a limited amount of testing on the device-
	//       specific config space, because I was limited to seeing what
	//       the guest driver for the console device would do. You may
	//       run into issues when you implement virtio-net, since that
	//       does more with the device-specific config.
	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;

		if (!mmio_dev->vqdev->cfg || mmio_dev->vqdev->cfg_sz == 0) {
			VIRTIO_DEV_ERRX(mmio_dev->vqdev,
				"Driver attempted to read the device-specific configuration space, but the device failed to provide it.");
		}

		// virtio-v1.0-cs04 s3.1.1 Device Initialization
		if (!(mmio_dev->status & VIRTIO_CONFIG_S_DRIVER)) {
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Driver attempted to read the device-specific configuration space before setting the DRIVER status bit.\n"
				"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
		}

		if ((offset + size) > mmio_dev->vqdev->cfg_sz
			|| (offset + size) < offset) {
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to read invalid offset of the device specific  configuration space, or (offset + read width) wrapped around.");
		}

		target = (uint8_t*)((uint64_t)mmio_dev->vqdev->cfg + offset);

		// TODO: Check that size matches the size of the field at offset
		//       for the given device? i.e. virtio_console_config.rows
		//       should only be accessible via a 16 bit read or write.
		//       I haven't done this yet, it will be a significant
		//       undertaking and maintenance commitment, because you
		//       will have to do it for every virtio device you
		//       want to use in the future.
		switch (size) {
			case 1:
				return *((uint8_t*)target);
			case 2:
				if ((uint64_t)target % 2 != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"The driver must use 16 bit aligned reads for reading from 16 bit values in the device-specific configuration space.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
				return *((uint16_t*)target);
			case 4:
				if ((uint64_t)target % 4 != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"The driver must use 32 bit aligned reads for reading from 32 or 64 bit values in the device-specific configuration space.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
				return *((uint32_t*)target);
			default:
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver must use 8, 16, or 32 bit wide and aligned reads for reading from the device-specific configuration space.\n"
					"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
		}
	}

	// virtio-v1.0-cs04 4.2.2.2 MMIO Device Register Layout
	if (size != 4 || (offset % 4) != 0) {
		VIRTIO_DRI_ERRX(mmio_dev->vqdev,
			"The driver must only use 32 bit wide and aligned reads for reading the control registers on the MMIO transport.\n"
			"  See virtio-v1.0-cs04 4.2.2.2 MMIO Device Register Layout");
	}

	// virtio-v1.0-cs04 Table 4.1
	switch (offset) {
		// Magic value
		// 0x74726976 (a Little Endian equivalent of the “virt” string).
		case VIRTIO_MMIO_MAGIC_VALUE:
			return VIRT_MAGIC;

		// Device version number
		// 0x2. Note: Legacy devices (see 4.2.4 Legacy interface) used 0x1.
		case VIRTIO_MMIO_VERSION:
			return VIRT_MMIO_VERSION;

		// Virtio Subsystem Device ID (see virtio-v1.0-cs04 sec. 5 for values)
		// Value 0x0 is used to define a system memory map with placeholder
		// devices at static, well known addresses.
		case VIRTIO_MMIO_DEVICE_ID:
			return mmio_dev->vqdev->dev_id;

		// Virtio Subsystem Vendor ID
		case VIRTIO_MMIO_VENDOR_ID:
			return VIRT_MMIO_VENDOR;

		// Flags representing features the device supports
		case VIRTIO_MMIO_DEVICE_FEATURES:
			if (!(mmio_dev->status & VIRTIO_CONFIG_S_DRIVER))
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				         "Attempt to read device features before setting the DRIVER status bit.\n"
				         "  See virtio-v1.0-cs04 s3.1.1 Device Initialization");

			// high 32 bits requested
			if (mmio_dev->dev_feat_sel)
				return mmio_dev->vqdev->dev_feat >> 32;
			return mmio_dev->vqdev->dev_feat; // low 32 bits requested

		// Maximum virtual queue size
		// Returns the maximum size (number of elements) of the queue the device
		// is ready to process or zero (0x0) if the queue is not available.
		// Applies to the queue selected by writing to QueueSel.
		case VIRTIO_MMIO_QUEUE_NUM_MAX:
		// TODO: Are there other cases that count as "queue not available"?
		// NOTE: !qready does not count as "queue not available".
			if (mmio_dev->qsel >= mmio_dev->vqdev->num_vqs)
				return 0;
			return mmio_dev->vqdev->vqs[mmio_dev->qsel].qnum_max;

		// Virtual queue ready bit
		// Applies to the queue selected by writing to QueueSel.
		case VIRTIO_MMIO_QUEUE_READY:
			if (mmio_dev->qsel >= mmio_dev->vqdev->num_vqs)
				return 0;
			return mmio_dev->vqdev->vqs[mmio_dev->qsel].qready;

		// Interrupt status
		// Bit mask of events that caused the device interrupt to be asserted.
		// bit 0: Used Ring Update
		// bit 1: Configuration Change
		case VIRTIO_MMIO_INTERRUPT_STATUS:
			return mmio_dev->isr;

		// Device status
		case VIRTIO_MMIO_STATUS:
			return mmio_dev->status;

		// Configuration atomicity value
		// Contains a version for the device-specific configuration space
		// The driver checks this version before and after accessing the config
		// space, and if the values don't match it repeats the access.
		case VIRTIO_MMIO_CONFIG_GENERATION:
			return mmio_dev->cfg_gen;

		// Write-only register offsets:
		case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		case VIRTIO_MMIO_DRIVER_FEATURES:
		case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		case VIRTIO_MMIO_QUEUE_SEL:
		case VIRTIO_MMIO_QUEUE_NUM:
		case VIRTIO_MMIO_QUEUE_NOTIFY:
		case VIRTIO_MMIO_INTERRUPT_ACK:
		case VIRTIO_MMIO_QUEUE_DESC_LOW:
		case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		case VIRTIO_MMIO_QUEUE_USED_LOW:
		case VIRTIO_MMIO_QUEUE_USED_HIGH:
			// Read of write-only register
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to read write-only device register offset 0x%x.",
				offset);
			return 0;
		default:
			// Bad register offset
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to read invalid device register offset 0x%x.",
				offset);
			return 0;
	}

	return 0;
}

void virtio_mmio_wr(struct virtual_machine *vm,
                    struct virtio_mmio_dev *mmio_dev, uint64_t gpa,
                    uint8_t size, uint32_t *value)
{
	uint64_t offset = gpa - mmio_dev->addr;
	struct virtio_vq *notified_queue;
	uint8_t *target; // target of write to device-specific config space
	void *temp_ptr; // for facilitating bitwise ops on pointers
	const char *err; // returned err strings

	if (!mmio_dev->vqdev) {
		// If there is no vqdev on the mmio_dev,
		// we just make all registers write-ignored.
		return;
	}

	// virtio-v1.0-cs04 s4.2.3.1.1 Device Initialization (MMIO)
	if (mmio_dev->vqdev->dev_id == 0)
		VIRTIO_DRI_ERRX(mmio_dev->vqdev,
			"Attempt to write to a device whose DeviceID is 0x0.\n"
			"  See virtio-v1.0-cs04 s4.2.3.1.1 Device Initialization");

	// Warn if FAILED and trying to do something that is definitely not a reset.
	// virtio-v1.0-cs04 s2.1.1 Device Status Field
	if (offset != VIRTIO_MMIO_STATUS
		&& (mmio_dev->status & VIRTIO_CONFIG_S_FAILED))
		VIRTIO_DRI_WARNX(mmio_dev->vqdev,
			"The FAILED status bit is set. The driver should probably reset the device before continuing.\n"
			"  See virtio-v1.0-cs04 s2.1.1 Device Status Field");

	// TODO: I could only do a limited amount of testing on the device-
	//       specific config space, because I was limited to seeing what
	//       the guest driver for the console device would do. You may
	//       run into issues when you implement virtio-net, since that
	//       does more with the device-specific config. (In fact, I don't think
	//       the guest driver ever even tried to write the device-specific
	//       config space for the console, so this section is entirely untested)
	if (offset >= VIRTIO_MMIO_CONFIG) {
		offset -= VIRTIO_MMIO_CONFIG;

		if (!mmio_dev->vqdev->cfg || mmio_dev->vqdev->cfg_sz == 0) {
			VIRTIO_DEV_ERRX(mmio_dev->vqdev,
				"Driver attempted to write to the device-specific configuration space, but the device failed to provide it.");
		}

		// virtio-v1.0-cs04 s3.1.1 Device Initialization
		if (!(mmio_dev->status & VIRTIO_CONFIG_S_FEATURES_OK)) {
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Driver attempted to write the device-specific configuration space before setting the FEATURES_OK status bit.\n"
				"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
		}

		if ((offset + size) > mmio_dev->vqdev->cfg_sz
			|| (offset + size) < offset) {
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to write invalid offset of the device specific configuration space, or (offset + write width) wrapped around.");
		}

		target = (uint8_t*)((uint64_t)mmio_dev->vqdev->cfg + offset);

		// TODO: Check that size matches the size of the field at offset
		//       for the given device? i.e. virtio_console_config.rows
		//       should only be accessible via a 16 bit read or write.
		//       I haven't done this yet, it will be a significant
		//       undertaking and maintenance commitment, because you
		//       will have to do it for every virtio device you
		//       want to use in the future.
		switch (size) {
			case 1:
				*((uint8_t*)target) = *((uint8_t*)value);
				break;
			case 2:
				if ((uint64_t)target % 2 != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"The driver must use 16 bit aligned writes for writing to 16 bit values in the device-specific configuration space.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
				*((uint16_t*)target) = *((uint16_t*)value);
				break;
			case 4:
				if ((uint64_t)target % 4 != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"The driver must use 32 bit aligned writes for writing to 32 or 64 bit values in the device-specific configuration space.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
				*((uint32_t*)target) = *((uint32_t*)value);
				break;
			default:
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver must use 8, 16, or 32 bit wide and aligned writes for writing to the device-specific configuration space.\n"
					"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout");
		}

		// Increment cfg_gen because the device-specific config changed
		mmio_dev->cfg_gen++;

		// Notify the driver that the device-specific config changed
		virtio_mmio_set_cfg_irq(mmio_dev);
		if (mmio_dev->poke_guest)
			mmio_dev->poke_guest(mmio_dev->vec, mmio_dev->dest);

		return;
	}

	// virtio-v1.0-cs04 4.2.2.2 MMIO Device Register Layout
	if (size != 4 || (offset % 4) != 0) {
		VIRTIO_DRI_ERRX(mmio_dev->vqdev,
			"The driver must only use 32 bit wide and aligned writes for writing the control registers on the MMIO transport.\n"
			"  See virtio-v1.0-cs04 4.2.2.2 MMIO Device Register Layout");
	}

	// virtio-v1.0-cs04 Table 4.1
	switch (offset) {

		// Device (host) features word selection.
		case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
			mmio_dev->dev_feat_sel = *value;
			break;

		// Device feature flags activated by the driver
		case VIRTIO_MMIO_DRIVER_FEATURES:
			// virtio-v1.0-cs04 s3.1.1 Device Initialization
			if (mmio_dev->status & VIRTIO_CONFIG_S_FEATURES_OK) {
				// NOTE: The spec just says the driver isn't allowed to accept
				//       NEW feature bits after setting FEATURES_OK. Although
				//       the language makes it seem like it might be fine to
				//       let the driver un-accept features after it sets
				//       FEATURES_OK, this would require very careful handling,
				//       so for now we just don't allow the driver to write to
				//       the DriverFeatures register after FEATURES_OK is set.
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver may not accept (i.e. activate) new feature bits  offered by the device after setting FEATURES_OK.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			} else if (mmio_dev->dri_feat_sel) {
				// clear high 32 bits
				mmio_dev->vqdev->dri_feat &= 0xffffffff;
				// write high 32 bits
				mmio_dev->vqdev->dri_feat |= ((uint64_t)(*value) << 32);
			} else {
				// clear low 32 bits
				mmio_dev->vqdev->dri_feat &= ((uint64_t)0xffffffff << 32);
				// write low 32 bits
				mmio_dev->vqdev->dri_feat |= *value;
			}
			break;

		// Activated (guest) features word selection
		case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
			mmio_dev->dri_feat_sel = *value;
			break;

		// Virtual queue index
		// Selects the virtual queue that QueueNumMax, QueueNum, QueueReady,
		// QueueDescLow, QueueDescHigh, QueueAvailLow, QueueAvailHigh,
		// QueueUsedLow and QueueUsedHigh apply to. The index number of the
		// first queue is zero (0x0).
		case VIRTIO_MMIO_QUEUE_SEL:
		// NOTE: We must allow the driver to write whatever they want to
		//       QueueSel, because QueueNumMax contians 0x0 for invalid
		//       QueueSel indices.
			mmio_dev->qsel = *value;
			break;

		// Virtual queue size
		// The queue size is the number of elements in the queue, thus in the
		// Descriptor Table, the Available Ring and the Used Ring. Writes
		// notify the device what size queue the driver will use.
		// This applies to the queue selected by writing to QueueSel.
		case VIRTIO_MMIO_QUEUE_NUM:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				// virtio-v1.0-cs04 4.2.2.2 MMIO Device Register Layout
				if (*value <= mmio_dev->vqdev->vqs[mmio_dev->qsel].qnum_max)
					mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.num = *value;
				else if ((*value != 0) && (*value & ((*value) - 1)))
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"The driver may only write powers of 2 to the QueueNum register.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");
				else
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to write value greater than QueueNumMax to QueueNum register.");
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueNum register for invalid QueueSel. QueueSel was %u, but the number of queues is %u.",
					mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Virtual queue ready bit
		// Writing one (0x1) to this register notifies the device that it can
		// execute requests from the virtual queue selected by QueueSel.
		case VIRTIO_MMIO_QUEUE_READY:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				// NOTE: For now, anything that is not a toggle between
				//       0x1 and 0x0 will bounce with no effect whatsoever.
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready == 0x0
					&& *value == 0x1) {
					// Driver is trying to write 0x1 QueueReady when the queue
					// is currently disabled (QueueReady is 0x0). We validate
					// the vring the driver provided, set up an eventfd for the
					// queue, set qready on the queue to 0x1, and then launch
					// the service thread for the queue.

					// Check that the host actually provided a service function
					if (!mmio_dev->vqdev->vqs[mmio_dev->qsel].srv_fn) {
						VIRTIO_DEV_ERRX(mmio_dev->vqdev,
							"The host must provide a service function for each queue on the device before the driver writes 0x1 to QueueReady. No service function found for queue %u."
							, mmio_dev->qsel);
					}

					virtio_check_vring(&mmio_dev->vqdev->vqs[mmio_dev->qsel]);

					mmio_dev->vqdev->vqs[mmio_dev->qsel].eventfd = eventfd(0, 0);
					mmio_dev->vqdev->vqs[mmio_dev->qsel].qready = 0x1;

					mmio_dev->vqdev->vqs[mmio_dev->qsel].srv_th =
							vmm_run_task(vm,
									mmio_dev->vqdev->vqs[mmio_dev->qsel].srv_fn,
									&mmio_dev->vqdev->vqs[mmio_dev->qsel]);
					if (!mmio_dev->vqdev->vqs[mmio_dev->qsel].srv_th) {
						VIRTIO_DEV_ERRX(mmio_dev->vqdev,
							"vm_run_task failed when trying to start service thread after driver wrote 0x1 to QueueReady.");
					}
				} else if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready == 0x1
					       && *value == 0x0) {
					// Driver is trying to revoke QueueReady while the queue is
					// currently enabled (QueueReady is 0x1).
					// TODO: For now we are going to just make this an error.
					//       In the future, when we actually decide how we want
					//       to clean up the threads, the sequence might look
					//       something like this:
					//       1. Ask the queue's service thread to exit and wait
					//          for it to finish and exit.
					//       2. Once it has exited, close the queue's eventfd
					//          and set both the eventfd and srv_th fields to 0.
					//       3. Finally, write 0x0 to QueueReady.
					VIRTIO_DEV_ERRX(mmio_dev->vqdev,
						"Our (Akaros) MMIO device does not currently allow the driver to revoke QueueReady (i.e. change QueueReady from 0x1 to 0x0). The driver tried to revoke it, so whatever you are doing might require this ability.");
				}

			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueReady register for invalid QueueSel. QueueSel was %u, but the number of queues is %u.",
					mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue notifier
		// Writing a queue index to this register notifies the device that
		// there are new buffers to process in that queue.
		case VIRTIO_MMIO_QUEUE_NOTIFY:
			if (!(mmio_dev->status & VIRTIO_CONFIG_S_DRIVER_OK))
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"Attempt to notify the device before setting the DRIVER_OK status bit.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			else if (*value < mmio_dev->vqdev->num_vqs) {
				notified_queue = &mmio_dev->vqdev->vqs[*value];

				// kick the queue's service thread
				if (notified_queue->eventfd > 0)
					eventfd_write(notified_queue->eventfd, 1);
				else
					VIRTIO_DEV_ERRX(mmio_dev->vqdev,
						"You need to provide a valid eventfd on your virtio_vq so that it can be kicked when the driver writes to QueueNotify.");
			}
			break;

		// Interrupt acknowledge
		// Writing a value with bits set as defined in InterruptStatus to this
		// register notifies the device that events causing the interrupt have
		// been handled.
		case VIRTIO_MMIO_INTERRUPT_ACK:
			if (*value & ~0x3)
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"Attempt to set undefined bits in InterruptACK register.\n"
					"  See virtio-v1.0-cs04 s4.2.2.1 MMIO Device Register Layout");
			mmio_dev->isr &= ~(*value);
			break;

		// Device status
		// Writing non-zero values to this register sets the status flags.
		// Writing zero (0x0) to this register triggers a device reset.
		case VIRTIO_MMIO_STATUS:
			if (*value == 0)
				virtio_mmio_reset(mmio_dev);
			else if (mmio_dev->status & ~(*value)) {
				// virtio-v1.0-cs04 s2.1.1. driver must NOT clear a status bit
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver must not clear any device status bits, except as a result of resetting the device.\n"
					"  See virtio-v1.0-cs04 s2.1.1 Device Status Field");
			} else if (mmio_dev->status & VIRTIO_CONFIG_S_FAILED
				&&   mmio_dev->status != *value) {
				// virtio-v1.0-cs04 s2.1.1. MUST reset before re-init if FAILED
				// NOTE: This fails if the driver tries to *change* the status
				//       after the FAILED bit is set. The driver can set the
				//       same status again all it wants.
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver must reset the device after setting the FAILED status bit, before attempting to re-initialize the device.\n"
					"  See virtio-v1.0-cs04 s2.1.1 Device Status Field");
			}

			// NOTE: If a bit is not set in value, then at this point it
			//       CANNOT be set in status either, because if it were
			//       set in status, we would have just crashed with an
			//       error due to the attempt to clear a status bit.

			// Now we check that status bits are set in the correct
			// sequence during device initialization as described
			// in virtio-v1.0-cs04 s3.1.1 Device Initialization

			else if ((*value & VIRTIO_CONFIG_S_DRIVER)
			          && !(*value & VIRTIO_CONFIG_S_ACKNOWLEDGE)) {
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"Tried to set DRIVER status bit before setting ACKNOWLEDGE feature bit.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			} else if ((*value & VIRTIO_CONFIG_S_FEATURES_OK)
			         && !((*value & VIRTIO_CONFIG_S_ACKNOWLEDGE)
				           && (*value & VIRTIO_CONFIG_S_DRIVER))) {
				// All those parentheses... Lisp must be making a comeback.
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"Tried to set FEATURES_OK status bit before setting both ACKNOWLEDGE and DRIVER status bits.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			} else if ((*value & VIRTIO_CONFIG_S_DRIVER_OK)
			         && !((*value & VIRTIO_CONFIG_S_ACKNOWLEDGE)
				           && (*value & VIRTIO_CONFIG_S_DRIVER)
				           && (*value & VIRTIO_CONFIG_S_FEATURES_OK))) {
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"Tried to set DRIVER_OK status bit before setting all of ACKNOWLEDGE, DRIVER, and FEATURES_OK status bits.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			}

			// NOTE: For now, we allow the driver to set all status bits up
			//       through FEATURES_OK in one fell swoop. The driver is,
			//       however, required to re-read FEATURES_OK after setting it
			//       to be sure that the driver-activated features are a subset
			//       of those supported by the device, so it must make an
			//       additional write to set DRIVER_OK.

			else if ((*value & VIRTIO_CONFIG_S_DRIVER_OK)
			         && !(mmio_dev->status & VIRTIO_CONFIG_S_FEATURES_OK)) {
				VIRTIO_DRI_ERRX(mmio_dev->vqdev,
					"The driver may not set FEATURES_OK and DRIVER_OK status bits simultaneously. It must read back FEATURES_OK after setting it to ensure that its activated features are supported by the device before setting DRIVER_OK.\n"
					"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
			} else {
				// NOTE: Don't set the FEATURES_OK bit unless the driver
				//       activated a valid subset of the supported features
				//       prior to attempting to set FEATURES_OK.
				if (!(mmio_dev->status & VIRTIO_CONFIG_S_FEATURES_OK)
				    && (*value & VIRTIO_CONFIG_S_FEATURES_OK)) {

					err = virtio_validate_feat(mmio_dev->vqdev,
					                           mmio_dev->vqdev->dri_feat);

					if ((mmio_dev->vqdev->dri_feat
						& ~mmio_dev->vqdev->dev_feat)) {
						VIRTIO_DRI_WARNX(mmio_dev->vqdev,
							"The driver did not accept (e.g. activate) a subset of the features offered by the device prior to attempting to set the FEATURES_OK status bit. The bit will remain unset.\n"
							"  See virtio-v1.0-cs04 s3.1.1 Device Initialization");
						*value &= ~VIRTIO_CONFIG_S_FEATURES_OK;
					} else if (err) {
						VIRTIO_DRI_WARNX(mmio_dev->vqdev,
							"The driver did not accept (e.g. activate) a valid combination of the features offered by the device prior to attempting to set the FEATURES_OK status bit. The bit will remain unset.\n"
							"  See virtio-v1.0-cs04 s3.1.1 Device Initialization\n"
							"  Validation Error: %s", err);
						*value &= ~VIRTIO_CONFIG_S_FEATURES_OK;
					}
				}
				// Device status is only a byte wide.
				mmio_dev->status = *value & 0xff;
			}
			break;

		// Queue's Descriptor Table 64 bit long physical address, low 32
		case VIRTIO_MMIO_QUEUE_DESC_LOW:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueDescLow on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear low bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.desc
				  & ((uint64_t)0xffffffff << 32));
				// write low bits
				temp_ptr = (void *) ((uint64_t)temp_ptr | *value);

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 16)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's descriptor table (%p) is misaligned. Address should be a multiple of 16.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue desc
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.desc = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueDescLow register for invalid QueueSel. QueueSel was %u, but the number of queues is %u.",
					mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue's Descriptor Table 64 bit long physical address, high 32
		case VIRTIO_MMIO_QUEUE_DESC_HIGH:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueDescHigh on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear high bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.desc
				  & ((uint64_t)0xffffffff));
				// write high bits
				temp_ptr = (void *) ((uint64_t)temp_ptr
				                  | ((uint64_t)(*value) << 32));

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 16)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's descriptor table (%p) is misaligned. Address should be a multiple of 16.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue desc
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.desc = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueDescHigh register for invalid QueueSel. QueueSel was %u, but the number of queues is %u."
					, mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue's Available Ring 64 bit long physical address, low 32
		case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueAvailLow on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear low bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.avail
				  & ((uint64_t)0xffffffff << 32));
				// write low bits
				temp_ptr = (void *) ((uint64_t)temp_ptr | *value);

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 2)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's available ring (%p) is misaligned. Address should be a multiple of 2.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue avail
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.avail = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueAvailLow register for invalid QueueSel. QueueSel was %u, but the number of queues is %u."
					, mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue's Available Ring 64 bit long physical address, high 32
		case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueAvailHigh on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear high bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.avail
				 &  ((uint64_t)0xffffffff));
				// write high bits
				temp_ptr = (void *) ((uint64_t)temp_ptr
				                  | ((uint64_t)(*value) << 32));

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 2)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's available ring (%p) is misaligned. Address should be a multiple of 2.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue avail
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.avail = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueAvailHigh register for invalid QueueSel. QueueSel was %u, but the number of queues is %u."
					, mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue's Used Ring 64 bit long physical address, low 32
		case VIRTIO_MMIO_QUEUE_USED_LOW:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueUsedLow on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear low bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.used
				  & ((uint64_t)0xffffffff << 32));
				// write low bits
				temp_ptr = (void *) ((uint64_t)temp_ptr | *value);

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 4)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's used ring (%p) is misaligned. Address should be a multiple of 4.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue used
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.used = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueUsedLow register for invalid QueueSel. QueueSel was %u, but the number of queues is %u."
					, mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Queue's Used Ring 64 bit long physical address, high 32
		case VIRTIO_MMIO_QUEUE_USED_HIGH:
			if (mmio_dev->qsel < mmio_dev->vqdev->num_vqs) {
				if (mmio_dev->vqdev->vqs[mmio_dev->qsel].qready != 0)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Attempt to access QueueUsedHigh on queue %d, which has nonzero QueueReady.\n"
						"  See virtio-v1.0-cs04 s4.2.2.2 MMIO Device Register Layout"
						, mmio_dev->qsel);

				// clear high bits
				temp_ptr = (void *)
				    ((uint64_t)mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.used
				  & ((uint64_t)0xffffffff));
				// write high bits
				temp_ptr = (void *) ((uint64_t)temp_ptr
				                  | ((uint64_t)(*value) << 32));

				// virtio-v1.0-cs04 s2.4 Virtqueues
				if ((uint64_t)temp_ptr % 4)
					VIRTIO_DRI_ERRX(mmio_dev->vqdev,
						"Physical address of guest's used ring (%p) is misaligned. Address should be a multiple of 4.\n"
						"  See virtio-v1.0-cs04 s2.4 Virtqueues");

				// assign the new value to the queue used
				mmio_dev->vqdev->vqs[mmio_dev->qsel].vring.used = temp_ptr;
			} else {
				VIRTIO_DRI_WARNX(mmio_dev->vqdev,
					"Attempt to write QueueUsedHigh register for invalid QueueSel. QueueSel was %u, but the number of queues is %u."
					, mmio_dev->qsel, mmio_dev->vqdev->num_vqs);
			}
			break;

		// Read-only register offsets:
		case VIRTIO_MMIO_MAGIC_VALUE:
		case VIRTIO_MMIO_VERSION:
		case VIRTIO_MMIO_DEVICE_ID:
		case VIRTIO_MMIO_VENDOR_ID:
		case VIRTIO_MMIO_DEVICE_FEATURES:
		case VIRTIO_MMIO_QUEUE_NUM_MAX:
		case VIRTIO_MMIO_INTERRUPT_STATUS:
		case VIRTIO_MMIO_CONFIG_GENERATION:
			// Write to read-only register
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to write read-only device register offset 0x%x.",
				offset);
			break;
		default:
			// Bad register offset
			VIRTIO_DRI_ERRX(mmio_dev->vqdev,
				"Attempt to write invalid device register offset 0x%x.",
				offset);
			break;
	}
}
