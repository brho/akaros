/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_HW_H_
#define _E1000_HW_H_

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>
#include "e1000_osdep.h"
#include "e1000_regs.h"
#include "e1000_defines.h"

struct e1000_hw;

#define E1000_DEV_ID_82542                    0x1000
#define E1000_DEV_ID_82543GC_FIBER            0x1001
#define E1000_DEV_ID_82543GC_COPPER           0x1004
#define E1000_DEV_ID_82544EI_COPPER           0x1008
#define E1000_DEV_ID_82544EI_FIBER            0x1009
#define E1000_DEV_ID_82544GC_COPPER           0x100C
#define E1000_DEV_ID_82544GC_LOM              0x100D
#define E1000_DEV_ID_82540EM                  0x100E
#define E1000_DEV_ID_82540EM_LOM              0x1015
#define E1000_DEV_ID_82540EP_LOM              0x1016
#define E1000_DEV_ID_82540EP                  0x1017
#define E1000_DEV_ID_82540EP_LP               0x101E
#define E1000_DEV_ID_82545EM_COPPER           0x100F
#define E1000_DEV_ID_82545EM_FIBER            0x1011
#define E1000_DEV_ID_82545GM_COPPER           0x1026
#define E1000_DEV_ID_82545GM_FIBER            0x1027
#define E1000_DEV_ID_82545GM_SERDES           0x1028
#define E1000_DEV_ID_82546EB_COPPER           0x1010
#define E1000_DEV_ID_82546EB_FIBER            0x1012
#define E1000_DEV_ID_82546EB_QUAD_COPPER      0x101D
#define E1000_DEV_ID_82546GB_COPPER           0x1079
#define E1000_DEV_ID_82546GB_FIBER            0x107A
#define E1000_DEV_ID_82546GB_SERDES           0x107B
#define E1000_DEV_ID_82546GB_PCIE             0x108A
#define E1000_DEV_ID_82546GB_QUAD_COPPER      0x1099
#define E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3 0x10B5
#define E1000_DEV_ID_82541EI                  0x1013
#define E1000_DEV_ID_82541EI_MOBILE           0x1018
#define E1000_DEV_ID_82541ER_LOM              0x1014
#define E1000_DEV_ID_82541ER                  0x1078
#define E1000_DEV_ID_82541GI                  0x1076
#define E1000_DEV_ID_82541GI_LF               0x107C
#define E1000_DEV_ID_82541GI_MOBILE           0x1077
#define E1000_DEV_ID_82547EI                  0x1019
#define E1000_DEV_ID_82547EI_MOBILE           0x101A
#define E1000_DEV_ID_82547GI                  0x1075
#define E1000_REVISION_0 0
#define E1000_REVISION_1 1
#define E1000_REVISION_2 2
#define E1000_REVISION_3 3
#define E1000_REVISION_4 4

#define E1000_FUNC_0     0
#define E1000_FUNC_1     1

#define E1000_ALT_MAC_ADDRESS_OFFSET_LAN0   0
#define E1000_ALT_MAC_ADDRESS_OFFSET_LAN1   3

enum e1000_mac_type {
	e1000_undefined = 0,
	e1000_82542,
	e1000_82543,
	e1000_82544,
	e1000_82540,
	e1000_82545,
	e1000_82545_rev_3,
	e1000_82546,
	e1000_82546_rev_3,
	e1000_82541,
	e1000_82541_rev_2,
	e1000_82547,
	e1000_82547_rev_2,
	e1000_num_macs	/* List is 1-based, so subtract 1 for true count. */
};

enum e1000_media_type {
	e1000_media_type_unknown = 0,
	e1000_media_type_copper = 1,
	e1000_media_type_fiber = 2,
	e1000_media_type_internal_serdes = 3,
	e1000_num_media_types
};

enum e1000_nvm_type {
	e1000_nvm_unknown = 0,
	e1000_nvm_none,
	e1000_nvm_eeprom_spi,
	e1000_nvm_eeprom_microwire,
	e1000_nvm_flash_hw,
	e1000_nvm_flash_sw
};

