/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Non-volatile storage routines */

#include "2sysincludes.h"
#include "2common.h"
#include "2crc8.h"
#include "2misc.h"
#include "2nvstorage.h"
#include "2nvstorage_fields.h"

static void vb2_nv_regen_crc(struct vb2_context *ctx)
{
	ctx->nvdata[VB2_NV_OFFS_CRC] = vb2_crc8(ctx->nvdata, VB2_NV_OFFS_CRC);
	ctx->flags |= VB2_CONTEXT_NVDATA_CHANGED;
}

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
int vb2_nv_check_crc(const struct vb2_context *ctx)
{
	const uint8_t *p = ctx->nvdata;

	/* Check header */
	if (VB2_NV_HEADER_SIGNATURE !=
	    (p[VB2_NV_OFFS_HEADER] & VB2_NV_HEADER_MASK))
		return VB2_ERROR_NV_HEADER;

	/* Check CRC */
	if (vb2_crc8(p, VB2_NV_OFFS_CRC) != p[VB2_NV_OFFS_CRC])
		return VB2_ERROR_NV_CRC;

	return VB2_SUCCESS;
}

void vb2_nv_init(struct vb2_context *ctx)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	uint8_t *p = ctx->nvdata;

	/* Check data for consistency */
	if (vb2_nv_check_crc(ctx) != VB2_SUCCESS) {
		/* Data is inconsistent (bad CRC or header); reset defaults */
		memset(p, 0, VB2_NVDATA_SIZE);
		p[VB2_NV_OFFS_HEADER] = (VB2_NV_HEADER_SIGNATURE |
					 VB2_NV_HEADER_FW_SETTINGS_RESET |
					 VB2_NV_HEADER_KERNEL_SETTINGS_RESET);

		/* Regenerate CRC */
		vb2_nv_regen_crc(ctx);

		/* Set status flag */
		sd->status |= VB2_SD_STATUS_NV_REINIT;
		/* TODO: unit test for status flag being set */
	}

	sd->status |= VB2_SD_STATUS_NV_INIT;
}

/* Macro for vb2_nv_get() single-bit settings to reduce duplicate code. */
#define GETBIT(offs, mask) (p[offs] & mask ? 1 : 0)

uint32_t vb2_nv_get(struct vb2_context *ctx, enum vb2_nv_param param)
{
	const uint8_t *p = ctx->nvdata;

	/*
	 * TODO: We could reduce the binary size for this code by #ifdef'ing
	 * out the params not used by firmware verification.
	 */
	switch (param) {
	case VB2_NV_FIRMWARE_SETTINGS_RESET:
		return GETBIT(VB2_NV_OFFS_HEADER,
			      VB2_NV_HEADER_FW_SETTINGS_RESET);

	case VB2_NV_KERNEL_SETTINGS_RESET:
		return GETBIT(VB2_NV_OFFS_HEADER,
			      VB2_NV_HEADER_KERNEL_SETTINGS_RESET);

	case VB2_NV_DEBUG_RESET_MODE:
		return GETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_DEBUG_RESET);

	case VB2_NV_TRY_NEXT:
		return GETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_TRY_NEXT);

	case VB2_NV_TRY_COUNT:
		return p[VB2_NV_OFFS_BOOT] & VB2_NV_BOOT_TRY_COUNT_MASK;

	case VB2_NV_FW_TRIED:
		return GETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_TRIED);

	case VB2_NV_FW_RESULT:
		return p[VB2_NV_OFFS_BOOT2] & VB2_NV_BOOT2_RESULT_MASK;

	case VB2_NV_FW_PREV_TRIED:
		return GETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_PREV_TRIED);

	case VB2_NV_FW_PREV_RESULT:
		return (p[VB2_NV_OFFS_BOOT2] & VB2_NV_BOOT2_PREV_RESULT_MASK)
			>> VB2_NV_BOOT2_PREV_RESULT_SHIFT;

	case VB2_NV_RECOVERY_REQUEST:
		return p[VB2_NV_OFFS_RECOVERY];

	case VB2_NV_RECOVERY_SUBCODE:
		return p[VB2_NV_OFFS_RECOVERY_SUBCODE];

	case VB2_NV_LOCALIZATION_INDEX:
		return p[VB2_NV_OFFS_LOCALIZATION];

	case VB2_NV_KERNEL_FIELD:
		return (p[VB2_NV_OFFS_KERNEL]
			| (p[VB2_NV_OFFS_KERNEL + 1] << 8)
			| (p[VB2_NV_OFFS_KERNEL + 2] << 16)
			| (p[VB2_NV_OFFS_KERNEL + 3] << 24));

	case VB2_NV_DEV_BOOT_USB:
		return GETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_USB);

	case VB2_NV_DEV_BOOT_LEGACY:
		return GETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_LEGACY);

	case VB2_NV_DEV_BOOT_SIGNED_ONLY:
		return GETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_SIGNED_ONLY);

	case VB2_NV_DEV_BOOT_FASTBOOT_FULL_CAP:
		return GETBIT(VB2_NV_OFFS_DEV,
			      VB2_NV_DEV_FLAG_FASTBOOT_FULL_CAP);

	case VB2_NV_DEV_DEFAULT_BOOT:
		return (p[VB2_NV_OFFS_DEV] & VB2_NV_DEV_FLAG_DEFAULT_BOOT)
			>> VB2_NV_DEV_DEFAULT_BOOT_SHIFT;

	case VB2_NV_DISABLE_DEV_REQUEST:
		return GETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_DISABLE_DEV);

	case VB2_NV_OPROM_NEEDED:
		return GETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_OPROM_NEEDED);

	case VB2_NV_BACKUP_NVRAM_REQUEST:
		return GETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_BACKUP_NVRAM);

	case VB2_NV_CLEAR_TPM_OWNER_REQUEST:
		return GETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_CLEAR_OWNER_REQUEST);

	case VB2_NV_CLEAR_TPM_OWNER_DONE:
		return GETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_CLEAR_OWNER_DONE);

	case VB2_NV_TPM_REQUESTED_REBOOT:
		return GETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_REBOOTED);

	case VB2_NV_REQ_WIPEOUT:
		return GETBIT(VB2_NV_OFFS_HEADER , VB2_NV_HEADER_WIPEOUT);

	case VB2_NV_FASTBOOT_UNLOCK_IN_FW:
		return GETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_UNLOCK_FASTBOOT);

	case VB2_NV_BOOT_ON_AC_DETECT:
		return GETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_BOOT_ON_AC_DETECT);

	case VB2_NV_TRY_RO_SYNC:
		return GETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_TRY_RO_SYNC);

	case VB2_NV_BATTERY_CUTOFF_REQUEST:
		return GETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_BATTERY_CUTOFF);
	}

	/*
	 * Put default return outside the switch() instead of in default:, so
	 * that adding a new param will cause a compiler warning.
	 */
	return 0;
}

