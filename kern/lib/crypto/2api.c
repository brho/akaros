/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Externally-callable APIs
 * (Firmware portion)
 */

#include "2sysincludes.h"
#include "2api.h"
#include "2common.h"
#include "2misc.h"
#include "2nvstorage.h"
#include "2secdata.h"
#include "2sha.h"
#include "2rsa.h"
#include "2tpm_bootmode.h"

int vb2api_secdata_check(const struct vb2_context *ctx)
{
	return vb2_secdata_check_crc(ctx);
}

int vb2api_secdata_create(struct vb2_context *ctx)
{
	return vb2_secdata_create(ctx);
}

void vb2api_fail(struct vb2_context *ctx, uint8_t reason, uint8_t subcode)
{
	/* Initialize the vboot context if it hasn't been yet */
	vb2_init_context(ctx);

	vb2_fail(ctx, reason, subcode);
}

int vb2api_fw_phase1(struct vb2_context *ctx)
{
	int rv;

	/* Initialize the vboot context if it hasn't been yet */
	vb2_init_context(ctx);

	/* Initialize NV context */
	vb2_nv_init(ctx);

	/*
	 * Handle caller-requested reboot due to secdata.  Do this before we
	 * even look at secdata.  If we fail because of a reboot loop we'll be
	 * the first failure so will get to set the recovery reason.
	 */
	if (!(ctx->flags & VB2_CONTEXT_SECDATA_WANTS_REBOOT)) {
		/* No reboot requested */
		vb2_nv_set(ctx, VB2_NV_TPM_REQUESTED_REBOOT, 0);
	} else if (vb2_nv_get(ctx, VB2_NV_TPM_REQUESTED_REBOOT)) {
		/*
		 * Reboot requested... again.  Fool me once, shame on you.
		 * Fool me twice, shame on me.  Fail into recovery to avoid
		 * a reboot loop.
		 */
		vb2_fail(ctx, VB2_RECOVERY_RO_TPM_REBOOT, 0);
	} else {
		/* Reboot requested for the first time */
		vb2_nv_set(ctx, VB2_NV_TPM_REQUESTED_REBOOT, 1);
		return VB2_ERROR_API_PHASE1_SECDATA_REBOOT;
	}

	/* Initialize secure data */
	rv = vb2_secdata_init(ctx);
	if (rv)
		vb2_fail(ctx, VB2_RECOVERY_SECDATA_INIT, rv);

	/* Load and parse the GBB header */
	rv = vb2_fw_parse_gbb(ctx);
	if (rv)
		vb2_fail(ctx, VB2_RECOVERY_GBB_HEADER, rv);

	/*
	 * Check for recovery.  Note that this function returns void, since any
	 * errors result in requesting recovery.  That's also why we don't
	 * return error from failures in the preceding two steps; those
	 * failures simply cause us to detect recovery mode here.
	 */
	vb2_check_recovery(ctx);

	/* Check for dev switch */
	rv = vb2_check_dev_switch(ctx);
	if (rv && !(ctx->flags & VB2_CONTEXT_RECOVERY_MODE)) {
		/*
		 * Error in dev switch processing, and we weren't already
		 * headed for recovery mode.  Reboot into recovery mode, since
		 * it's too late to handle those errors this boot, and we need
		 * to take a different path through the dev switch checking
		 * code in that case.
		 */
		vb2_fail(ctx, VB2_RECOVERY_DEV_SWITCH, rv);
		return rv;
	}

	/* Return error if recovery is needed */
	if (ctx->flags & VB2_CONTEXT_RECOVERY_MODE) {
		/* Always clear RAM when entering recovery mode */
		ctx->flags |= VB2_CONTEXT_CLEAR_RAM;
		return VB2_ERROR_API_PHASE1_RECOVERY;
	}

	return VB2_SUCCESS;
}

int vb2api_fw_phase2(struct vb2_context *ctx)
{
	int rv;

	/*
	 * Use the slot from the last boot if this is a resume.  Do not set
	 * VB2_SD_STATUS_CHOSE_SLOT so the try counter is not decremented on
	 * failure as we are explicitly not attempting to boot from a new slot.
	 */
	if (ctx->flags & VB2_CONTEXT_S3_RESUME) {
		struct vb2_shared_data *sd = vb2_get_sd(ctx);

		/* Set the current slot to the last booted slot */
		sd->fw_slot = vb2_nv_get(ctx, VB2_NV_FW_TRIED);

		/* Set context flag if we're using slot B */
		if (sd->fw_slot)
			ctx->flags |= VB2_CONTEXT_FW_SLOT_B;

		return VB2_SUCCESS;
	}

	/* Always clear RAM when entering developer mode */
	if (ctx->flags & VB2_CONTEXT_DEVELOPER_MODE)
		ctx->flags |= VB2_CONTEXT_CLEAR_RAM;

	/* Check for explicit request to clear TPM */
	rv = vb2_check_tpm_clear(ctx);
	if (rv) {
		vb2_fail(ctx, VB2_RECOVERY_TPM_CLEAR_OWNER, rv);
		return rv;
	}

	/* Decide which firmware slot to try this boot */
	rv = vb2_select_fw_slot(ctx);
	if (rv) {
		vb2_fail(ctx, VB2_RECOVERY_FW_SLOT, rv);
		return rv;
	}

	return VB2_SUCCESS;
}

int vb2api_extend_hash(struct vb2_context *ctx,
		       const void *buf,
		       uint32_t size)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	struct vb2_digest_context *dc = (struct vb2_digest_context *)
		(ctx->workbuf + sd->workbuf_hash_offset);

	/* Must have initialized hash digest work area */
	if (!sd->workbuf_hash_size)
		return VB2_ERROR_API_EXTEND_HASH_WORKBUF;

	/* Don't extend past the data we expect to hash */
	if (!size || size > sd->hash_remaining_size)
		return VB2_ERROR_API_EXTEND_HASH_SIZE;

	sd->hash_remaining_size -= size;

	if (dc->using_hwcrypto)
		return vb2ex_hwcrypto_digest_extend(buf, size);
	else
		return vb2_digest_extend(dc, buf, size);
}

int vb2api_get_pcr_digest(struct vb2_context *ctx,
			  enum vb2_pcr_digest which_digest,
			  uint8_t *dest,
			  uint32_t *dest_size)
{
	const uint8_t *digest;
	uint32_t digest_size;

	switch (which_digest) {
	case BOOT_MODE_PCR:
		digest = vb2_get_boot_state_digest(ctx);
		digest_size = VB2_SHA1_DIGEST_SIZE;
		break;
	case HWID_DIGEST_PCR:
		digest = vb2_get_sd(ctx)->gbb_hwid_digest;
		digest_size = VB2_GBB_HWID_DIGEST_SIZE;
		break;
	default:
		return VB2_ERROR_API_PCR_DIGEST;
	}

	if (digest == NULL || *dest_size < digest_size)
		return VB2_ERROR_API_PCR_DIGEST_BUF;

	memcpy(dest, digest, digest_size);
	if (digest_size < *dest_size)
		memset(dest + digest_size, 0, *dest_size - digest_size);

	*dest_size = digest_size;

	return VB2_SUCCESS;
}