enum e1000_nvm_override {
	e1000_nvm_override_none = 0,
	e1000_nvm_override_spi_small,
	e1000_nvm_override_spi_large,
	e1000_nvm_override_microwire_small,
	e1000_nvm_override_microwire_large
};

enum e1000_phy_type {
	e1000_phy_unknown = 0,
	e1000_phy_none,
	e1000_phy_m88,
	e1000_phy_igp,
	e1000_phy_igp_2,
	e1000_phy_gg82563,
	e1000_phy_igp_3,
	e1000_phy_ife,
};

enum e1000_bus_type {
	e1000_bus_type_unknown = 0,
	e1000_bus_type_pci,
	e1000_bus_type_pcix,
	e1000_bus_type_pci_express,
	e1000_bus_type_reserved
};

enum e1000_bus_speed {
	e1000_bus_speed_unknown = 0,
	e1000_bus_speed_33,
	e1000_bus_speed_66,
	e1000_bus_speed_100,
	e1000_bus_speed_120,
	e1000_bus_speed_133,
	e1000_bus_speed_2500,
	e1000_bus_speed_5000,
	e1000_bus_speed_reserved
};

enum e1000_bus_width {
	e1000_bus_width_unknown = 0,
	e1000_bus_width_pcie_x1,
	e1000_bus_width_pcie_x2,
	e1000_bus_width_pcie_x4 = 4,
	e1000_bus_width_pcie_x8 = 8,
	e1000_bus_width_32,
	e1000_bus_width_64,
	e1000_bus_width_reserved
};

enum e1000_1000t_rx_status {
	e1000_1000t_rx_status_not_ok = 0,
	e1000_1000t_rx_status_ok,
	e1000_1000t_rx_status_undefined = 0xFF
};

enum e1000_rev_polarity {
	e1000_rev_polarity_normal = 0,
	e1000_rev_polarity_reversed,
	e1000_rev_polarity_undefined = 0xFF
};

enum e1000_fc_mode {
	e1000_fc_none = 0,
	e1000_fc_rx_pause,
	e1000_fc_tx_pause,
	e1000_fc_full,
	e1000_fc_default = 0xFF
};

enum e1000_ffe_config {
	e1000_ffe_config_enabled = 0,
	e1000_ffe_config_active,
	e1000_ffe_config_blocked
};

enum e1000_dsp_config {
	e1000_dsp_config_disabled = 0,
	e1000_dsp_config_enabled,
	e1000_dsp_config_activated,
	e1000_dsp_config_undefined = 0xFF
};

enum e1000_ms_type {
	e1000_ms_hw_default = 0,
	e1000_ms_force_master,
	e1000_ms_force_slave,
	e1000_ms_auto
};

enum e1000_smart_speed {
	e1000_smart_speed_default = 0,
	e1000_smart_speed_on,
	e1000_smart_speed_off
};

enum e1000_serdes_link_state {
	e1000_serdes_link_down = 0,
	e1000_serdes_link_autoneg_progress,
	e1000_serdes_link_autoneg_complete,
	e1000_serdes_link_forced_up
};

/* Receive Descriptor */
struct e1000_rx_desc {
	uint64_t buffer_addr;		/* Address of the descriptor's data buffer */
	uint16_t length;			/* Length of data DMAed into data buffer */
	uint16_t csum;				/* Packet checksum */
	uint8_t status;				/* Descriptor status */
	uint8_t errors;				/* Descriptor Errors */
	uint16_t special;
};

