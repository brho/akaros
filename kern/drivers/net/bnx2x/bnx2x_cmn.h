/* bnx2x_cmn.h: Broadcom Everest network driver.
 *
 * Copyright (c) 2007-2013 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Maintained by: Ariel Elior <ariel.elior@qlogic.com>
 * Written by: Eliezer Tamir
 * Based on code from Michael Chan's bnx2 driver
 * UDP CSUM errata workaround by Arik Gendelman
 * Slowpath and fastpath rework by Vladislav Zolotarov
 * Statistics and Link management by Yitchak Gertner
 *
 */
#pragma once

#include <linux_compat.h>

#include "bnx2x.h"
#include "bnx2x_sriov.h"

/* This is used as a replacement for an MCP if it's not present */
extern int bnx2x_load_count[2][3]; /* per-path: 0-common, 1-port0, 2-port1 */
extern int bnx2x_num_queues;

/************************ Macros ********************************/
#define BNX2X_PCI_FREE(x, y, size) \
	do { \
		if (x) { \
			dma_free_coherent(&bp->pdev->linux_dev, size, (void *)x, y); \
			x = NULL; \
			y = 0; \
		} \
	} while (0)

#define BNX2X_FREE(x) \
	do { \
		if (x) { \
			kfree((void *)x); \
			x = NULL; \
		} \
	} while (0)

#define BNX2X_PCI_ALLOC(y, size)					\
({									\
	void *x = dma_zalloc_coherent(&bp->pdev->linux_dev, size, y, MEM_WAIT); \
	if (x)								\
		DP(NETIF_MSG_HW,					\
		   "BNX2X_PCI_ALLOC: Physical %p Virtual %p\n",	\
		   (unsigned long long)(*y), x);			\
	x;								\
})
#define BNX2X_PCI_FALLOC(y, size)					\
({									\
	void *x = dma_alloc_coherent(&bp->pdev->linux_dev, size, y, MEM_WAIT); \
	if (x) {							\
		memset(x, 0xff, size);					\
		DP(NETIF_MSG_HW,					\
		   "BNX2X_PCI_FALLOC: Physical %p Virtual %p\n",	\
		   (unsigned long long)(*y), x);			\
	}								\
	x;								\
})

/*********************** Interfaces ****************************
 *  Functions that need to be implemented by each driver version
 */
/* Init */

/**
 * bnx2x_send_unload_req - request unload mode from the MCP.
 *
 * @bp:			driver handle
 * @unload_mode:	requested function's unload mode
 *
 * Return unload mode returned by the MCP: COMMON, PORT or FUNC.
 */
uint32_t bnx2x_send_unload_req(struct bnx2x *bp, int unload_mode);

/**
 * bnx2x_send_unload_done - send UNLOAD_DONE command to the MCP.
 *
 * @bp:		driver handle
 * @keep_link:		true iff link should be kept up
 */
void bnx2x_send_unload_done(struct bnx2x *bp, bool keep_link);

/**
 * bnx2x_config_rss_pf - configure RSS parameters in a PF.
 *
 * @bp:			driver handle
 * @rss_obj:		RSS object to use
 * @ind_table:		indirection table to configure
 * @config_hash:	re-configure RSS hash keys configuration
 * @enable:		enabled or disabled configuration
 */
int bnx2x_rss(struct bnx2x *bp, struct bnx2x_rss_config_obj *rss_obj,
	      bool config_hash, bool enable);

/**
 * bnx2x__init_func_obj - init function object
 *
 * @bp:			driver handle
 *
 * Initializes the Function Object with the appropriate
 * parameters which include a function slow path driver
 * interface.
 */
void bnx2x__init_func_obj(struct bnx2x *bp);

/**
 * bnx2x_setup_queue - setup eth queue.
 *
 * @bp:		driver handle
 * @fp:		pointer to the fastpath structure
 * @leading:	boolean
 *
 */
int bnx2x_setup_queue(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       bool leading);

/**
 * bnx2x_setup_leading - bring up a leading eth queue.
 *
 * @bp:		driver handle
 */
int bnx2x_setup_leading(struct bnx2x *bp);

/**
 * bnx2x_fw_command - send the MCP a request
 *
 * @bp:		driver handle
 * @command:	request
 * @param:	request's parameter
 *
 * block until there is a reply
 */
uint32_t bnx2x_fw_command(struct bnx2x *bp, uint32_t command, uint32_t param);

/**
 * bnx2x_initial_phy_init - initialize link parameters structure variables.
 *
 * @bp:		driver handle
 * @load_mode:	current mode
 */
int bnx2x_initial_phy_init(struct bnx2x *bp, int load_mode);

/**
 * bnx2x_link_set - configure hw according to link parameters structure.
 *
 * @bp:		driver handle
 */
void bnx2x_link_set(struct bnx2x *bp);

/**
 * bnx2x_force_link_reset - Forces link reset, and put the PHY
 * in reset as well.
 *
 * @bp:		driver handle
 */
void bnx2x_force_link_reset(struct bnx2x *bp);

/**
 * bnx2x_link_test - query link status.
 *
 * @bp:		driver handle
 * @is_serdes:	bool
 *
 * Returns 0 if link is UP.
 */
uint8_t bnx2x_link_test(struct bnx2x *bp, uint8_t is_serdes);

/**
 * bnx2x_drv_pulse - write driver pulse to shmem
 *
 * @bp:		driver handle
 *
 * writes the value in bp->fw_drv_pulse_wr_seq to drv_pulse mbox
 * in the shmem.
 */
void bnx2x_drv_pulse(struct bnx2x *bp);

/**
 * bnx2x_igu_ack_sb - update IGU with current SB value
 *
 * @bp:		driver handle
 * @igu_sb_id:	SB id
 * @segment:	SB segment
 * @index:	SB index
 * @op:		SB operation
 * @update:	is HW update required
 */
