/*
 * Copyright (c) 2016 Google Inc.
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
 */

#include <vmm/virtio.h>
#include <vmm/virtio_ids.h>
#include <vmm/virtio_config.h>

// Returns NULL if the features are valid, otherwise returns
// an error string describing what part of validation failed
// We pass the vqdev instead of just the dev_id in case we
// also want to validate the device-specific config space.
// feat is the feature vector that you want to validate for the vqdev
const char *virtio_validate_feat(struct virtio_vq_dev *vqdev, uint64_t feat)
{

	// First validate device-specific features. We want to tell someone
	// when they forgot to implement validation code for a new device
	// as soon as possible, so that they don't skip this when they
	// implement new devices.
	switch (vqdev->dev_id) {
	case VIRTIO_ID_CONSOLE:
		// No interdependent features for the console.
		break;
	case VIRTIO_ID_NET:
		// There is no "mandatory" feature bit that we always want to
		// have, either the device can set its own MAC Address (as it
		// does now) or the driver can set it using a controller thread.
		break;
	case VIRTIO_ID_BLOCK:
		break;
	case 0:
		return "Invalid device id (0x0)! On the MMIO transport, this value indicates that the device is a system memory map with placeholder devices at static, well known addresses. In any case, this is not something you validate features for.";
	default:
		return "Validation not implemented for this device type! You MUST implement validation for this device! You should add your new code to the virtio_validate_feat function in vmm/virtio.c.";
	}

	// Validate common features
	if (!(feat & ((uint64_t)1 << VIRTIO_F_VERSION_1)))
		return "A device MUST offer the VIRTIO_F_VERSION_1 feature bit and a driver MUST accept it.\n"
		       "  See virtio-v1.0-cs04 s6.1 & s6.2.";

	return NULL;
}
