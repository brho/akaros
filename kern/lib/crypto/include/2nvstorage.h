/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Non-volatile storage routines
 */

#ifndef VBOOT_REFERENCE_VBOOT_2NVSTORAGE_H_
#define VBOOT_REFERENCE_VBOOT_2NVSTORAGE_H_

enum vb2_nv_param {
	/*
	 * Parameter values have been reset to defaults (flag for firmware).
	 * 0=clear; 1=set.
	 */
	VB2_NV_FIRMWARE_SETTINGS_RESET = 0,
	/*
	 * Parameter values have been reset to defaults (flag for kernel).
	 * 0=clear; 1=set.
	 */
	VB2_NV_KERNEL_SETTINGS_RESET,
	/* Request debug reset on next S3->S0 transition.  0=clear; 1=set. */
	VB2_NV_DEBUG_RESET_MODE,
	/* Firmware slot to try next.  0=A, 1=B */
	VB2_NV_TRY_NEXT,
	/*
	 * Number of times to try booting RW firmware slot B before slot A.
	 * Valid range: 0-15.
	 *
	 * For VB2, number of times to try booting the slot indicated by
	 * VB2_NV_TRY_NEXT.  On a 1->0 transition of try count, VB2_NV_TRY_NEXT
	 * will be set to the other slot.
	 */
	VB2_NV_TRY_COUNT,
	/*
	 * Request recovery mode on next boot; see 2recovery_reason.h for
	 * currently defined reason codes.  8-bit value.
	 */
	VB2_NV_RECOVERY_REQUEST,
	/*
	 * Localization index for screen bitmaps displayed by firmware.
	 * 8-bit value.
	 */
	VB2_NV_LOCALIZATION_INDEX,
	/* Field reserved for kernel/user-mode use; 32-bit value. */
	VB2_NV_KERNEL_FIELD,
	/* Allow booting from USB in developer mode.  0=no, 1=yes. */
	VB2_NV_DEV_BOOT_USB,
	/* Allow booting of legacy OSes in developer mode.  0=no, 1=yes. */
	VB2_NV_DEV_BOOT_LEGACY,
	/* Only boot Google-signed images in developer mode.  0=no, 1=yes. */
	VB2_NV_DEV_BOOT_SIGNED_ONLY,
	/*
	 * Allow full fastboot capability in firmware in developer mode.
	 * 0=no, 1=yes.
	 */
	VB2_NV_DEV_BOOT_FASTBOOT_FULL_CAP,
	/* Set default boot mode (see vb2_dev_default_boot) */
	VB2_NV_DEV_DEFAULT_BOOT,
	/*
	 * Set by userspace to request that RO firmware disable dev-mode on the
	 * next boot. This is likely only possible if the dev-switch is
	 * virtual.
	 */
	VB2_NV_DISABLE_DEV_REQUEST,
	/*
	 * Set and cleared by vboot to request that the video Option ROM be
	 * loaded at boot time, so that BIOS screens can be displayed. 0=no,
	 * 1=yes.
	 */
	VB2_NV_OPROM_NEEDED,
	/* Request that the firmware clear the TPM owner on the next boot. */
	VB2_NV_CLEAR_TPM_OWNER_REQUEST,
	/* Flag that TPM owner was cleared on request. */
	VB2_NV_CLEAR_TPM_OWNER_DONE,
	/* TPM requested a reboot already. */
	VB2_NV_TPM_REQUESTED_REBOOT,
	/* More details on recovery reason */
	VB2_NV_RECOVERY_SUBCODE,
	/* Request that NVRAM be backed up at next boot if possible. */
	VB2_NV_BACKUP_NVRAM_REQUEST,
	/* Firmware slot tried this boot (0=A, 1=B) */
	VB2_NV_FW_TRIED,
	/* Result of trying that firmware (see vb2_fw_result) */
	VB2_NV_FW_RESULT,
	/* Firmware slot tried previous boot (0=A, 1=B) */
	VB2_NV_FW_PREV_TRIED,
	/* Result of trying that firmware (see vb2_fw_result) */
	VB2_NV_FW_PREV_RESULT,
	/* Request wipeout of the device by the app. */
	VB2_NV_REQ_WIPEOUT,

	/* Fastboot: Unlock in firmware, 0=disabled, 1=enabled. */
	VB2_NV_FASTBOOT_UNLOCK_IN_FW,
	/* Boot system when AC detected (0=no, 1=yes). */
	VB2_NV_BOOT_ON_AC_DETECT,
	/* Try to update the EC-RO image after updating the EC-RW image(0=no, 1=yes). */
	VB2_NV_TRY_RO_SYNC,
        /* Cut off battery and shutdown on next boot. */
        VB2_NV_BATTERY_CUTOFF_REQUEST,
};

/* Set default boot in developer mode */
enum vb2_dev_default_boot {
	/* Default to boot from disk*/
	VB2_DEV_DEFAULT_BOOT_DISK = 0,

	/* Default to boot from USB */
	VB2_DEV_DEFAULT_BOOT_USB= 1,

	/* Default to boot legacy OS */
	VB2_DEV_DEFAULT_BOOT_LEGACY = 2,

};

/* Firmware result codes for VB2_NV_FW_RESULT and VB2_NV_FW_PREV_RESULT */
enum vb2_fw_result {
	/* Unknown */
	VB2_FW_RESULT_UNKNOWN = 0,

	/* Trying a new slot, but haven't reached success/failure */
	VB2_FW_RESULT_TRYING = 1,

	/* Successfully booted to the OS */
	VB2_FW_RESULT_SUCCESS = 2,

	/* Known failure */
	VB2_FW_RESULT_FAILURE = 3,
};

/**
 * Check the CRC of the non-volatile storage context.
 *
 * Use this if reading from non-volatile storage may be flaky, and you want to
 * retry reading it several times.
 *
 * This may be called before vb2_context_init().
 *
 * @param ctx		Context pointer
 * @return VB2_SUCCESS, or non-zero error code if error.
 */
int vb2_nv_check_crc(const struct vb2_context *ctx);

/**
 * Initialize the non-volatile storage context and verify its CRC.
 *
 * @param ctx		Context pointer
 */
void vb2_nv_init(struct vb2_context *ctx);

/**
 * Read a non-volatile value.
 *
 * @param ctx		Context pointer
 * @param param		Parameter to read
 * @return The value of the parameter.  If you somehow force an invalid
 *         parameter number, returns 0.
 */
uint32_t vb2_nv_get(struct vb2_context *ctx, enum vb2_nv_param param);

/**
 * Write a non-volatile value.
 *
 * Ignores writes to unknown params.
 *
 * @param ctx		Context pointer
 * @param param		Parameter to write
 * @param value		New value
 */
void vb2_nv_set(struct vb2_context *ctx,
		enum vb2_nv_param param,
		uint32_t value);

#endif  /* VBOOT_REFERENCE_VBOOT_2NVSTORAGE_H_ */