void bnx2x_igu_ack_sb(struct bnx2x *bp, uint8_t igu_sb_id, uint8_t segment,
		      uint16_t index, uint8_t op, uint8_t update);

/* Disable transactions from chip to host */
void bnx2x_pf_disable(struct bnx2x *bp);
int bnx2x_pretend_func(struct bnx2x *bp, uint16_t pretend_func_val);

/**
 * bnx2x__link_status_update - handles link status change.
 *
 * @bp:		driver handle
 */
void bnx2x__link_status_update(struct bnx2x *bp);

/**
 * bnx2x_link_report - report link status to upper layer.
 *
 * @bp:		driver handle
 */
void bnx2x_link_report(struct bnx2x *bp);

/* None-atomic version of bnx2x_link_report() */
void __bnx2x_link_report(struct bnx2x *bp);

/**
 * bnx2x_get_mf_speed - calculate MF speed.
 *
 * @bp:		driver handle
 *
 * Takes into account current linespeed and MF configuration.
 */
uint16_t bnx2x_get_mf_speed(struct bnx2x *bp);

/**
 * bnx2x_msix_sp_int - MSI-X slowpath interrupt handler
 *
 * @irq:		irq number
 * @dev_instance:	private instance
 */
void bnx2x_msix_sp_int(struct hw_trapframe *hw_tf, void *dev_instance);

/**
 * bnx2x_interrupt - non MSI-X interrupt handler
 *
 * @irq:		irq number
 * @dev_instance:	private instance
 */
void bnx2x_interrupt(struct hw_trapframe *hw_tf, void *dev_instance);

/**
 * bnx2x_cnic_notify - send command to cnic driver
 *
 * @bp:		driver handle
 * @cmd:	command
 */
int bnx2x_cnic_notify(struct bnx2x *bp, int cmd);

/**
 * bnx2x_setup_cnic_irq_info - provides cnic with IRQ information
 *
 * @bp:		driver handle
 */
void bnx2x_setup_cnic_irq_info(struct bnx2x *bp);

/**
 * bnx2x_setup_cnic_info - provides cnic with updated info
 *
 * @bp:		driver handle
 */
void bnx2x_setup_cnic_info(struct bnx2x *bp);

/**
 * bnx2x_int_enable - enable HW interrupts.
 *
 * @bp:		driver handle
 */
void bnx2x_int_enable(struct bnx2x *bp);

/**
 * bnx2x_int_disable_sync - disable interrupts.
 *
 * @bp:		driver handle
 * @disable_hw:	true, disable HW interrupts.
 *
 * This function ensures that there are no
 * ISRs or SP DPCs (sp_task) are running after it returns.
 */
void bnx2x_int_disable_sync(struct bnx2x *bp, int disable_hw);

/**
 * bnx2x_nic_init_cnic - init driver internals for cnic.
 *
 * @bp:		driver handle
 * @load_code:	COMMON, PORT or FUNCTION
 *
 * Initializes:
 *  - rings
 *  - status blocks
 *  - etc.
 */
void bnx2x_nic_init_cnic(struct bnx2x *bp);

/**
 * bnx2x_preirq_nic_init - init driver internals.
 *
 * @bp:		driver handle
 *
 * Initializes:
 *  - fastpath object
 *  - fastpath rings
 *  etc.
 */
void bnx2x_pre_irq_nic_init(struct bnx2x *bp);

/**
 * bnx2x_postirq_nic_init - init driver internals.
 *
 * @bp:		driver handle
 * @load_code:	COMMON, PORT or FUNCTION
 *
 * Initializes:
 *  - status blocks
 *  - slowpath rings
 *  - etc.
 */
void bnx2x_post_irq_nic_init(struct bnx2x *bp, uint32_t load_code);
/**
 * bnx2x_alloc_mem_cnic - allocate driver's memory for cnic.
 *
 * @bp:		driver handle
 */
int bnx2x_alloc_mem_cnic(struct bnx2x *bp);
/**
 * bnx2x_alloc_mem - allocate driver's memory.
 *
 * @bp:		driver handle
 */
int bnx2x_alloc_mem(struct bnx2x *bp);

/**
 * bnx2x_free_mem_cnic - release driver's memory for cnic.
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem_cnic(struct bnx2x *bp);
/**
 * bnx2x_free_mem - release driver's memory.
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem(struct bnx2x *bp);

/**
 * bnx2x_set_num_queues - set number of queues according to mode.
 *
 * @bp:		driver handle
 */
void bnx2x_set_num_queues(struct bnx2x *bp);

/**
 * bnx2x_chip_cleanup - cleanup chip internals.
 *
 * @bp:			driver handle
 * @unload_mode:	COMMON, PORT, FUNCTION
 * @keep_link:		true iff link should be kept up.
 *
 * - Cleanup MAC configuration.
 * - Closes clients.
 * - etc.
 */
void bnx2x_chip_cleanup(struct bnx2x *bp, int unload_mode, bool keep_link);

/**
 * bnx2x_acquire_hw_lock - acquire HW lock.
 *
 * @bp:		driver handle
 * @resource:	resource bit which was locked
 */
int bnx2x_acquire_hw_lock(struct bnx2x *bp, uint32_t resource);

/**
 * bnx2x_release_hw_lock - release HW lock.
 *
 * @bp:		driver handle
 * @resource:	resource bit which was locked
 */
int bnx2x_release_hw_lock(struct bnx2x *bp, uint32_t resource);

/**
 * bnx2x_release_leader_lock - release recovery leader lock
 *
 * @bp:		driver handle
 */
int bnx2x_release_leader_lock(struct bnx2x *bp);