#undef GETBIT

/* Macro for vb2_nv_set() single-bit settings to reduce duplicate code. */
#define SETBIT(offs, mask)					\
	{ if (value) p[offs] |= mask; else p[offs] &= ~mask; }

void vb2_nv_set(struct vb2_context *ctx,
		enum vb2_nv_param param,
		uint32_t value)
{
	uint8_t *p = ctx->nvdata;

	/* If not changing the value, don't regenerate the CRC. */
	if (vb2_nv_get(ctx, param) == value)
		return;

	/*
	 * TODO: We could reduce the binary size for this code by #ifdef'ing
	 * out the params not used by firmware verification.
	 */
	switch (param) {
	case VB2_NV_FIRMWARE_SETTINGS_RESET:
		SETBIT(VB2_NV_OFFS_HEADER, VB2_NV_HEADER_FW_SETTINGS_RESET);
		break;

	case VB2_NV_KERNEL_SETTINGS_RESET:
		SETBIT(VB2_NV_OFFS_HEADER, VB2_NV_HEADER_KERNEL_SETTINGS_RESET);
		break;

	case VB2_NV_DEBUG_RESET_MODE:
		SETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_DEBUG_RESET);
		break;

	case VB2_NV_TRY_NEXT:
		SETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_TRY_NEXT);
		break;

	case VB2_NV_TRY_COUNT:
		/* Clip to valid range. */
		if (value > VB2_NV_BOOT_TRY_COUNT_MASK)
			value = VB2_NV_BOOT_TRY_COUNT_MASK;

		p[VB2_NV_OFFS_BOOT] &= ~VB2_NV_BOOT_TRY_COUNT_MASK;
		p[VB2_NV_OFFS_BOOT] |= (uint8_t)value;
		break;

	case VB2_NV_FW_TRIED:
		SETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_TRIED);
		break;

	case VB2_NV_FW_RESULT:
		/* Map out of range values to unknown */
		if (value > VB2_NV_BOOT2_RESULT_MASK)
			value = VB2_FW_RESULT_UNKNOWN;

		p[VB2_NV_OFFS_BOOT2] &= ~VB2_NV_BOOT2_RESULT_MASK;
		p[VB2_NV_OFFS_BOOT2] |= (uint8_t)value;
		break;

	case VB2_NV_FW_PREV_TRIED:
		SETBIT(VB2_NV_OFFS_BOOT2, VB2_NV_BOOT2_PREV_TRIED);
		break;

	case VB2_NV_FW_PREV_RESULT:
		/* Map out of range values to unknown */
		if (value > VB2_NV_BOOT2_RESULT_MASK)
			value = VB2_FW_RESULT_UNKNOWN;

		p[VB2_NV_OFFS_BOOT2] &= ~VB2_NV_BOOT2_PREV_RESULT_MASK;
		p[VB2_NV_OFFS_BOOT2] |=
			(uint8_t)(value << VB2_NV_BOOT2_PREV_RESULT_SHIFT);
		break;

	case VB2_NV_RECOVERY_REQUEST:
		/*
		 * Map values outside the valid range to the legacy reason,
		 * since we can't determine if we're called from kernel or user
		 * mode.
		 */
		if (value > 0xff)
			value = VB2_RECOVERY_LEGACY;
		p[VB2_NV_OFFS_RECOVERY] = (uint8_t)value;
		break;

	case VB2_NV_RECOVERY_SUBCODE:
		p[VB2_NV_OFFS_RECOVERY_SUBCODE] = (uint8_t)value;
		break;

	case VB2_NV_LOCALIZATION_INDEX:
		/* Map values outside the valid range to the default index. */
		if (value > 0xFF)
			value = 0;
		p[VB2_NV_OFFS_LOCALIZATION] = (uint8_t)value;
		break;

	case VB2_NV_KERNEL_FIELD:
		p[VB2_NV_OFFS_KERNEL] = (uint8_t)(value);
		p[VB2_NV_OFFS_KERNEL + 1] = (uint8_t)(value >> 8);
		p[VB2_NV_OFFS_KERNEL + 2] = (uint8_t)(value >> 16);
		p[VB2_NV_OFFS_KERNEL + 3] = (uint8_t)(value >> 24);
		break;

	case VB2_NV_DEV_BOOT_USB:
		SETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_USB);
		break;

	case VB2_NV_DEV_BOOT_LEGACY:
		SETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_LEGACY);
		break;

	case VB2_NV_DEV_BOOT_SIGNED_ONLY:
		SETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_SIGNED_ONLY);
		break;

	case VB2_NV_DEV_BOOT_FASTBOOT_FULL_CAP:
		SETBIT(VB2_NV_OFFS_DEV, VB2_NV_DEV_FLAG_FASTBOOT_FULL_CAP);
		break;

	case VB2_NV_DEV_DEFAULT_BOOT:
		/* Map out of range values to disk */
		if (value > (VB2_NV_DEV_FLAG_DEFAULT_BOOT >>
			     VB2_NV_DEV_DEFAULT_BOOT_SHIFT))
			value = VB2_DEV_DEFAULT_BOOT_DISK;

		p[VB2_NV_OFFS_DEV] &= ~VB2_NV_DEV_FLAG_DEFAULT_BOOT;
		p[VB2_NV_OFFS_DEV] |=
			(uint8_t)(value << VB2_NV_DEV_DEFAULT_BOOT_SHIFT);
		break;

	case VB2_NV_DISABLE_DEV_REQUEST:
		SETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_DISABLE_DEV);
		break;

	case VB2_NV_OPROM_NEEDED:
		SETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_OPROM_NEEDED);
		break;

	case VB2_NV_BACKUP_NVRAM_REQUEST:
		SETBIT(VB2_NV_OFFS_BOOT, VB2_NV_BOOT_BACKUP_NVRAM);
		break;

	case VB2_NV_CLEAR_TPM_OWNER_REQUEST:
		SETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_CLEAR_OWNER_REQUEST);
		break;

	case VB2_NV_CLEAR_TPM_OWNER_DONE:
		SETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_CLEAR_OWNER_DONE);
		break;

	case VB2_NV_TPM_REQUESTED_REBOOT:
		SETBIT(VB2_NV_OFFS_TPM, VB2_NV_TPM_REBOOTED);
		break;

	case VB2_NV_REQ_WIPEOUT:
		SETBIT(VB2_NV_OFFS_HEADER , VB2_NV_HEADER_WIPEOUT);
		break;

	case VB2_NV_FASTBOOT_UNLOCK_IN_FW:
		SETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_UNLOCK_FASTBOOT);
		break;

	case VB2_NV_BOOT_ON_AC_DETECT:
		SETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_BOOT_ON_AC_DETECT);
		break;

	case VB2_NV_TRY_RO_SYNC:
		SETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_TRY_RO_SYNC);
		break;

	case VB2_NV_BATTERY_CUTOFF_REQUEST:
		SETBIT(VB2_NV_OFFS_MISC, VB2_NV_MISC_BATTERY_CUTOFF);
		break;
	}

	/*
	 * Note there is no default case.  This causes a compiler warning if
	 * a new param is added to the enum without adding support here.
	 */

	/* Need to regenerate CRC, since the value changed. */
	vb2_nv_regen_crc(ctx);
}

#undef SETBIT
