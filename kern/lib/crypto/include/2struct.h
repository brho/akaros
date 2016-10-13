/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Data structure definitions for verified boot, for on-disk / in-eeprom
 * data.
 */

#ifndef VBOOT_REFERENCE_VBOOT_2STRUCT_H_
#define VBOOT_REFERENCE_VBOOT_2STRUCT_H_
#include <stdint.h>
#include "2crypto.h"

/*
 * Key block flags.
 *
 *The following flags set where the key is valid.  Not used by firmware
 * verification; only kernel verification.
 */
#define VB2_KEY_BLOCK_FLAG_DEVELOPER_0  0x01 /* Developer switch off */
#define VB2_KEY_BLOCK_FLAG_DEVELOPER_1  0x02 /* Developer switch on */
#define VB2_KEY_BLOCK_FLAG_RECOVERY_0   0x04 /* Not recovery mode */
#define VB2_KEY_BLOCK_FLAG_RECOVERY_1   0x08 /* Recovery mode */
#define VB2_GBB_HWID_DIGEST_SIZE	32

/****************************************************************************/

/* Flags for vb2_shared_data.flags */
enum vb2_shared_data_flags {
	/* User has explicitly and physically requested recovery */
	VB2_SD_FLAG_MANUAL_RECOVERY = (1 << 0),

	/* Developer mode is enabled */
	/* TODO: should have been VB2_SD_FLAG_DEV_MODE_ENABLED */
	VB2_SD_DEV_MODE_ENABLED = (1 << 1),

	/*
	 * TODO: might be nice to add flags for why dev mode is enabled - via
	 * gbb, virtual dev switch, or forced on for testing.
	 */

	/* Kernel keyblock was verified by signature (not just hash) */
	VB2_SD_FLAG_KERNEL_SIGNED = (1 << 2),
};

/* Flags for vb2_shared_data.status */
enum vb2_shared_data_status {
	/* Reinitialized NV data due to invalid checksum */
	VB2_SD_STATUS_NV_REINIT = (1 << 0),

	/* NV data has been initialized */
	VB2_SD_STATUS_NV_INIT = (1 << 1),

	/* Secure data initialized */
	VB2_SD_STATUS_SECDATA_INIT = (1 << 2),

	/* Chose a firmware slot */
	VB2_SD_STATUS_CHOSE_SLOT = (1 << 3),

	/* Secure data kernel version space initialized */
	VB2_SD_STATUS_SECDATAK_INIT = (1 << 4),
};

/*
 * Data shared between vboot API calls.  Stored at the start of the work
 * buffer.
 */
struct vb2_shared_data {
	/* Flags; see enum vb2_shared_data_flags */
	uint32_t flags;

	/* Flags from GBB header */
	uint32_t gbb_flags;

	/*
	 * Reason we are in recovery mode this boot (enum vb2_nv_recovery), or
	 * 0 if we aren't.
	 */
	uint32_t recovery_reason;

	/* Firmware slot used last boot (0=A, 1=B) */
	uint32_t last_fw_slot;

	/* Result of last boot (enum vb2_fw_result) */
	uint32_t last_fw_result;

	/* Firmware slot used this boot */
	uint32_t fw_slot;

	/*
	 * Version for this slot (top 16 bits = key, lower 16 bits = firmware).
	 *
	 * TODO: Make this a union to allow getting/setting those versions
	 * separately?
	 */
	uint32_t fw_version;

	/* Version stored in secdata (must be <= fw_version to boot). */
	uint32_t fw_version_secdata;

	/*
	 * Status flags for this boot; see enum vb2_shared_data_status.  Status
	 * is "what we've done"; flags above are "decisions we've made".
	 */
	uint32_t status;

	/**********************************************************************
	 * Data from kernel verification stage.
	 *
	 * TODO: shouldn't be part of the main struct, since that needlessly
	 * uses more memory during firmware verification.
	 */

	/*
	 * Version for the current kernel (top 16 bits = key, lower 16 bits =
	 * kernel preamble).
	 *
	 * TODO: Make this a union to allow getting/setting those versions
	 * separately?
	 */
	uint32_t kernel_version;

	/* Kernel version from secdatak (must be <= kernel_version to boot) */
	uint32_t kernel_version_secdatak;

	/**********************************************************************
	 * Temporary variables used during firmware verification.  These don't
	 * really need to persist through to the OS, but there's nowhere else
	 * we can put them.
	 */