/**
 * bnx2x_set_eth_mac - configure eth MAC address in the HW
 *
 * @bp:		driver handle
 * @set:	set or clear
 *
 * Configures according to the value in netdev->dev_addr.
 */
int bnx2x_set_eth_mac(struct bnx2x *bp, bool set);

/**
 * bnx2x_set_rx_mode - set MAC filtering configurations.
 *
 * @dev:	netdevice
 *
 * called with netif_tx_lock from dev_mcast.c
 * If bp->state is OPEN, should be called with
 * netif_addr_lock_bh()
 */
void bnx2x_set_rx_mode_inner(struct bnx2x *bp);

/* Parity errors related */
void bnx2x_set_pf_load(struct bnx2x *bp);
bool bnx2x_clear_pf_load(struct bnx2x *bp);
bool bnx2x_chk_parity_attn(struct bnx2x *bp, bool *global, bool print);
bool bnx2x_reset_is_done(struct bnx2x *bp, int engine);
void bnx2x_set_reset_in_progress(struct bnx2x *bp);
void bnx2x_set_reset_global(struct bnx2x *bp);
void bnx2x_disable_close_the_gate(struct bnx2x *bp);
int bnx2x_init_hw_func_cnic(struct bnx2x *bp);

/**
 * bnx2x_sp_event - handle ramrods completion.
 *
 * @fp:		fastpath handle for the event
 * @rr_cqe:	eth_rx_cqe
 */
void bnx2x_sp_event(struct bnx2x_fastpath *fp, union eth_rx_cqe *rr_cqe);

/**
 * bnx2x_ilt_set_info - prepare ILT configurations.
 *
 * @bp:		driver handle
 */
void bnx2x_ilt_set_info(struct bnx2x *bp);

/**
 * bnx2x_ilt_set_cnic_info - prepare ILT configurations for SRC
 * and TM.
 *
 * @bp:		driver handle
 */
void bnx2x_ilt_set_info_cnic(struct bnx2x *bp);

/**
 * bnx2x_dcbx_init - initialize dcbx protocol.
 *
 * @bp:		driver handle
 */
void bnx2x_dcbx_init(struct bnx2x *bp, bool update_shmem);

/**
 * bnx2x_set_power_state - set power state to the requested value.
 *
 * @bp:		driver handle
 * @state:	required state D0 or D3hot
 *
 * Currently only D0 and D3hot are supported.
 */
//int bnx2x_set_power_state(struct bnx2x *bp, pci_power_t state);
#define bnx2x_set_power_state(...)

/**
 * bnx2x_update_max_mf_config - update MAX part of MF configuration in HW.
 *
 * @bp:		driver handle
 * @value:	new value
 */
void bnx2x_update_max_mf_config(struct bnx2x *bp, uint32_t value);
/* Error handling */
void bnx2x_fw_dump_lvl(struct bnx2x *bp, const char *lvl);

/* dev_close main block */
int bnx2x_nic_unload(struct bnx2x *bp, int unload_mode, bool keep_link);

/* dev_open main block */
int bnx2x_nic_load(struct bnx2x *bp, int load_mode);

/* setup_tc callback */
int bnx2x_setup_tc(struct ether *dev, uint8_t num_tc);

int bnx2x_get_vf_config(struct ether *dev, int vf,
			struct ifla_vf_info *ivi);
int bnx2x_set_vf_mac(struct ether *dev, int queue, uint8_t *mac);
int bnx2x_set_vf_vlan(struct ether *netdev, int vf, uint16_t vlan,
		      uint8_t qos);

/* select_queue callback */
uint16_t bnx2x_select_queue(struct ether *dev, struct sk_buff *skb,
		       void *accel_priv, select_queue_fallback_t fallback);

static inline void bnx2x_update_rx_prod(struct bnx2x *bp,
					struct bnx2x_fastpath *fp,
					uint16_t bd_prod,
					uint16_t rx_comp_prod,
					uint16_t rx_sge_prod)
{
	struct ustorm_eth_rx_producers rx_prods = {0};
	uint32_t i;

	/* Update producers */
	rx_prods.bd_prod = bd_prod;
	rx_prods.cqe_prod = rx_comp_prod;
	rx_prods.sge_prod = rx_sge_prod;

	/* Make sure that the BD and SGE data is updated before updating the
	 * producers since FW might read the BD/SGE right after the producer
	 * is updated.
	 * This is only applicable for weak-ordered memory model archs such
	 * as IA-64. The following barrier is also mandatory since FW will
	 * assumes BDs must have buffers.
	 */
	wmb();

	for (i = 0; i < sizeof(rx_prods)/4; i++)
		REG_WR(bp, fp->ustorm_rx_prods_offset + i*4,
		       ((uint32_t *)&rx_prods)[i]);

	bus_wmb(); /* keep prod updates ordered */

	DP(NETIF_MSG_RX_STATUS,
	   "queue[%d]:  wrote  bd_prod %u  cqe_prod %u  sge_prod %u\n",
	   fp->index, bd_prod, rx_comp_prod, rx_sge_prod);
}

/* reload helper */
int bnx2x_reload_if_running(struct ether *dev);

int bnx2x_change_mac_addr(struct ether *dev, void *p);

/* NAPI poll Tx part */
int bnx2x_tx_int(struct bnx2x *bp, struct bnx2x_fp_txdata *txdata);

/* suspend/resume callbacks */
int bnx2x_suspend(struct pci_device *pdev, pm_message_t state);
int bnx2x_resume(struct pci_device *pdev);

/* Release IRQ vectors */
void bnx2x_free_irq(struct bnx2x *bp);

