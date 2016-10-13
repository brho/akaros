/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Secure storage APIs - kernel version space
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2crc8.h"
#include "2misc.h"
#include "2secdata.h"

int vb2_secdatak_check_crc(const struct vb2_context *ctx)
{
	const struct vb2_secdatak *sec =
		(const struct vb2_secdatak *)ctx->secdatak;

	/* Verify CRC */
	if (sec->crc8 != vb2_crc8(sec, offsetof(struct vb2_secdatak, crc8)))
		return VB2_ERROR_SECDATAK_CRC;

	return VB2_SUCCESS;
}

int vb2_secdatak_create(struct vb2_context *ctx)
{
	struct vb2_secdatak *sec = (struct vb2_secdatak *)ctx->secdatak;

	/* Clear the entire struct */
	memset(sec, 0, sizeof(*sec));

	/* Set to current version */
	sec->struct_version = VB2_SECDATAK_VERSION;

	/* Set UID */
	sec->uid = VB2_SECDATAK_UID;

	/* Calculate initial CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdatak, crc8));
	ctx->flags |= VB2_CONTEXT_SECDATAK_CHANGED;
	return VB2_SUCCESS;
}

int vb2_secdatak_init(struct vb2_context *ctx)
{
	struct vb2_secdatak *sec = (struct vb2_secdatak *)ctx->secdatak;
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	int rv;

	rv = vb2_secdatak_check_crc(ctx);
	if (rv)
		return rv;

	/* Make sure the UID is correct */
	if (sec->uid != VB2_SECDATAK_UID)
		return VB2_ERROR_SECDATAK_UID;

	/* Set status flag */
	sd->status |= VB2_SD_STATUS_SECDATAK_INIT;
	/* TODO: unit test for that */

	return VB2_SUCCESS;
}

int vb2_secdatak_get(struct vb2_context *ctx,
		    enum vb2_secdatak_param param,
		    uint32_t *dest)
{
	struct vb2_secdatak *sec = (struct vb2_secdatak *)ctx->secdatak;

	if (!(vb2_get_sd(ctx)->status & VB2_SD_STATUS_SECDATAK_INIT))
		return VB2_ERROR_SECDATAK_GET_UNINITIALIZED;

	switch(param) {
	case VB2_SECDATAK_VERSIONS:
		*dest = sec->kernel_versions;
		return VB2_SUCCESS;

	default:
		return VB2_ERROR_SECDATAK_GET_PARAM;
	}
}

int vb2_secdatak_set(struct vb2_context *ctx,
		    enum vb2_secdatak_param param,
		    uint32_t value)
{
	struct vb2_secdatak *sec = (struct vb2_secdatak *)ctx->secdatak;
	uint32_t now;

	if (!(vb2_get_sd(ctx)->status & VB2_SD_STATUS_SECDATAK_INIT))
		return VB2_ERROR_SECDATAK_SET_UNINITIALIZED;

	/* If not changing the value, don't regenerate the CRC. */
	if (vb2_secdatak_get(ctx, param, &now) == VB2_SUCCESS && now == value)
		return VB2_SUCCESS;

	switch(param) {
	case VB2_SECDATAK_VERSIONS:
		sec->kernel_versions = value;
		break;

	default:
		return VB2_ERROR_SECDATAK_SET_PARAM;
	}

	/* Regenerate CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdatak, crc8));
	ctx->flags |= VB2_CONTEXT_SECDATAK_CHANGED;
	return VB2_SUCCESS;
}
