/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX4_FW_H
#define MLX4_FW_H

#include "mlx4.h"
#include "icm.h"

struct mlx4_mod_stat_cfg {
	uint8_t log_pg_sz;
	uint8_t log_pg_sz_m;
};

struct mlx4_port_cap {
	uint8_t  supported_port_types;
	uint8_t  suggested_type;
	uint8_t  default_sense;
	uint8_t  log_max_macs;
	uint8_t  log_max_vlans;
	int ib_mtu;
	int max_port_width;
	int max_vl;
	int max_gids;
	int max_pkeys;
	uint64_t def_mac;
	uint16_t eth_mtu;
	int trans_type;
	int vendor_oui;
	uint16_t wavelength;
	uint64_t trans_code;
	uint8_t dmfs_optimized_state;
};

struct mlx4_dev_cap {
	int max_srq_sz;
	int max_qp_sz;
	int reserved_qps;
	int max_qps;
	int reserved_srqs;
	int max_srqs;
	int max_cq_sz;
	int reserved_cqs;
	int max_cqs;
	int max_mpts;
	int reserved_eqs;
	int max_eqs;
	int num_sys_eqs;
	int reserved_mtts;
	int max_mrw_sz;
	int reserved_mrws;
	int max_mtt_seg;
	int max_requester_per_qp;
	int max_responder_per_qp;
	int max_rdma_global;
	int local_ca_ack_delay;
	int num_ports;
	uint32_t max_msg_sz;
	uint16_t stat_rate_support;
	int fs_log_max_ucast_qp_range_size;
	int fs_max_num_qp_per_entry;
	uint64_t flags;
	uint64_t flags2;
	int reserved_uars;
	int uar_size;
	int min_page_sz;
	int bf_reg_size;
	int bf_regs_per_page;
	int max_sq_sg;
	int max_sq_desc_sz;
	int max_rq_sg;
	int max_rq_desc_sz;
	int max_qp_per_mcg;
	int reserved_mgms;
	int max_mcgs;
	int reserved_pds;
	int max_pds;
	int reserved_xrcds;
	int max_xrcds;
	int qpc_entry_sz;
	int rdmarc_entry_sz;
	int altc_entry_sz;
	int aux_entry_sz;
	int srq_entry_sz;
	int cqc_entry_sz;
	int eqc_entry_sz;
	int dmpt_entry_sz;
	int cmpt_entry_sz;
	int mtt_entry_sz;
	int resize_srq;
	uint32_t bmme_flags;
	uint32_t reserved_lkey;
	uint64_t max_icm_sz;
	int max_gso_sz;
	int max_rss_tbl_sz;
	uint32_t max_counters;
	uint32_t dmfs_high_rate_qpn_base;
	uint32_t dmfs_high_rate_qpn_range;
	struct mlx4_rate_limit_caps rl_caps;
	struct mlx4_port_cap port_cap[MLX4_MAX_PORTS + 1];
};

struct mlx4_func_cap {
	uint8_t	num_ports;
	uint8_t	flags;
	uint32_t	pf_context_behaviour;
	int	qp_quota;
	int	cq_quota;
	int	srq_quota;
	int	mpt_quota;
	int	mtt_quota;
	int	max_eq;
	int	reserved_eq;
	int	mcg_quota;
	uint32_t	qp0_qkey;
	uint32_t	qp0_tunnel_qpn;
	uint32_t	qp0_proxy_qpn;
	uint32_t	qp1_tunnel_qpn;
	uint32_t	qp1_proxy_qpn;
	uint32_t	reserved_lkey;
	uint8_t	physical_port;
	uint8_t	port_flags;
	uint8_t	flags1;
	uint64_t	phys_port_id;
	uint32_t	extra_flags;
};

struct mlx4_func {
	int	bus;
	int	device;
	int	function;
	int	physical_function;
	int	rsvd_eqs;
	int	max_eq;
	int	rsvd_uars;
};

struct mlx4_adapter {
	char board_id[MLX4_BOARD_ID_LEN];
	uint8_t   inta_pin;
};