void bnx2x_free_fp_mem(struct bnx2x *bp);
void bnx2x_init_rx_rings(struct bnx2x *bp);
void bnx2x_init_rx_rings_cnic(struct bnx2x *bp);
void bnx2x_free_skbs(struct bnx2x *bp);
void bnx2x_netif_stop(struct bnx2x *bp, int disable_hw);
void bnx2x_netif_start(struct bnx2x *bp);
int bnx2x_load_cnic(struct bnx2x *bp);

/**
 * bnx2x_enable_msix - set msix configuration.
 *
 * @bp:		driver handle
 *
 * fills msix_table, requests vectors, updates num_queues
 * according to number of available vectors.
 */
int bnx2x_enable_msix(struct bnx2x *bp);

/**
 * bnx2x_enable_msi - request msi mode from OS, updated internals accordingly
 *
 * @bp:		driver handle
 */
int bnx2x_enable_msi(struct bnx2x *bp);

/**
 * bnx2x_low_latency_recv - LL callback
 *
 * @napi:	napi structure
 */
int bnx2x_low_latency_recv(struct napi_struct *napi);

/**
 * bnx2x_alloc_mem_bp - allocate memories outsize main driver structure
 *
 * @bp:		driver handle
 */
int bnx2x_alloc_mem_bp(struct bnx2x *bp);

/**
 * bnx2x_free_mem_bp - release memories outsize main driver structure
 *
 * @bp:		driver handle
 */
void bnx2x_free_mem_bp(struct bnx2x *bp);

/**
 * bnx2x_change_mtu - change mtu netdev callback
 *
 * @dev:	net device
 * @new_mtu:	requested mtu
 *
 */
int bnx2x_change_mtu(struct ether *dev, int new_mtu);

#ifdef NETDEV_FCOE_WWNN
/**
 * bnx2x_fcoe_get_wwn - return the requested WWN value for this port
 *
 * @dev:	net_device
 * @wwn:	output buffer
 * @type:	WWN type: NETDEV_FCOE_WWNN (node) or NETDEV_FCOE_WWPN (port)
 *
 */
int bnx2x_fcoe_get_wwn(struct ether *dev, uint64_t *wwn, int type);
#endif

netdev_features_t bnx2x_fix_features(struct ether *dev,
				     netdev_features_t features);
int bnx2x_set_features(struct ether *dev, netdev_features_t features);

/**
 * bnx2x_tx_timeout - tx timeout netdev callback
 *
 * @dev:	net device
 */
void bnx2x_tx_timeout(struct ether *dev);

/*********************** Inlines **********************************/
/*********************** Fast path ********************************/
static inline void bnx2x_update_fpsb_idx(struct bnx2x_fastpath *fp)
{
	cmb(); /* status block is written to by the chip */
	fp->fp_hc_idx = fp->sb_running_index[SM_RX_ID];
}

static inline void bnx2x_igu_ack_sb_gen(struct bnx2x *bp, uint8_t igu_sb_id,
					uint8_t segment, uint16_t index,
					uint8_t op,
					uint8_t update, uint32_t igu_addr)
{
	struct igu_regular cmd_data = {0};

	cmd_data.sb_id_and_flags =
			((index << IGU_REGULAR_SB_INDEX_SHIFT) |
			 (segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
			 (update << IGU_REGULAR_BUPDATE_SHIFT) |
			 (op << IGU_REGULAR_ENABLE_INT_SHIFT));

	DP(NETIF_MSG_INTR, "write 0x%08x to IGU addr 0x%x\n",
	   cmd_data.sb_id_and_flags, igu_addr);
	REG_WR(bp, igu_addr, cmd_data.sb_id_and_flags);

	/* Make sure that ACK is written */
	bus_wmb();
	cmb();
}

static inline void bnx2x_hc_ack_sb(struct bnx2x *bp, uint8_t sb_id,
				   uint8_t storm, uint16_t index, uint8_t op,
				   uint8_t update)
{
	uint32_t hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp)*32 +
		       COMMAND_REG_INT_ACK);
	struct igu_ack_register igu_ack;

	igu_ack.status_block_index = index;
	igu_ack.sb_id_and_flags =
			((sb_id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT) |
			 (storm << IGU_ACK_REGISTER_STORM_ID_SHIFT) |
			 (update << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT) |
			 (op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));

	REG_WR(bp, hc_addr, (*(uint32_t *)&igu_ack));

	/* Make sure that ACK is written */
	bus_wmb();
	cmb();
}

static inline void bnx2x_ack_sb(struct bnx2x *bp, uint8_t igu_sb_id,
				uint8_t storm,
				uint16_t index, uint8_t op, uint8_t update)
{
	if (bp->common.int_block == INT_BLOCK_HC)
		bnx2x_hc_ack_sb(bp, igu_sb_id, storm, index, op, update);
	else {
		uint8_t segment;

		if (CHIP_INT_MODE_IS_BC(bp))
			segment = storm;
		else if (igu_sb_id != bp->igu_dsb_id)
			segment = IGU_SEG_ACCESS_DEF;
		else if (storm == ATTENTION_ID)
			segment = IGU_SEG_ACCESS_ATTN;
		else
			segment = IGU_SEG_ACCESS_DEF;
		bnx2x_igu_ack_sb(bp, igu_sb_id, segment, index, op, update);
	}
}

static inline uint16_t bnx2x_hc_ack_int(struct bnx2x *bp)
{
	uint32_t hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp)*32 +
		       COMMAND_REG_SIMD_MASK);
	uint32_t result = REG_RD(bp, hc_addr);

	cmb();
	return result;
}

static inline uint16_t bnx2x_igu_ack_int(struct bnx2x *bp)
{
	uint32_t igu_addr = (BAR_IGU_INTMEM + IGU_REG_SISR_MDPC_WMASK_LSB_UPPER*8);
	uint32_t result = REG_RD(bp, igu_addr);

	DP(NETIF_MSG_INTR, "read 0x%08x from IGU addr 0x%x\n",
	   result, igu_addr);

	cmb();
	return result;
}