	/* Root key offset and size from GBB header */
	uint32_t gbb_rootkey_offset;
	uint32_t gbb_rootkey_size;

	/* HWID digest from GBB header */
	uint8_t gbb_hwid_digest[VB2_GBB_HWID_DIGEST_SIZE];

	/* Offset of preamble from start of vblock */
	uint32_t vblock_preamble_offset;

	/*
	 * Offset and size of packed data key in work buffer.  Size is 0 if
	 * data key is not stored in the work buffer.
	 */
	uint32_t workbuf_data_key_offset;
	uint32_t workbuf_data_key_size;

	/*
	 * Offset and size of firmware preamble in work buffer.  Size is 0 if
	 * preamble is not stored in the work buffer.
	 */
	uint32_t workbuf_preamble_offset;
	uint32_t workbuf_preamble_size;

	/*
	 * Offset and size of hash context in work buffer.  Size is 0 if
	 * hash context is not stored in the work buffer.
	 */
	uint32_t workbuf_hash_offset;
	uint32_t workbuf_hash_size;

	/*
	 * Current tag we're hashing
	 *
	 * For new structs, this is the offset of the vb2_signature struct
	 * in the work buffer.
	 *
	 * TODO: rename to workbuf_hash_sig_offset when vboot1 structs are
	 * deprecated.
	 */
	uint32_t hash_tag;

	/* Amount of data we still expect to hash */
	uint32_t hash_remaining_size;

	/**********************************************************************
	 * Temporary variables used during kernel verification.  These don't
	 * really need to persist through to the OS, but there's nowhere else
	 * we can put them.
	 *
	 * TODO: make a union with the firmware verification temp variables,
	 * or make both of them workbuf-allocated sub-structs, so that we can
	 * overlap them so kernel variables don't bloat firmware verification
	 * stage memory requirements.
	 */

	/*
	 * Offset and size of packed kernel key in work buffer.  Size is 0 if
	 * subkey is not stored in the work buffer.  Note that kernel key may
	 * be inside the firmware preamble.
	 */
	uint32_t workbuf_kernel_key_offset;
	uint32_t workbuf_kernel_key_size;

} __attribute__((packed));

/****************************************************************************/

/* Signature at start of the GBB
 * Note that if you compile in the signature as is, you are likely to break any
 * tools that search for the signature. */
#define VB2_GBB_SIGNATURE "$GBB"
#define VB2_GBB_SIGNATURE_SIZE 4
#define VB2_GBB_XOR_CHARS "****"
/* TODO: can we write a macro to produce this at compile time? */
#define VB2_GBB_XOR_SIGNATURE { 0x0e, 0x6d, 0x68, 0x68 }

/* VB2 GBB struct version */
#define VB2_GBB_MAJOR_VER      1
#define VB2_GBB_MINOR_VER      2
/* v1.2 - added fields for sha256 digest of the HWID */

/* Flags for vb2_gbb_header.flags */
enum vb2_gbb_flag {
	/*
	 * Reduce the dev screen delay to 2 sec from 30 sec to speed up
	 * factory.
	 */
	VB2_GBB_FLAG_DEV_SCREEN_SHORT_DELAY = (1 << 0),

	/*
	 * BIOS should load option ROMs from arbitrary PCI devices. We'll never
	 * enable this ourselves because it executes non-verified code, but if
	 * a customer wants to void their warranty and set this flag in the
	 * read-only flash, they should be able to do so.
	 *
	 * (TODO: Currently not supported. Mark as deprecated/unused?)
	 */
	VB2_GBB_FLAG_LOAD_OPTION_ROMS = (1 << 1),

	/*
	 * The factory flow may need the BIOS to boot a non-ChromeOS kernel if
	 * the dev-switch is on. This flag allows that.
	 *
	 * (TODO: Currently not supported. Mark as deprecated/unused?)
	 */
	VB2_GBB_FLAG_ENABLE_ALTERNATE_OS = (1 << 2),

	/*
	 * Force dev switch on, regardless of physical/keyboard dev switch
	 * position.
	 */
	VB2_GBB_FLAG_FORCE_DEV_SWITCH_ON = (1 << 3),

	/* Allow booting from USB in dev mode even if dev_boot_usb=0. */
	VB2_GBB_FLAG_FORCE_DEV_BOOT_USB = (1 << 4),

	/* Disable firmware rollback protection. */
	VB2_GBB_FLAG_DISABLE_FW_ROLLBACK_CHECK = (1 << 5),