/* Receive Descriptor - Extended */
union e1000_rx_desc_extended {
	struct {
		uint64_t buffer_addr;
		uint64_t reserved;
	} read;
	struct {
		struct {
			uint32_t mrq;		/* Multiple Rx Queues */
			union {
				uint32_t rss;	/* RSS Hash */
				struct {
					uint16_t ip_id;	/* IP id */
					uint16_t csum;	/* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			uint32_t status_error;	/* ext status/error */
			uint16_t length;
			uint16_t vlan;		/* VLAN tag */
		} upper;
	} wb;						/* writeback */
};

#define MAX_PS_BUFFERS 4
/* Receive Descriptor - Packet Split */
union e1000_rx_desc_packet_split {
	struct {
		/* one buffer for protocol header(s), three data buffers */
		uint64_t buffer_addr[MAX_PS_BUFFERS];
	} read;
	struct {
		struct {
			uint32_t mrq;		/* Multiple Rx Queues */
			union {
				uint32_t rss;	/* RSS Hash */
				struct {
					uint16_t ip_id;	/* IP id */
					uint16_t csum;	/* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			uint32_t status_error;	/* ext status/error */
			uint16_t length0;	/* length of buffer 0 */
			uint16_t vlan;		/* VLAN tag */
		} middle;
		struct {
			uint16_t header_status;
			uint16_t length[3];	/* length of buffers 1-3 */
		} upper;
		uint64_t reserved;
	} wb;						/* writeback */
};

/* Transmit Descriptor */
struct e1000_tx_desc {
	uint64_t buffer_addr;		/* Address of the descriptor's data buffer */
	union {
		uint32_t data;
		struct {
			uint16_t length;	/* Data buffer length */
			uint8_t cso;		/* Checksum offset */
			uint8_t cmd;		/* Descriptor control */
		} flags;
	} lower;
	union {
		uint32_t data;
		struct {
			uint8_t status;		/* Descriptor status */
			uint8_t css;		/* Checksum start */
			uint16_t special;
		} fields;
	} upper;
};

/* Offload Context Descriptor */
struct e1000_context_desc {
	union {
		uint32_t ip_config;
		struct {
			uint8_t ipcss;		/* IP checksum start */
			uint8_t ipcso;		/* IP checksum offset */
			uint16_t ipcse;		/* IP checksum end */
		} ip_fields;
	} lower_setup;
	union {
		uint32_t tcp_config;
		struct {
			uint8_t tucss;		/* TCP checksum start */
			uint8_t tucso;		/* TCP checksum offset */
			uint16_t tucse;		/* TCP checksum end */
		} tcp_fields;
	} upper_setup;
	uint32_t cmd_and_length;
	union {
		uint32_t data;
		struct {
			uint8_t status;		/* Descriptor status */
			uint8_t hdr_len;	/* Header length */
			uint16_t mss;		/* Maximum segment size */
		} fields;
	} tcp_seg_setup;
};

/* Offload data descriptor */
struct e1000_data_desc {
	uint64_t buffer_addr;		/* Address of the descriptor's buffer address */
	union {
		uint32_t data;
		struct {
			uint16_t length;	/* Data buffer length */
			uint8_t typ_len_ext;
			uint8_t cmd;
		} flags;
	} lower;
	union {
		uint32_t data;
		struct {
			uint8_t status;		/* Descriptor status */
			uint8_t popts;		/* Packet Options */
			uint16_t special;
		} fields;
	} upper;
};

/* Statistics counters collected by the MAC */
struct e1000_hw_stats {
	uint64_t crcerrs;
	uint64_t algnerrc;
	uint64_t symerrs;
	uint64_t rxerrc;
	uint64_t mpc;
	uint64_t scc;
	uint64_t ecol;
	uint64_t mcc;
	uint64_t latecol;
	uint64_t colc;
	uint64_t dc;
	uint64_t tncrs;
	uint64_t sec;
	uint64_t cexterr;
	uint64_t rlec;
	uint64_t xonrxc;
	uint64_t xontxc;
	uint64_t xoffrxc;
	uint64_t xofftxc;
	uint64_t fcruc;
	uint64_t prc64;
	uint64_t prc127;
	uint64_t prc255;
	uint64_t prc511;
	uint64_t prc1023;
	uint64_t prc1522;
	uint64_t gprc;
	uint64_t bprc;
	uint64_t mprc;
	uint64_t gptc;
	uint64_t gorc;
	uint64_t gotc;
	uint64_t rnbc;
	uint64_t ruc;
	uint64_t rfc;
	uint64_t roc;
	uint64_t rjc;
	uint64_t mgprc;
	uint64_t mgpdc;
	uint64_t mgptc;
	uint64_t tor;
	uint64_t tot;
	uint64_t tpr;
	uint64_t tpt;
	uint64_t ptc64;
	uint64_t ptc127;
	uint64_t ptc255;
	uint64_t ptc511;
	uint64_t ptc1023;
	uint64_t ptc1522;
	uint64_t mptc;
	uint64_t bptc;
	uint64_t tsctc;
	uint64_t tsctfc;
	uint64_t iac;
	uint64_t icrxptc;
	uint64_t icrxatc;
	uint64_t ictxptc;
	uint64_t ictxatc;
	uint64_t ictxqec;
	uint64_t ictxqmtc;
	uint64_t icrxdmtc;
	uint64_t icrxoc;
	uint64_t cbtmpc;
	uint64_t htdpmc;
	uint64_t cbrdpc;
	uint64_t cbrmpc;
	uint64_t rpthc;
	uint64_t hgptc;
	uint64_t htcbdpc;
	uint64_t hgorc;
	uint64_t hgotc;
	uint64_t lenerrs;
	uint64_t scvpc;
	uint64_t hrmpc;
	uint64_t doosync;
};

struct e1000_phy_stats {
	uint32_t idle_errors;
	uint32_t receive_errors;
};

struct e1000_host_mng_dhcp_cookie {
	uint32_t signature;
	uint8_t status;
	uint8_t reserved0;
	uint16_t vlan_id;
	uint32_t reserved1;
	uint16_t reserved2;
	uint8_t reserved3;
	uint8_t checksum;
};

/* Host Interface "Rev 1" */
struct e1000_host_command_header {
	uint8_t command_id;
	uint8_t command_length;
	uint8_t command_options;
	uint8_t checksum;
};

#define E1000_HI_MAX_DATA_LENGTH     252
struct e1000_host_command_info {
	struct e1000_host_command_header command_header;
	uint8_t command_data[E1000_HI_MAX_DATA_LENGTH];
};

/* Host Interface "Rev 2" */
struct e1000_host_mng_command_header {
	uint8_t command_id;
	uint8_t checksum;
	uint16_t reserved1;
	uint16_t reserved2;
	uint16_t command_length;
};

#define E1000_HI_MAX_MNG_DATA_LENGTH 0x6F8
struct e1000_host_mng_command_info {
	struct e1000_host_mng_command_header command_header;
	uint8_t command_data[E1000_HI_MAX_MNG_DATA_LENGTH];
};

#include "e1000_mac.h"
#include "e1000_phy.h"
#include "e1000_nvm.h"
#include "e1000_manage.h"

struct e1000_mac_operations {
	/* Function pointers for the MAC. */
	int32_t(*init_params) (struct e1000_hw *);
	int32_t(*id_led_init) (struct e1000_hw *);
	int32_t(*blink_led) (struct e1000_hw *);
	int32_t(*check_for_link) (struct e1000_hw *);
	bool(*check_mng_mode) (struct e1000_hw * hw);
	int32_t(*cleanup_led) (struct e1000_hw *);
	void (*clear_hw_cntrs) (struct e1000_hw *);
	void (*clear_vfta) (struct e1000_hw *);
	 int32_t(*get_bus_info) (struct e1000_hw *);
	void (*set_lan_id) (struct e1000_hw *);
	 int32_t(*get_link_up_info) (struct e1000_hw *, uint16_t *, uint16_t *);
	 int32_t(*led_on) (struct e1000_hw *);
	 int32_t(*led_off) (struct e1000_hw *);
	void (*update_mc_addr_list) (struct e1000_hw *, uint8_t *, uint32_t);
	 int32_t(*reset_hw) (struct e1000_hw *);
	 int32_t(*init_hw) (struct e1000_hw *);
	 int32_t(*setup_link) (struct e1000_hw *);
	 int32_t(*setup_physical_interface) (struct e1000_hw *);
	 int32_t(*setup_led) (struct e1000_hw *);
	void (*write_vfta) (struct e1000_hw *, uint32_t, uint32_t);
	void (*mta_set) (struct e1000_hw *, uint32_t);
	void (*config_collision_dist) (struct e1000_hw *);
	void (*rar_set) (struct e1000_hw *, uint8_t *, uint32_t);
	 int32_t(*read_mac_addr) (struct e1000_hw *);
	 int32_t(*validate_mdi_setting) (struct e1000_hw *);
	 int32_t(*mng_host_if_write) (struct e1000_hw *, uint8_t *, uint16_t,
								  uint16_t, uint8_t *);
	 int32_t(*mng_write_cmd_header) (struct e1000_hw * hw,
									 struct e1000_host_mng_command_header *);
	 int32_t(*mng_enable_host_if) (struct e1000_hw *);
	 int32_t(*wait_autoneg) (struct e1000_hw *);
};

struct e1000_phy_operations {
	int32_t(*init_params) (struct e1000_hw *);
	int32_t(*acquire) (struct e1000_hw *);
	int32_t(*check_polarity) (struct e1000_hw *);
	int32_t(*check_reset_block) (struct e1000_hw *);
	int32_t(*commit) (struct e1000_hw *);
#if 0
	s32(*force_speed_duplex) (struct e1000_hw *);
#endif
	int32_t(*get_cfg_done) (struct e1000_hw * hw);
#if 0
	s32(*get_cable_length) (struct e1000_hw *);
#endif
	int32_t(*get_info) (struct e1000_hw *);
	int32_t(*read_reg) (struct e1000_hw *, uint32_t, uint16_t *);
	void (*release) (struct e1000_hw *);
	 int32_t(*reset) (struct e1000_hw *);
	 int32_t(*set_d0_lplu_state) (struct e1000_hw *, bool);
	 int32_t(*set_d3_lplu_state) (struct e1000_hw *, bool);
	 int32_t(*write_reg) (struct e1000_hw *, uint32_t, uint16_t);
	void (*power_up) (struct e1000_hw *);
	void (*power_down) (struct e1000_hw *);
};

struct e1000_nvm_operations {
	int32_t(*init_params) (struct e1000_hw *);
	int32_t(*acquire) (struct e1000_hw *);
	int32_t(*read) (struct e1000_hw *, uint16_t, uint16_t, uint16_t *);
	void (*release) (struct e1000_hw *);
	void (*reload) (struct e1000_hw *);
	 int32_t(*update) (struct e1000_hw *);
	 int32_t(*valid_led_default) (struct e1000_hw *, uint16_t *);
	 int32_t(*validate) (struct e1000_hw *);
	 int32_t(*write) (struct e1000_hw *, uint16_t, uint16_t, uint16_t *);
};

struct e1000_mac_info {
	struct e1000_mac_operations ops;
	uint8_t addr[6];
	uint8_t perm_addr[6];