static inline uint16_t bnx2x_ack_int(struct bnx2x *bp)
{
	cmb();
	if (bp->common.int_block == INT_BLOCK_HC)
		return bnx2x_hc_ack_int(bp);
	else
		return bnx2x_igu_ack_int(bp);
}

static inline int bnx2x_has_tx_work_unload(struct bnx2x_fp_txdata *txdata)
{
	/* Tell compiler that consumer and producer can change */
	cmb();
	return txdata->tx_pkt_prod != txdata->tx_pkt_cons;
}

static inline uint16_t bnx2x_tx_avail(struct bnx2x *bp,
				 struct bnx2x_fp_txdata *txdata)
{
	int16_t used;
	uint16_t prod;
	uint16_t cons;

	prod = txdata->tx_bd_prod;
	cons = txdata->tx_bd_cons;

	used = SUB_S16(prod, cons);

#ifdef BNX2X_STOP_ON_ERROR
	warn_on(used < 0);
	warn_on(used > txdata->tx_ring_size);
	warn_on((txdata->tx_ring_size - used) > MAX_TX_AVAIL);
#endif

	return (int16_t)(txdata->tx_ring_size) - used;
}

static inline int bnx2x_tx_queue_has_work(struct bnx2x_fp_txdata *txdata)
{
	uint16_t hw_cons;

	/* Tell compiler that status block fields can change */
	cmb();
	hw_cons = le16_to_cpu(*txdata->tx_cons_sb);
	return hw_cons != txdata->tx_pkt_cons;
}

static inline bool bnx2x_has_tx_work(struct bnx2x_fastpath *fp)
{
	uint8_t cos;
	for_each_cos_in_tx_queue(fp, cos)
		if (bnx2x_tx_queue_has_work(fp->txdata_ptr[cos]))
			return true;
	return false;
}

#define BNX2X_IS_CQE_COMPLETED(cqe_fp) (cqe_fp->marker == 0x0)
#define BNX2X_SEED_CQE(cqe_fp) (cqe_fp->marker = 0xFFFFFFFF)
static inline int bnx2x_has_rx_work(struct bnx2x_fastpath *fp)
{
	uint16_t cons;
	union eth_rx_cqe *cqe;
	struct eth_fast_path_rx_cqe *cqe_fp;

	cons = RCQ_BD(fp->rx_comp_cons);
	cqe = &fp->rx_comp_ring[cons];
	cqe_fp = &cqe->fast_path_cqe;
	return BNX2X_IS_CQE_COMPLETED(cqe_fp);
}

/**
 * bnx2x_tx_disable - disables tx from stack point of view
 *
 * @bp:		driver handle
 */
static inline void bnx2x_tx_disable(struct bnx2x *bp)
{
panic("Not implemented");
#if 0 // AKAROS_PORT
	netif_tx_disable(bp->dev);
	netif_carrier_off(bp->dev);
#endif
}

static inline void bnx2x_free_rx_sge(struct bnx2x *bp,
				     struct bnx2x_fastpath *fp,
				     uint16_t index)
{
	struct sw_rx_page *sw_buf = &fp->rx_page_ring[index];
	struct page *page = sw_buf->page;
	struct eth_rx_sge *sge = &fp->rx_sge_ring[index];

	/* Skip "next page" elements */
	if (!page)
		return;

	dma_unmap_page(&bp->pdev->dev, dma_unmap_addr(sw_buf, mapping),
		       SGE_PAGES, DMA_FROM_DEVICE);
	free_cont_pages(page2kva(page), PAGES_PER_SGE_SHIFT);

	sw_buf->page = NULL;
	sge->addr_hi = 0;
	sge->addr_lo = 0;
}

static inline void bnx2x_del_all_napi_cnic(struct bnx2x *bp)
{
panic("Not implemented");
#if 0 // AKAROS_PORT
	int i;

	for_each_rx_queue_cnic(bp, i) {
		napi_hash_del(&bnx2x_fp(bp, i, napi));
		netif_napi_del(&bnx2x_fp(bp, i, napi));
	}
#endif
}

static inline void bnx2x_del_all_napi(struct bnx2x *bp)
{
panic("Not implemented");
#if 0 // AKAROS_PORT
	int i;

	for_each_eth_queue(bp, i) {
		napi_hash_del(&bnx2x_fp(bp, i, napi));
		netif_napi_del(&bnx2x_fp(bp, i, napi));
	}
#endif
}

int bnx2x_set_int_mode(struct bnx2x *bp);

static inline void bnx2x_disable_msi(struct bnx2x *bp)
{
panic("Not implemented");
#if 0 // AKAROS_PORT
	if (bp->flags & USING_MSIX_FLAG) {
		pci_disable_msix(bp->pdev);
		bp->flags &= ~(USING_MSIX_FLAG | USING_SINGLE_MSIX_FLAG);
	} else if (bp->flags & USING_MSI_FLAG) {
		pci_disable_msi(bp->pdev);
		bp->flags &= ~USING_MSI_FLAG;
	}
#endif
}

static inline void bnx2x_clear_sge_mask_next_elems(struct bnx2x_fastpath *fp)
{
	int i, j;

	for (i = 1; i <= NUM_RX_SGE_PAGES; i++) {
		int idx = RX_SGE_CNT * i - 1;

		for (j = 0; j < 2; j++) {
			BIT_VEC64_CLEAR_BIT(fp->sge_mask, idx);
			idx--;
		}
	}
}

static inline void bnx2x_init_sge_ring_bit_mask(struct bnx2x_fastpath *fp)
{
	/* Set the mask to all 1-s: it's faster to compare to 0 than to 0xf-s */
	memset(fp->sge_mask, 0xff, sizeof(fp->sge_mask));

	/* Clear the two last indices in the page to 1:
	   these are the indices that correspond to the "next" element,
	   hence will never be indicated and should be removed from
	   the calculations. */
	bnx2x_clear_sge_mask_next_elems(fp);
}

