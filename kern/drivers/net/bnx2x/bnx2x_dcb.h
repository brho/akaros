/* bnx2x_dcb.h: Broadcom Everest network driver.
 *
 * Copyright 2009-2013 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Dmitry Kravkov
 *
 */
#ifndef BNX2X_DCB_H
#define BNX2X_DCB_H

#include "bnx2x_hsi.h"

#define LLFC_DRIVER_TRAFFIC_TYPE_MAX 3 /* NW, iSCSI, FCoE */
struct bnx2x_dcbx_app_params {
	uint32_t enabled;
	uint32_t traffic_type_priority[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
};

#define DCBX_COS_MAX_NUM_E2	DCBX_E2E3_MAX_NUM_COS
/* bnx2x currently limits numbers of supported COSes to 3 to be extended to 6 */
#define BNX2X_MAX_COS_SUPPORT	3
#define DCBX_COS_MAX_NUM_E3B0	BNX2X_MAX_COS_SUPPORT
#define DCBX_COS_MAX_NUM	BNX2X_MAX_COS_SUPPORT

struct bnx2x_dcbx_cos_params {
	uint32_t	bw_tbl;
	uint32_t	pri_bitmask;
	/*
	 * strict priority: valid values are 0..5; 0 is highest priority.
	 * There can't be two COSes with the same priority.
	 */
	uint8_t	strict;
#define BNX2X_DCBX_STRICT_INVALID			DCBX_COS_MAX_NUM
#define BNX2X_DCBX_STRICT_COS_HIGHEST			0
#define BNX2X_DCBX_STRICT_COS_NEXT_LOWER_PRI(sp)	((sp) + 1)
	uint8_t	pauseable;
};

struct bnx2x_dcbx_pg_params {
	uint32_t enabled;
	uint8_t num_of_cos; /* valid COS entries */
	struct bnx2x_dcbx_cos_params	cos_params[DCBX_COS_MAX_NUM];
};

struct bnx2x_dcbx_pfc_params {
	uint32_t enabled;
	uint32_t priority_non_pauseable_mask;
};

struct bnx2x_dcbx_port_params {
	struct bnx2x_dcbx_pfc_params pfc;
	struct bnx2x_dcbx_pg_params  ets;
	struct bnx2x_dcbx_app_params app;
};

#define BNX2X_DCBX_CONFIG_INV_VALUE			(0xFFFFFFFF)
#define BNX2X_DCBX_OVERWRITE_SETTINGS_DISABLE		0
#define BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE		1
#define BNX2X_DCBX_OVERWRITE_SETTINGS_INVALID	(BNX2X_DCBX_CONFIG_INV_VALUE)
#define BNX2X_IS_ETS_ENABLED(bp) ((bp)->dcb_state == BNX2X_DCB_STATE_ON &&\
				  (bp)->dcbx_port_params.ets.enabled)

struct bnx2x_config_lldp_params {
	uint32_t overwrite_settings;
	uint32_t msg_tx_hold;
	uint32_t msg_fast_tx;
	uint32_t tx_credit_max;
	uint32_t msg_tx_interval;
	uint32_t tx_fast;
};

struct bnx2x_admin_priority_app_table {
		uint32_t valid;
		uint32_t priority;
#define INVALID_TRAFFIC_TYPE_PRIORITY	(0xFFFFFFFF)
		uint32_t traffic_type;
#define TRAFFIC_TYPE_ETH		0
#define TRAFFIC_TYPE_PORT		1
		uint32_t app_id;
};

#define DCBX_CONFIG_MAX_APP_PROTOCOL 4
struct bnx2x_config_dcbx_params {
	uint32_t overwrite_settings;
	uint32_t admin_dcbx_version;
	uint32_t admin_ets_enable;
	uint32_t admin_pfc_enable;
	uint32_t admin_tc_supported_tx_enable;
	uint32_t admin_ets_configuration_tx_enable;
	uint32_t admin_ets_recommendation_tx_enable;
	uint32_t admin_pfc_tx_enable;
	uint32_t admin_application_priority_tx_enable;
	uint32_t admin_ets_willing;
	uint32_t admin_ets_reco_valid;
	uint32_t admin_pfc_willing;
	uint32_t admin_app_priority_willing;
	uint32_t admin_configuration_bw_precentage[8];
	uint32_t admin_configuration_ets_pg[8];
	uint32_t admin_recommendation_bw_precentage[8];
	uint32_t admin_recommendation_ets_pg[8];
	uint32_t admin_pfc_bitmap;
	struct bnx2x_admin_priority_app_table
		admin_priority_app_table[DCBX_CONFIG_MAX_APP_PROTOCOL];
	uint32_t admin_default_priority;
};

