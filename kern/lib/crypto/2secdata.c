/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Secure storage APIs
 */

#include "2sysincludes.h"
#include "2common.h"
#include "2crc8.h"
#include "2misc.h"
#include "2secdata.h"

int vb2_secdata_check_crc(const struct vb2_context *ctx)
{
	const struct vb2_secdata *sec =
		(const struct vb2_secdata *)ctx->secdata;

	/* Verify CRC */
	if (sec->crc8 != vb2_crc8(sec, offsetof(struct vb2_secdata, crc8)))
		return VB2_ERROR_SECDATA_CRC;

	/* CRC(<000...00>) is 0, so check version as well (should never be 0) */
	if (!sec->struct_version)
		return VB2_ERROR_SECDATA_ZERO;

	return VB2_SUCCESS;
}

int vb2_secdata_create(struct vb2_context *ctx)
{
	struct vb2_secdata *sec = (struct vb2_secdata *)ctx->secdata;

	/* Clear the entire struct */
	memset(sec, 0, sizeof(*sec));

	/* Set to current version */
	sec->struct_version = VB2_SECDATA_VERSION;

	/* Calculate initial CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdata, crc8));
	ctx->flags |= VB2_CONTEXT_SECDATA_CHANGED;
	return VB2_SUCCESS;
}

int vb2_secdata_init(struct vb2_context *ctx)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	int rv;

	rv = vb2_secdata_check_crc(ctx);
	if (rv)
		return rv;

	/* Set status flag */
	sd->status |= VB2_SD_STATUS_SECDATA_INIT;
	/* TODO: unit test for that */

	/* Read this now to make sure crossystem has it even in rec mode. */
	rv = vb2_secdata_get(ctx, VB2_SECDATA_VERSIONS,
			     &sd->fw_version_secdata);
	if (rv)
		return rv;

	return VB2_SUCCESS;
}

int vb2_secdata_get(struct vb2_context *ctx,
		    enum vb2_secdata_param param,
		    uint32_t *dest)
{
	struct vb2_secdata *sec = (struct vb2_secdata *)ctx->secdata;

	if (!(vb2_get_sd(ctx)->status & VB2_SD_STATUS_SECDATA_INIT))
		return VB2_ERROR_SECDATA_GET_UNINITIALIZED;

	switch(param) {
	case VB2_SECDATA_FLAGS:
		*dest = sec->flags;
		return VB2_SUCCESS;

	case VB2_SECDATA_VERSIONS:
		*dest = sec->fw_versions;
		return VB2_SUCCESS;

	default:
		return VB2_ERROR_SECDATA_GET_PARAM;
	}
}

int vb2_secdata_set(struct vb2_context *ctx,
		    enum vb2_secdata_param param,
		    uint32_t value)
{
	struct vb2_secdata *sec = (struct vb2_secdata *)ctx->secdata;
	uint32_t now;

	if (!(vb2_get_sd(ctx)->status & VB2_SD_STATUS_SECDATA_INIT))
		return VB2_ERROR_SECDATA_SET_UNINITIALIZED;

	/* If not changing the value, don't regenerate the CRC. */
	if (vb2_secdata_get(ctx, param, &now) == VB2_SUCCESS && now == value)
		return VB2_SUCCESS;

	switch(param) {
	case VB2_SECDATA_FLAGS:
		/* Make sure flags is in valid range */
		if (value > 0xff)
			return VB2_ERROR_SECDATA_SET_FLAGS;

		sec->flags = value;
		break;

	case VB2_SECDATA_VERSIONS:
		sec->fw_versions = value;
		break;

	default:
		return VB2_ERROR_SECDATA_SET_PARAM;
	}

	/* Regenerate CRC */
	sec->crc8 = vb2_crc8(sec, offsetof(struct vb2_secdata, crc8));
	ctx->flags |= VB2_CONTEXT_SECDATA_CHANGED;
	return VB2_SUCCESS;
}