/* note that we are not allocating a new buffer,
 * we are just moving one from cons to prod
 * we are not creating a new mapping,
 * so there is no need to check for dma_mapping_error().
 */
static inline void bnx2x_reuse_rx_data(struct bnx2x_fastpath *fp,
				      uint16_t cons, uint16_t prod)
{
	struct sw_rx_bd *cons_rx_buf = &fp->rx_buf_ring[cons];
	struct sw_rx_bd *prod_rx_buf = &fp->rx_buf_ring[prod];
	struct eth_rx_bd *cons_bd = &fp->rx_desc_ring[cons];
	struct eth_rx_bd *prod_bd = &fp->rx_desc_ring[prod];

	dma_unmap_addr_set(prod_rx_buf, mapping,
			   dma_unmap_addr(cons_rx_buf, mapping));
	prod_rx_buf->data = cons_rx_buf->data;
	*prod_bd = *cons_bd;
}

/************************* Init ******************************************/

/* returns func by VN for current port */
static inline int func_by_vn(struct bnx2x *bp, int vn)
{
	return 2 * vn + BP_PORT(bp);
}

static inline int bnx2x_config_rss_eth(struct bnx2x *bp, bool config_hash)
{
	return bnx2x_rss(bp, &bp->rss_conf_obj, config_hash, true);
}

/**
 * bnx2x_func_start - init function
 *
 * @bp:		driver handle
 *
 * Must be called before sending CLIENT_SETUP for the first client.
 */
static inline int bnx2x_func_start(struct bnx2x *bp)
{
	struct bnx2x_func_state_params func_params = {NULL};
	struct bnx2x_func_start_params *start_params =
		&func_params.params.start;

	/* Prepare parameters for function state transitions */
	__set_bit(RAMROD_COMP_WAIT, &func_params.ramrod_flags);

	func_params.f_obj = &bp->func_obj;
	func_params.cmd = BNX2X_F_CMD_START;

	/* Function parameters */
	start_params->mf_mode = bp->mf_mode;
	start_params->sd_vlan_tag = bp->mf_ov;

	if (CHIP_IS_E2(bp) || CHIP_IS_E3(bp))
		start_params->network_cos_mode = STATIC_COS;
	else /* CHIP_IS_E1X */
		start_params->network_cos_mode = FW_WRR;

	start_params->tunnel_mode	= TUNN_MODE_GRE;
	start_params->gre_tunnel_type	= IPGRE_TUNNEL;
	start_params->inner_gre_rss_en	= 1;

	if (IS_MF_UFP(bp) && BNX2X_IS_MF_SD_PROTOCOL_FCOE(bp)) {
		start_params->class_fail_ethtype = ETH_P_FIP;
		start_params->class_fail = 1;
		start_params->no_added_tags = 1;
	}

	return bnx2x_func_state_change(bp, &func_params);
}

/**
 * bnx2x_set_fw_mac_addr - fill in a MAC address in FW format
 *
 * @fw_hi:	pointer to upper part
 * @fw_mid:	pointer to middle part
 * @fw_lo:	pointer to lower part
 * @mac:	pointer to MAC address
 */
static inline void bnx2x_set_fw_mac_addr(__le16 *fw_hi, __le16 *fw_mid,
					 __le16 *fw_lo, uint8_t *mac)
{
	((uint8_t *)fw_hi)[0]  = mac[1];
	((uint8_t *)fw_hi)[1]  = mac[0];
	((uint8_t *)fw_mid)[0] = mac[3];
	((uint8_t *)fw_mid)[1] = mac[2];
	((uint8_t *)fw_lo)[0]  = mac[5];
	((uint8_t *)fw_lo)[1]  = mac[4];
}

static inline void bnx2x_free_rx_sge_range(struct bnx2x *bp,
					   struct bnx2x_fastpath *fp, int last)
{
	int i;

	if (fp->disable_tpa)
		return;

	for (i = 0; i < last; i++)
		bnx2x_free_rx_sge(bp, fp, i);
}

static inline void bnx2x_set_next_page_rx_bd(struct bnx2x_fastpath *fp)
{
	int i;

	for (i = 1; i <= NUM_RX_RINGS; i++) {
		struct eth_rx_bd *rx_bd;

		rx_bd = &fp->rx_desc_ring[RX_DESC_CNT * i - 2];
		rx_bd->addr_hi =
			cpu_to_le32(U64_HI(fp->rx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));
		rx_bd->addr_lo =
			cpu_to_le32(U64_LO(fp->rx_desc_mapping +
				    BCM_PAGE_SIZE*(i % NUM_RX_RINGS)));
	}
}

/* Statistics ID are global per chip/path, while Client IDs for E1x are per
 * port.
 */
static inline uint8_t bnx2x_stats_id(struct bnx2x_fastpath *fp)
{
	struct bnx2x *bp = fp->bp;
	if (!CHIP_IS_E1x(bp)) {
		/* there are special statistics counters for FCoE 136..140 */
		if (IS_FCOE_FP(fp))
			return bp->cnic_base_cl_id + (bp->pf_num >> 1);
		return fp->cl_id;
	}
	return fp->cl_id + BP_PORT(bp) * FP_SB_MAX_E1x;
}

static inline void bnx2x_init_vlan_mac_fp_objs(struct bnx2x_fastpath *fp,
					       bnx2x_obj_type obj_type)
{
	struct bnx2x *bp = fp->bp;

	/* Configure classification DBs */
	bnx2x_init_mac_obj(bp, &bnx2x_sp_obj(bp, fp).mac_obj, fp->cl_id,
			   fp->cid, BP_FUNC(bp), bnx2x_sp(bp, mac_rdata),
			   bnx2x_sp_mapping(bp, mac_rdata),
			   BNX2X_FILTER_MAC_PENDING,
			   &bp->sp_state, obj_type,
			   &bp->macs_pool);
}