	enum e1000_mac_type type;

	uint32_t collision_delta;
	uint32_t ledctl_default;
	uint32_t ledctl_mode1;
	uint32_t ledctl_mode2;
	uint32_t mc_filter_type;
	uint32_t tx_packet_delta;
	uint32_t txcw;

	uint16_t current_ifs_val;
	uint16_t ifs_max_val;
	uint16_t ifs_min_val;
	uint16_t ifs_ratio;
	uint16_t ifs_step_size;
	uint16_t mta_reg_count;

	/* Maximum size of the MTA register table in all supported adapters */
#define MAX_MTA_REG 128
	uint32_t mta_shadow[MAX_MTA_REG];
	uint16_t rar_entry_count;

	uint8_t forced_speed_duplex;

	bool adaptive_ifs;
	bool arc_subsystem_valid;
	bool asf_firmware_present;
	bool autoneg;
	bool autoneg_failed;
	bool get_link_status;
	bool in_ifs_mode;
	bool report_tx_early;
	enum e1000_serdes_link_state serdes_link_state;
	bool serdes_has_link;
	bool tx_pkt_filtering;
};

struct e1000_phy_info {
	struct e1000_phy_operations ops;
	enum e1000_phy_type type;

	enum e1000_1000t_rx_status local_rx;
	enum e1000_1000t_rx_status remote_rx;
	enum e1000_ms_type ms_type;
	enum e1000_ms_type original_ms_type;
	enum e1000_rev_polarity cable_polarity;
	enum e1000_smart_speed smart_speed;