#define GET_FLAGS(flags, bits)		((flags) & (bits))
#define SET_FLAGS(flags, bits)		((flags) |= (bits))
#define RESET_FLAGS(flags, bits)	((flags) &= ~(bits))

enum {
	DCBX_READ_LOCAL_MIB,
	DCBX_READ_REMOTE_MIB
};

#define ETH_TYPE_FCOE		(0x8906)
#define TCP_PORT_ISCSI		(0xCBC)

#define PFC_VALUE_FRAME_SIZE				(512)
#define PFC_QUANTA_IN_NANOSEC_FROM_SPEED_MEGA(mega_speed)  \
				((1000 * PFC_VALUE_FRAME_SIZE)/(mega_speed))

#define PFC_BRB1_REG_HIGH_LLFC_LOW_THRESHOLD			130
#define PFC_BRB1_REG_HIGH_LLFC_HIGH_THRESHOLD			170

struct cos_entry_help_data {
	uint32_t			pri_join_mask;
	uint32_t			cos_bw;
	uint8_t			strict;
	bool			pausable;
};

struct cos_help_data {
	struct cos_entry_help_data	data[DCBX_COS_MAX_NUM];
	uint8_t				num_of_cos;
};

#define DCBX_ILLEGAL_PG				(0xFF)
#define DCBX_PFC_PRI_MASK			(0xFF)
#define DCBX_STRICT_PRIORITY			(15)
#define DCBX_INVALID_COS_BW			(0xFFFFFFFF)
#define DCBX_PFC_PRI_NON_PAUSE_MASK(bp)		\
			((bp)->dcbx_port_params.pfc.priority_non_pauseable_mask)
#define DCBX_PFC_PRI_PAUSE_MASK(bp)		\
					((uint8_t)~DCBX_PFC_PRI_NON_PAUSE_MASK(bp))
#define DCBX_PFC_PRI_GET_PAUSE(bp, pg_pri)	\
				((pg_pri) & (DCBX_PFC_PRI_PAUSE_MASK(bp)))
#define DCBX_PFC_PRI_GET_NON_PAUSE(bp, pg_pri)	\
			(DCBX_PFC_PRI_NON_PAUSE_MASK(bp) & (pg_pri))
#define DCBX_IS_PFC_PRI_SOME_PAUSE(bp, pg_pri)	\
			(0 != DCBX_PFC_PRI_GET_PAUSE(bp, pg_pri))
#define IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pg_pri)	\
			(pg_pri == DCBX_PFC_PRI_GET_PAUSE((bp), (pg_pri)))
#define IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(bp, pg_pri)\
			((pg_pri) == DCBX_PFC_PRI_GET_NON_PAUSE((bp), (pg_pri)))
#define IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pg_pri)	\
			(!(IS_DCBX_PFC_PRI_ONLY_NON_PAUSE((bp), (pg_pri)) || \
			 IS_DCBX_PFC_PRI_ONLY_PAUSE((bp), (pg_pri))))

struct pg_entry_help_data {
	uint8_t	num_of_dif_pri;
	uint8_t	pg;
	uint32_t	pg_priority;
};

struct pg_help_data {
	struct pg_entry_help_data	data[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
	uint8_t				num_of_pg;
};

/* forward DCB/PFC related declarations */
struct bnx2x;
void bnx2x_dcbx_update(struct work_struct *work);
void bnx2x_dcbx_init_params(struct bnx2x *bp);
void bnx2x_dcbx_set_state(struct bnx2x *bp, bool dcb_on,
			  uint32_t dcbx_enabled);

enum {
	BNX2X_DCBX_STATE_NEG_RECEIVED = 0x1,
	BNX2X_DCBX_STATE_TX_PAUSED,
	BNX2X_DCBX_STATE_TX_RELEASED
};

void bnx2x_dcbx_set_params(struct bnx2x *bp, uint32_t state);
void bnx2x_dcbx_pmf_update(struct bnx2x *bp);
/* DCB netlink */
#ifdef BCM_DCBNL
extern const struct dcbnl_rtnl_ops bnx2x_dcbnl_ops;
int bnx2x_dcbnl_update_applist(struct bnx2x *bp, bool delall);
#endif /* BCM_DCBNL */

int bnx2x_dcbx_stop_hw_tx(struct bnx2x *bp);
int bnx2x_dcbx_resume_hw_tx(struct bnx2x *bp);

#endif /* BNX2X_DCB_H */