/**
 * bnx2x_get_path_func_num - get number of active functions
 *
 * @bp:		driver handle
 *
 * Calculates the number of active (not hidden) functions on the
 * current path.
 */
static inline uint8_t bnx2x_get_path_func_num(struct bnx2x *bp)
{
	uint8_t func_num = 0, i;

	/* 57710 has only one function per-port */
	if (CHIP_IS_E1(bp))
		return 1;

	/* Calculate a number of functions enabled on the current
	 * PATH/PORT.
	 */
	if (CHIP_REV_IS_SLOW(bp)) {
		if (IS_MF(bp))
			func_num = 4;
		else
			func_num = 2;
	} else {
		for (i = 0; i < E1H_FUNC_MAX / 2; i++) {
			uint32_t func_config =
				MF_CFG_RD(bp,
					  func_mf_config[BP_PORT(bp) + 2 * i].
					  config);
			func_num +=
				((func_config & FUNC_MF_CFG_FUNC_HIDE) ? 0 : 1);
		}
	}

	warn_on(!func_num);

	return func_num;
}

static inline void bnx2x_init_bp_objs(struct bnx2x *bp)
{
	/* RX_MODE controlling object */
	bnx2x_init_rx_mode_obj(bp, &bp->rx_mode_obj);

	/* multicast configuration controlling object */
	bnx2x_init_mcast_obj(bp, &bp->mcast_obj, bp->fp->cl_id, bp->fp->cid,
			     BP_FUNC(bp), BP_FUNC(bp),
			     bnx2x_sp(bp, mcast_rdata),
			     bnx2x_sp_mapping(bp, mcast_rdata),
			     BNX2X_FILTER_MCAST_PENDING, &bp->sp_state,
			     BNX2X_OBJ_TYPE_RX);

	/* Setup CAM credit pools */
	bnx2x_init_mac_credit_pool(bp, &bp->macs_pool, BP_FUNC(bp),
				   bnx2x_get_path_func_num(bp));

	bnx2x_init_vlan_credit_pool(bp, &bp->vlans_pool, BP_ABS_FUNC(bp)>>1,
				    bnx2x_get_path_func_num(bp));

	/* RSS configuration object */
	bnx2x_init_rss_config_obj(bp, &bp->rss_conf_obj, bp->fp->cl_id,
				  bp->fp->cid, BP_FUNC(bp), BP_FUNC(bp),
				  bnx2x_sp(bp, rss_rdata),
				  bnx2x_sp_mapping(bp, rss_rdata),
				  BNX2X_FILTER_RSS_CONF_PENDING, &bp->sp_state,
				  BNX2X_OBJ_TYPE_RX);
}

static inline uint8_t bnx2x_fp_qzone_id(struct bnx2x_fastpath *fp)
{
	if (CHIP_IS_E1x(fp->bp))
		return fp->cl_id + BP_PORT(fp->bp) * ETH_MAX_RX_CLIENTS_E1H;
	else
		return fp->cl_id;
}

static inline void bnx2x_init_txdata(struct bnx2x *bp,
				     struct bnx2x_fp_txdata *txdata,
				     uint32_t cid,
				     int txq_index, __le16 *tx_cons_sb,
				     struct bnx2x_fastpath *fp)
{
	txdata->cid = cid;
	txdata->txq_index = txq_index;
	txdata->tx_cons_sb = tx_cons_sb;
	txdata->parent_fp = fp;
	txdata->tx_ring_size = IS_FCOE_FP(fp) ? MAX_TX_AVAIL : bp->tx_ring_size;

	/* Poke function - ghetto extern from bnx2x_dev.c */
	extern void __bnx2x_tx_queue(void *txdata_arg);
	poke_init(&txdata->poker, __bnx2x_tx_queue);
	/* TODO: AKAROS_PORT multi queue: assign proper oq */
	txdata->oq = bp->edev->oq;

	DP(NETIF_MSG_IFUP, "created tx data cid %d, txq %d\n",
	   txdata->cid, txdata->txq_index);
}

static inline uint8_t bnx2x_cnic_eth_cl_id(struct bnx2x *bp, uint8_t cl_idx)
{
	return bp->cnic_base_cl_id + cl_idx +
		(bp->pf_num >> 1) * BNX2X_MAX_CNIC_ETH_CL_ID_IDX;
}

static inline uint8_t bnx2x_cnic_fw_sb_id(struct bnx2x *bp)
{
	/* the 'first' id is allocated for the cnic */
	return bp->base_fw_ndsb;
}

static inline uint8_t bnx2x_cnic_igu_sb_id(struct bnx2x *bp)
{
	return bp->igu_base_sb;
}

static inline int bnx2x_clean_tx_queue(struct bnx2x *bp,
				       struct bnx2x_fp_txdata *txdata)
{
	int cnt = 1000;

	while (bnx2x_has_tx_work_unload(txdata)) {
		if (!cnt) {
			BNX2X_ERR("timeout waiting for queue[%d]: txdata->tx_pkt_prod(%d) != txdata->tx_pkt_cons(%d)\n",
				  txdata->txq_index, txdata->tx_pkt_prod,
				  txdata->tx_pkt_cons);
#ifdef BNX2X_STOP_ON_ERROR
			bnx2x_panic();
			return -EBUSY;
#else
			break;
#endif
		}
		cnt--;
		kthread_usleep(1000);
	}

	return 0;
}

int bnx2x_get_link_cfg_idx(struct bnx2x *bp);