	uint32_t addr;
	uint32_t id;
	uint32_t reset_delay_us;	/* in usec */
	uint32_t revision;

	enum e1000_media_type media_type;

	uint16_t autoneg_advertised;
	uint16_t autoneg_mask;
	uint16_t cable_length;
	uint16_t max_cable_length;
	uint16_t min_cable_length;

	uint8_t mdix;

	bool disable_polarity_correction;
	bool is_mdix;
	bool polarity_correction;
	bool reset_disable;
	bool speed_downgraded;
	bool autoneg_wait_to_complete;
};

struct e1000_nvm_info {
	struct e1000_nvm_operations ops;
	enum e1000_nvm_type type;
	enum e1000_nvm_override override;

	uint32_t flash_bank_size;
	uint32_t flash_base_addr;

	uint16_t word_size;
	uint16_t delay_usec;
	uint16_t address_bits;
	uint16_t opcode_bits;
	uint16_t page_size;
};

struct e1000_bus_info {
	enum e1000_bus_type type;
	enum e1000_bus_speed speed;
	enum e1000_bus_width width;

	uint16_t func;
	uint16_t pci_cmd_word;
};

struct e1000_fc_info {
	uint32_t high_water;		/* Flow control high-water mark */
	uint32_t low_water;			/* Flow control low-water mark */
	uint16_t pause_time;		/* Flow control pause timer */
	bool send_xon;				/* Flow control send XON */
	bool strict_ieee;			/* Strict IEEE mode */
	enum e1000_fc_mode current_mode;	/* FC mode in effect */
	enum e1000_fc_mode requested_mode;	/* FC mode requested by caller */
};

struct e1000_dev_spec_82541 {
	enum e1000_dsp_config dsp_config;
	enum e1000_ffe_config ffe_config;
	uint16_t spd_default;
	bool phy_init_script;
};

struct e1000_dev_spec_82542 {
	bool dma_fairness;
};

struct e1000_dev_spec_82543 {
	uint32_t tbi_compatibility;
	bool dma_fairness;
	bool init_phy_disabled;
};

struct e1000_hw {
	void *back;