	/* Allow Enter key to trigger dev->tonorm screen transition */
	VB2_GBB_FLAG_ENTER_TRIGGERS_TONORM = (1 << 6),

	/* Allow booting Legacy OSes in dev mode even if dev_boot_legacy=0. */
	VB2_GBB_FLAG_FORCE_DEV_BOOT_LEGACY = (1 << 7),

	/* Allow booting using alternate keys for FAFT servo testing */
	VB2_GBB_FLAG_FAFT_KEY_OVERIDE = (1 << 8),

	/* Disable EC software sync */
	VB2_GBB_FLAG_DISABLE_EC_SOFTWARE_SYNC = (1 << 9),

	/* Default to booting legacy OS when dev screen times out */
	VB2_GBB_FLAG_DEFAULT_DEV_BOOT_LEGACY = (1 << 10),

	/* Disable PD software sync */
	VB2_GBB_FLAG_DISABLE_PD_SOFTWARE_SYNC = (1 << 11),

	/* Disable shutdown on lid closed */
	VB2_GBB_FLAG_DISABLE_LID_SHUTDOWN = (1 << 12),

	/*
	 * Allow full fastboot capability in firmware even if
	 * dev_boot_fastboot_full_cap=0.
	 */
	VB2_GBB_FLAG_FORCE_DEV_BOOT_FASTBOOT_FULL_CAP = (1 << 13),

	/* Enable serial */
	VB2_GBB_FLAG_ENABLE_SERIAL = (1 << 14),
};

struct vb2_gbb_header {
	/* Fields present in version 1.1 */
	uint8_t  signature[VB2_GBB_SIGNATURE_SIZE]; /* VB2_GBB_SIGNATURE */
	uint16_t major_version;   /* See VB2_GBB_MAJOR_VER */
	uint16_t minor_version;   /* See VB2_GBB_MINOR_VER */
	uint32_t header_size;     /* Size of GBB header in bytes */
	uint32_t flags;           /* Flags (see enum vb2_gbb_flag) */

	/* Offsets (from start of header) and sizes (in bytes) of components */
	uint32_t hwid_offset;		/* HWID */
	uint32_t hwid_size;
	uint32_t rootkey_offset;	/* Root key */
	uint32_t rootkey_size;
	uint32_t bmpfv_offset;		/* BMP FV */
	uint32_t bmpfv_size;
	uint32_t recovery_key_offset;	/* Recovery key */
	uint32_t recovery_key_size;

	/* Added in version 1.2 */
	uint8_t  hwid_digest[VB2_GBB_HWID_DIGEST_SIZE];	/* SHA-256 of HWID */

	/* Pad to match EXPECETED_VB2_GBB_HEADER_SIZE.  Initialize to 0. */
	uint8_t  pad[48];
} __attribute__((packed));

/* The GBB is used outside of vboot_reference, so this size is important. */
#define EXPECTED_VB2_GBB_HEADER_SIZE 128

/*
 * Root key hash for Ryu devices only.  Contains the hash of the root key.
 * This will be embedded somewhere inside the RO part of the firmware, so that
 * it can verify the GBB contains only the official root key.
 */

#define RYU_ROOT_KEY_HASH_MAGIC "RtKyHash"
#define RYU_ROOT_KEY_HASH_MAGIC_INVCASE "rTkYhASH"
#define RYU_ROOT_KEY_HASH_MAGIC_SIZE 8

#define RYU_ROOT_KEY_HASH_VERSION_MAJOR 1
#define RYU_ROOT_KEY_HASH_VERSION_MINOR 0

struct vb2_ryu_root_key_hash {
	/* Magic number (RYU_ROOT_KEY_HASH_MAGIC) */
	uint8_t magic[RYU_ROOT_KEY_HASH_MAGIC_SIZE];

	/* Version of this struct */
	uint16_t header_version_major;
	uint16_t header_version_minor;

	/*
	 * Length of this struct, in bytes, including any variable length data
	 * which follows (there is none, yet).
	 */
	uint32_t struct_size;

	/*
	 * SHA-256 hash digest of the entire root key section from the GBB.  If
	 * all 0 bytes, all root keys will be treated as if matching.
	 */
	uint8_t root_key_hash_digest[32];
};

#define EXPECTED_VB2_RYU_ROOT_KEY_HASH_SIZE 48

#endif  /* VBOOT_REFERENCE_VBOOT_2STRUCT_H_ */