static inline void __storm_memset_struct(struct bnx2x *bp,
					 uint32_t addr, size_t size,
					 uint32_t *data)
{
	int i;
	for (i = 0; i < size/4; i++)
		REG_WR(bp, addr + (i * 4), data[i]);
}

/**
 * bnx2x_wait_sp_comp - wait for the outstanding SP commands.
 *
 * @bp:		driver handle
 * @mask:	bits that need to be cleared
 */
static inline bool bnx2x_wait_sp_comp(struct bnx2x *bp, unsigned long mask)
{
	int tout = 5000; /* Wait for 5 secs tops */

	while (tout--) {
		mb();
		qlock(&bp->dev->qlock);
		if (!(bp->sp_state & mask)) {
			qunlock(&bp->dev->qlock);
			return true;
		}
		qunlock(&bp->dev->qlock);

		kthread_usleep(1000);
	}

	mb();

	qlock(&bp->dev->qlock);
	if (bp->sp_state & mask) {
		BNX2X_ERR("Filtering completion timed out. sp_state 0x%lx, mask 0x%lx\n",
			  bp->sp_state, mask);
		qunlock(&bp->dev->qlock);
		return false;
	}
	qunlock(&bp->dev->qlock);

	return true;
}

/**
 * bnx2x_set_ctx_validation - set CDU context validation values
 *
 * @bp:		driver handle
 * @cxt:	context of the connection on the host memory
 * @cid:	SW CID of the connection to be configured
 */
void bnx2x_set_ctx_validation(struct bnx2x *bp, struct eth_context *cxt,
			      uint32_t cid);

void bnx2x_update_coalesce_sb_index(struct bnx2x *bp, uint8_t fw_sb_id,
				    uint8_t sb_index, uint8_t disable,
				    uint16_t usec);
void bnx2x_acquire_phy_lock(struct bnx2x *bp);
void bnx2x_release_phy_lock(struct bnx2x *bp);

/**
 * bnx2x_extract_max_cfg - extract MAX BW part from MF configuration.
 *
 * @bp:		driver handle
 * @mf_cfg:	MF configuration
 *
 */
static inline uint16_t bnx2x_extract_max_cfg(struct bnx2x *bp,
					     uint32_t mf_cfg)
{
	uint16_t max_cfg = (mf_cfg & FUNC_MF_CFG_MAX_BW_MASK) >>
			      FUNC_MF_CFG_MAX_BW_SHIFT;
	if (!max_cfg) {
		DP(NETIF_MSG_IFUP | BNX2X_MSG_ETHTOOL,
		   "Max BW configured to 0 - using 100 instead\n");
		max_cfg = 100;
	}
	return max_cfg;
}

/* checks if HW supports GRO for given MTU */
static inline bool bnx2x_mtu_allows_gro(int mtu)
{
panic("Not implemented");
#if 0 // AKAROS_PORT
	/* gro frags per page */
	int fpp = SGE_PAGE_SIZE / (mtu - ETH_MAX_TPA_HEADER_SIZE);

	/*
	 * 1. Number of frags should not grow above MAX_SKB_FRAGS
	 * 2. Frag must fit the page
	 */
	return mtu <= SGE_PAGE_SIZE && (U_ETH_SGL_SIZE * fpp) <= MAX_SKB_FRAGS;
#endif
}

/**
 * bnx2x_get_iscsi_info - update iSCSI params according to licensing info.
 *
 * @bp:		driver handle
 *
 */
void bnx2x_get_iscsi_info(struct bnx2x *bp);

/**
 * bnx2x_link_sync_notify - send notification to other functions.
 *
 * @bp:		driver handle
 *
 */
static inline void bnx2x_link_sync_notify(struct bnx2x *bp)
{
	int func;
	int vn;

	/* Set the attention towards other drivers on the same port */
	for (vn = VN_0; vn < BP_MAX_VN_NUM(bp); vn++) {
		if (vn == BP_VN(bp))
			continue;

		func = func_by_vn(bp, vn);
		REG_WR(bp, MISC_REG_AEU_GENERAL_ATTN_0 +
		       (LINK_SYNC_ATTENTION_BIT_FUNC_0 + func)*4, 1);
	}
}

/**
 * bnx2x_update_drv_flags - update flags in shmem
 *
 * @bp:		driver handle
 * @flags:	flags to update
 * @set:	set or clear
 *
 */
static inline void bnx2x_update_drv_flags(struct bnx2x *bp, uint32_t flags,
					  uint32_t set)
{
	if (SHMEM2_HAS(bp, drv_flags)) {
		uint32_t drv_flags;
		bnx2x_acquire_hw_lock(bp, HW_LOCK_RESOURCE_DRV_FLAGS);
		drv_flags = SHMEM2_RD(bp, drv_flags);

		if (set)
			SET_FLAGS(drv_flags, flags);
		else
			RESET_FLAGS(drv_flags, flags);

		SHMEM2_WR(bp, drv_flags, drv_flags);
		DP(NETIF_MSG_IFUP, "drv_flags 0x%08x\n", drv_flags);
		bnx2x_release_hw_lock(bp, HW_LOCK_RESOURCE_DRV_FLAGS);
	}
}



/**
 * bnx2x_fill_fw_str - Fill buffer with FW version string
 *
 * @bp:        driver handle
 * @buf:       character buffer to fill with the fw name
 * @buf_len:   length of the above buffer
 *
 */
void bnx2x_fill_fw_str(struct bnx2x *bp, char *buf, size_t buf_len);

int bnx2x_drain_tx_queues(struct bnx2x *bp);
void bnx2x_squeeze_objects(struct bnx2x *bp);

void bnx2x_schedule_sp_rtnl(struct bnx2x*, enum sp_rtnl_flag,
			    uint32_t verbose);