struct mlx4_init_hca_param {
	uint64_t qpc_base;
	uint64_t rdmarc_base;
	uint64_t auxc_base;
	uint64_t altc_base;
	uint64_t srqc_base;
	uint64_t cqc_base;
	uint64_t eqc_base;
	uint64_t mc_base;
	uint64_t dmpt_base;
	uint64_t cmpt_base;
	uint64_t mtt_base;
	uint64_t global_caps;
	uint16_t log_mc_entry_sz;
	uint16_t log_mc_hash_sz;
	uint16_t hca_core_clock; /* Internal Clock Frequency (in MHz) */
	uint8_t  log_num_qps;
	uint8_t  log_num_srqs;
	uint8_t  log_num_cqs;
	uint8_t  log_num_eqs;
	uint16_t num_sys_eqs;
	uint8_t  log_rd_per_qp;
	uint8_t  log_mc_table_sz;
	uint8_t  log_mpt_sz;
	uint8_t  log_uar_sz;
	uint8_t  mw_enabled;  /* Enable memory windows */
	uint8_t  uar_page_sz; /* log pg sz in 4k chunks */
	uint8_t  steering_mode; /* for QUERY_HCA */
	uint8_t  dmfs_high_steer_mode; /* for QUERY_HCA */
	uint64_t dev_cap_enabled;
	uint16_t cqe_size; /* For use only when CQE stride feature enabled */
	uint16_t eqe_size; /* For use only when EQE stride feature enabled */
	uint8_t rss_ip_frags;
};

struct mlx4_init_ib_param {
	int port_width;
	int vl_cap;
	int mtu_cap;
	uint16_t gid_cap;
	uint16_t pkey_cap;
	int set_guid0;
	uint64_t guid0;
	int set_node_guid;
	uint64_t node_guid;
	int set_si_guid;
	uint64_t si_guid;
};

struct mlx4_set_ib_param {
	int set_si_guid;
	int reset_qkey_viol;
	uint64_t si_guid;
	uint32_t cap_mask;
};

void mlx4_dev_cap_dump(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap);
int mlx4_QUERY_DEV_CAP(struct mlx4_dev *dev, struct mlx4_dev_cap *dev_cap);
int mlx4_QUERY_PORT(struct mlx4_dev *dev, int port, struct mlx4_port_cap *port_cap);
int mlx4_QUERY_FUNC_CAP(struct mlx4_dev *dev, uint8_t gen_or_port,
			struct mlx4_func_cap *func_cap);
int mlx4_QUERY_FUNC_CAP_wrapper(struct mlx4_dev *dev, int slave,
				struct mlx4_vhcr *vhcr,
				struct mlx4_cmd_mailbox *inbox,
				struct mlx4_cmd_mailbox *outbox,
				struct mlx4_cmd_info *cmd);
int mlx4_QUERY_FUNC(struct mlx4_dev *dev, struct mlx4_func *func, int slave);
int mlx4_MAP_FA(struct mlx4_dev *dev, struct mlx4_icm *icm);
int mlx4_UNMAP_FA(struct mlx4_dev *dev);
int mlx4_RUN_FW(struct mlx4_dev *dev);
int mlx4_QUERY_FW(struct mlx4_dev *dev);
int mlx4_QUERY_ADAPTER(struct mlx4_dev *dev, struct mlx4_adapter *adapter);
int mlx4_INIT_HCA(struct mlx4_dev *dev, struct mlx4_init_hca_param *param);
int mlx4_QUERY_HCA(struct mlx4_dev *dev, struct mlx4_init_hca_param *param);
int mlx4_CLOSE_HCA(struct mlx4_dev *dev, int panic);
int mlx4_map_cmd(struct mlx4_dev *dev, uint16_t op, struct mlx4_icm *icm,
		 uint64_t virt);
int mlx4_SET_ICM_SIZE(struct mlx4_dev *dev, uint64_t icm_size,
		      uint64_t *aux_pages);
int mlx4_MAP_ICM_AUX(struct mlx4_dev *dev, struct mlx4_icm *icm);
int mlx4_UNMAP_ICM_AUX(struct mlx4_dev *dev);
int mlx4_NOP(struct mlx4_dev *dev);
int mlx4_MOD_STAT_CFG(struct mlx4_dev *dev, struct mlx4_mod_stat_cfg *cfg);
void mlx4_opreq_action(struct work_struct *work);

#endif /* MLX4_FW_H */