	uint8_t *hw_addr;
	uint8_t *flash_address;
	unsigned long io_base;

	struct e1000_mac_info mac;
	struct e1000_fc_info fc;
	struct e1000_phy_info phy;
	struct e1000_nvm_info nvm;
	struct e1000_bus_info bus;
	struct e1000_host_mng_dhcp_cookie mng_cookie;

	union {
		struct e1000_dev_spec_82541 _82541;
		struct e1000_dev_spec_82542 _82542;
		struct e1000_dev_spec_82543 _82543;
	} dev_spec;

	uint16_t device_id;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;
	uint16_t vendor_id;

	uint8_t revision_id;
};

#include <vfs.h>
#include <kfs.h>
#include <slab.h>
#include <kmalloc.h>
#include <kref.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <error.h>
#include <cpio.h>
#include <pmap.h>
#include <smp.h>
#include <ip.h>

/* These functions must be implemented by drivers */
void e1000_pci_clear_mwi(struct e1000_hw *hw);
void e1000_pci_set_mwi(struct e1000_hw *hw);
int32_t e1000_read_pcie_cap_reg(struct e1000_hw *hw,
								uint32_t reg, uint16_t * value);
void e1000_read_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t * value);
void e1000_write_pci_cfg(struct e1000_hw *hw, uint32_t reg, uint16_t * value);

#endif
