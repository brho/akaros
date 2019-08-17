/*
 * Copyright © 2006-2015, Intel Corporation.
 *
 * Authors: Ashok Raj <ashok.raj@intel.com>
 *          Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *          David Woodhouse <David.Woodhouse@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 */

#ifndef _INTEL_IOMMU_H_
#define _INTEL_IOMMU_H_

#include <atomic.h>
#include <env.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t

/* paging: root table entries */
#define RT_LO_PRESENT_SHIFT	0

/* paging: context entries */
#define CTX_HI_DID_SHIFT	8
#define CTX_HI_AW_SHIFT		0 // address width
#define CTX_LO_TRANS_SHIFT	2
#define CTX_LO_FPD_SHIFT	1
#define CTX_LO_PRESENT_SHIFT	0

#define CTX_AW_L2	0x0 // 2-level page table
#define CTX_AW_L3	0x1
#define CTX_AW_L4	0x2
#define CTX_AW_L5	0x3
#define CTX_AW_L6	0x4

#define IOMMU_DID_DEFAULT 1 // means pid 1 cannot have a passthru device

struct iommu {
	spinlock_t iommu_lock;
	TAILQ_ENTRY(iommu) iommu_link;
	struct proc_list procs; // unused
	bool supported;
	bool device_iotlb;
	bool rwbf;

	uint16_t iotlb_cmd_offset;
	uint16_t iotlb_addr_offset;
	void __iomem *regio;
	uint64_t rba; /* for unique assertion */
	uint64_t num_assigned_devs;
	physaddr_t roottable;
	uint8_t haw_dmar; /* (=N+1) haw reported by DMAR */
	uint8_t haw_cap; /* (=N+1) haw reported by CAP[MGAW] of iommu */
};
extern TAILQ_HEAD(iommu_list_tq, iommu) iommu_list;

void iommu_initialize(struct iommu *iommu, uint8_t haw, uint64_t rba);
void iommu_initialize_global(void);
void iommu_map_pci_devices(void); /* associate pci devices with correct iommu */
bool iommu_supported(void);
struct iommu *get_default_iommu(void); /* IOMMU of DRHD with INCLUDE_PCI_ALL */
void iommu_enable(void); /* enable all iommus */
void iommu_disable(void); /* disable all iommus */
bool iommu_status(void); /* returns true if any iommu is turned on */
/* remove all pci devices associated with proc */
void iommu_process_cleanup(struct proc *p);

/*
 * VT-d hardware uses 4KiB page size regardless of host page size.
 */
#define VTD_PAGE_SHIFT		(12)
#define VTD_PAGE_SIZE		(1UL << VTD_PAGE_SHIFT)
#define VTD_PAGE_MASK		(((u64)-1) << VTD_PAGE_SHIFT)
#define VTD_PAGE_ALIGN(addr)	(((addr) + VTD_PAGE_SIZE - 1) & VTD_PAGE_MASK)

#define VTD_STRIDE_SHIFT        (9)
#define VTD_STRIDE_MASK         (((u64)-1) << VTD_STRIDE_SHIFT)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)
#define DMA_PTE_LARGE_PAGE (1 << 7)
#define DMA_PTE_SNP (1 << 11)

#define CONTEXT_TT_MULTI_LEVEL	0
#define CONTEXT_TT_DEV_IOTLB	1
#define CONTEXT_TT_PASS_THROUGH 2
#define CONTEXT_PASIDE		BIT_ULL(3)

/*
 * Intel IOMMU register specification per version 1.0 public spec.
 */
#define	DMAR_VER_REG	0x0	/* Arch version supported by this IOMMU */
#define	DMAR_CAP_REG	0x8	/* Hardware supported capabilities */
#define	DMAR_ECAP_REG	0x10	/* Extended capabilities supported */
#define	DMAR_GCMD_REG	0x18	/* Global command register */
#define	DMAR_GSTS_REG	0x1c	/* Global status register */
#define	DMAR_RTADDR_REG	0x20	/* Root entry table */
#define	DMAR_CCMD_REG	0x28	/* Context command reg */
#define	DMAR_FSTS_REG	0x34	/* Fault Status register */
#define	DMAR_FECTL_REG	0x38	/* Fault control register */
#define	DMAR_FEDATA_REG	0x3c	/* Fault event interrupt data register */
#define	DMAR_FEADDR_REG	0x40	/* Fault event interrupt addr register */
#define	DMAR_FEUADDR_REG 0x44	/* Upper address register */
#define	DMAR_AFLOG_REG	0x58	/* Advanced Fault control */
#define	DMAR_PMEN_REG	0x64	/* Enable Protected Memory Region */
#define	DMAR_PLMBASE_REG 0x68	/* PMRR Low addr */
#define	DMAR_PLMLIMIT_REG 0x6c	/* PMRR low limit */
#define	DMAR_PHMBASE_REG 0x70	/* pmrr high base addr */
#define	DMAR_PHMLIMIT_REG 0x78	/* pmrr high limit */
#define DMAR_IQH_REG	0x80	/* Invalidation queue head register */
#define DMAR_IQT_REG	0x88	/* Invalidation queue tail register */
#define DMAR_IQ_SHIFT	4	/* Invalidation queue head/tail shift */
#define DMAR_IQA_REG	0x90	/* Invalidation queue addr register */
#define DMAR_ICS_REG	0x9c	/* Invalidation complete status register */
#define DMAR_IRTA_REG	0xb8    /* Interrupt remapping table addr register */
#define DMAR_PQH_REG	0xc0	/* Page request queue head register */
#define DMAR_PQT_REG	0xc8	/* Page request queue tail register */
#define DMAR_PQA_REG	0xd0	/* Page request queue address register */
#define DMAR_PRS_REG	0xdc	/* Page request status register */
#define DMAR_PECTL_REG	0xe0	/* Page request event control register */
#define	DMAR_PEDATA_REG	0xe4	/* Page request event interrupt data register */
#define	DMAR_PEADDR_REG	0xe8	/* Page request event interrupt addr register */
#define	DMAR_PEUADDR_REG 0xec	/* Page request event Upper address register */
#define DMAR_MTRRCAP_REG 0x100	/* MTRR capability register */
#define DMAR_MTRRDEF_REG 0x108	/* MTRR default type register */
#define DMAR_MTRR_FIX64K_00000_REG 0x120 /* MTRR Fixed range registers */
#define DMAR_MTRR_FIX16K_80000_REG 0x128
#define DMAR_MTRR_FIX16K_A0000_REG 0x130
#define DMAR_MTRR_FIX4K_C0000_REG 0x138
#define DMAR_MTRR_FIX4K_C8000_REG 0x140
#define DMAR_MTRR_FIX4K_D0000_REG 0x148
#define DMAR_MTRR_FIX4K_D8000_REG 0x150
#define DMAR_MTRR_FIX4K_E0000_REG 0x158
#define DMAR_MTRR_FIX4K_E8000_REG 0x160
#define DMAR_MTRR_FIX4K_F0000_REG 0x168
#define DMAR_MTRR_FIX4K_F8000_REG 0x170
#define DMAR_MTRR_PHYSBASE0_REG 0x180 /* MTRR Variable range registers */
#define DMAR_MTRR_PHYSMASK0_REG 0x188
#define DMAR_MTRR_PHYSBASE1_REG 0x190
#define DMAR_MTRR_PHYSMASK1_REG 0x198
#define DMAR_MTRR_PHYSBASE2_REG 0x1a0
#define DMAR_MTRR_PHYSMASK2_REG 0x1a8
#define DMAR_MTRR_PHYSBASE3_REG 0x1b0
#define DMAR_MTRR_PHYSMASK3_REG 0x1b8
#define DMAR_MTRR_PHYSBASE4_REG 0x1c0
#define DMAR_MTRR_PHYSMASK4_REG 0x1c8
#define DMAR_MTRR_PHYSBASE5_REG 0x1d0
#define DMAR_MTRR_PHYSMASK5_REG 0x1d8
#define DMAR_MTRR_PHYSBASE6_REG 0x1e0
#define DMAR_MTRR_PHYSMASK6_REG 0x1e8
#define DMAR_MTRR_PHYSBASE7_REG 0x1f0
#define DMAR_MTRR_PHYSMASK7_REG 0x1f8
#define DMAR_MTRR_PHYSBASE8_REG 0x200
#define DMAR_MTRR_PHYSMASK8_REG 0x208
#define DMAR_MTRR_PHYSBASE9_REG 0x210
#define DMAR_MTRR_PHYSMASK9_REG 0x218
#define DMAR_VCCAP_REG		0xe00 /* Virtual command capability register */
#define DMAR_VCMD_REG		0xe10 /* Virtual command register */
#define DMAR_VCRSP_REG		0xe20 /* Virtual command response register */

#define OFFSET_STRIDE		(9)

#define dmar_readq(a) readq(a)
#define dmar_writeq(a,v) writeq(v,a)

#define DMAR_VER_MAJOR(v)		(((v) & 0xf0) >> 4)
#define DMAR_VER_MINOR(v)		((v) & 0x0f)

/*
 * Decoding Capability Register
 */
#define cap_5lp_support(c)	(((c) >> 60) & 1)
#define cap_pi_support(c)	(((c) >> 59) & 1)
#define cap_fl1gp_support(c)	(((c) >> 56) & 1)
#define cap_read_drain(c)	(((c) >> 55) & 1)
#define cap_write_drain(c)	(((c) >> 54) & 1)
#define cap_max_amask_val(c)	(((c) >> 48) & 0x3f)
#define cap_num_fault_regs(c)	((((c) >> 40) & 0xff) + 1)
#define cap_pgsel_inv(c)	(((c) >> 39) & 1)

#define cap_super_page_val(c)	(((c) >> 34) & 0xf)
#define cap_super_offset(c)	(((find_first_bit(&cap_super_page_val(c), 4)) \
					* OFFSET_STRIDE) + 21)

#define cap_fault_reg_offset(c)	((((c) >> 24) & 0x3ff) * 16)
#define cap_max_fault_reg_offset(c) \
	(cap_fault_reg_offset(c) + cap_num_fault_regs(c) * 16)

#define cap_zlr(c)		(((c) >> 22) & 1)
#define cap_isoch(c)		(((c) >> 23) & 1)
#define cap_mgaw(c)		((((c) >> 16) & 0x3f) + 1)
#define cap_sagaw(c)		(((c) >> 8) & 0x1f)
#define cap_caching_mode(c)	(((c) >> 7) & 1)
#define cap_phmr(c)		(((c) >> 6) & 1)
#define cap_plmr(c)		(((c) >> 5) & 1)
#define cap_rwbf(c)		(((c) >> 4) & 1)
#define cap_afl(c)		(((c) >> 3) & 1)
#define cap_ndoms(c)		(((unsigned long)1) << (4 + 2 * ((c) & 0x7)))
/*
 * Extended Capability Register
 */

#define ecap_smpwc(e)		(((e) >> 48) & 0x1)
#define ecap_flts(e)		(((e) >> 47) & 0x1)
#define ecap_slts(e)		(((e) >> 46) & 0x1)
#define ecap_smts(e)		(((e) >> 43) & 0x1)
#define ecap_dit(e)		((e >> 41) & 0x1)
#define ecap_pasid(e)		((e >> 40) & 0x1)
#define ecap_pss(e)		((e >> 35) & 0x1f)
#define ecap_eafs(e)		((e >> 34) & 0x1)
#define ecap_nwfs(e)		((e >> 33) & 0x1)
#define ecap_srs(e)		((e >> 31) & 0x1)
#define ecap_ers(e)		((e >> 30) & 0x1)
#define ecap_prs(e)		((e >> 29) & 0x1)
#define ecap_broken_pasid(e)	((e >> 28) & 0x1)
#define ecap_dis(e)		((e >> 27) & 0x1)
#define ecap_nest(e)		((e >> 26) & 0x1)
#define ecap_mts(e)		((e >> 25) & 0x1)
#define ecap_ecs(e)		((e >> 24) & 0x1)
#define ecap_iotlb_offset(e) 	((((e) >> 8) & 0x3ff) * 16)
#define ecap_max_iotlb_offset(e) (ecap_iotlb_offset(e) + 16)
#define ecap_coherent(e)	((e) & 0x1)
#define ecap_qis(e)		((e) & 0x2)
#define ecap_pass_through(e)	((e >> 6) & 0x1)
#define ecap_eim_support(e)	((e >> 4) & 0x1)
#define ecap_ir_support(e)	((e >> 3) & 0x1)
#define ecap_dev_iotlb_support(e)	(((e) >> 2) & 0x1)
#define ecap_max_handle_mask(e) ((e >> 20) & 0xf)
#define ecap_sc_support(e)	((e >> 7) & 0x1) /* Snooping Control */

/* IOTLB_REG */
#define DMA_TLB_FLUSH_GRANU_OFFSET  60
#define DMA_TLB_GLOBAL_FLUSH (((u64)1) << 60)
#define DMA_TLB_DSI_FLUSH (((u64)2) << 60)
#define DMA_TLB_PSI_FLUSH (((u64)3) << 60)
#define DMA_TLB_IIRG(type) ((type >> 60) & 3)
#define DMA_TLB_IAIG(val) (((val) >> 57) & 3)
#define DMA_TLB_READ_DRAIN (((u64)1) << 49)
#define DMA_TLB_WRITE_DRAIN (((u64)1) << 48)
#define DMA_TLB_DID(id)	(((u64)((id) & 0xffff)) << 32)
#define DMA_TLB_IVT (((u64)1) << 63)
#define DMA_TLB_IH_NONLEAF (((u64)1) << 6)
#define DMA_TLB_MAX_SIZE (0x3f)

/* INVALID_DESC */
#define DMA_CCMD_INVL_GRANU_OFFSET  61
#define DMA_ID_TLB_GLOBAL_FLUSH	(((u64)1) << 4)
#define DMA_ID_TLB_DSI_FLUSH	(((u64)2) << 4)
#define DMA_ID_TLB_PSI_FLUSH	(((u64)3) << 4)
#define DMA_ID_TLB_READ_DRAIN	(((u64)1) << 7)
#define DMA_ID_TLB_WRITE_DRAIN	(((u64)1) << 6)
#define DMA_ID_TLB_DID(id)	(((u64)((id & 0xffff) << 16)))
#define DMA_ID_TLB_IH_NONLEAF	(((u64)1) << 6)
#define DMA_ID_TLB_ADDR(addr)	(addr)
#define DMA_ID_TLB_ADDR_MASK(mask)	(mask)

/* PMEN_REG */
#define DMA_PMEN_EPM (((u32)1)<<31)
#define DMA_PMEN_PRS (((u32)1)<<0)

/* GCMD_REG */
#define DMA_GCMD_TE (((u32)1) << 31)
#define DMA_GCMD_SRTP (((u32)1) << 30)
#define DMA_GCMD_SFL (((u32)1) << 29)
#define DMA_GCMD_EAFL (((u32)1) << 28)
#define DMA_GCMD_WBF (((u32)1) << 27)
#define DMA_GCMD_QIE (((u32)1) << 26)
#define DMA_GCMD_SIRTP (((u32)1) << 24)
#define DMA_GCMD_IRE (((u32) 1) << 25)
#define DMA_GCMD_CFI (((u32) 1) << 23)

/* GSTS_REG */
#define DMA_GSTS_TES (((u32)1) << 31)
#define DMA_GSTS_RTPS (((u32)1) << 30)
#define DMA_GSTS_FLS (((u32)1) << 29)
#define DMA_GSTS_AFLS (((u32)1) << 28)
#define DMA_GSTS_WBFS (((u32)1) << 27)
#define DMA_GSTS_QIES (((u32)1) << 26)
#define DMA_GSTS_IRTPS (((u32)1) << 24)
#define DMA_GSTS_IRES (((u32)1) << 25)
#define DMA_GSTS_CFIS (((u32)1) << 23)

/* DMA_RTADDR_REG */
#define DMA_RTADDR_RTT (((u64)1) << 11)
#define DMA_RTADDR_SMT (((u64)1) << 10)

/* CCMD_REG */
#define DMA_CCMD_ICC (((u64)1) << 63)
#define DMA_CCMD_GLOBAL_INVL (((u64)1) << 61)
#define DMA_CCMD_DOMAIN_INVL (((u64)2) << 61)
#define DMA_CCMD_DEVICE_INVL (((u64)3) << 61)
#define DMA_CCMD_FM(m) (((u64)((m) & 0x3)) << 32)
#define DMA_CCMD_MASK_NOBIT 0
#define DMA_CCMD_MASK_1BIT 1
#define DMA_CCMD_MASK_2BIT 2
#define DMA_CCMD_MASK_3BIT 3
#define DMA_CCMD_SID(s) (((u64)((s) & 0xffff)) << 16)
#define DMA_CCMD_DID(d) ((u64)((d) & 0xffff))

/* FECTL_REG */
#define DMA_FECTL_IM (((u32)1) << 31)

/* FSTS_REG */
#define DMA_FSTS_PFO (1 << 0) /* Primary Fault Overflow */
#define DMA_FSTS_PPF (1 << 1) /* Primary Pending Fault */
#define DMA_FSTS_IQE (1 << 4) /* Invalidation Queue Error */
#define DMA_FSTS_ICE (1 << 5) /* Invalidation Completion Error */
#define DMA_FSTS_ITE (1 << 6) /* Invalidation Time-out Error */
#define DMA_FSTS_PRO (1 << 7) /* Page Request Overflow */
#define dma_fsts_fault_record_index(s) (((s) >> 8) & 0xff)

/* FRCD_REG, 32 bits access */
#define DMA_FRCD_F (((u32)1) << 31)
#define dma_frcd_type(d) ((d >> 30) & 1)
#define dma_frcd_fault_reason(c) (c & 0xff)
#define dma_frcd_source_id(c) (c & 0xffff)
/* low 64 bit */
#define dma_frcd_page_addr(d) (d & (((u64)-1) << PAGE_SHIFT))

/* PRS_REG */
#define DMA_PRS_PPR	((u32)1)

#define IOMMU_WAIT_OP(iommu, offset, op, cond, sts)			\
do {									\
	cycles_t start_time = get_cycles();				\
	while (1) {							\
		sts = op(iommu->reg + offset);				\
		if (cond)						\
			break;						\
		if (DMAR_OPERATION_TIMEOUT < (get_cycles() - start_time))\
			panic("DMAR hardware is malfunctioning\n");	\
		cpu_relax();						\
	}								\
} while (0)

#define QI_LENGTH	256	/* queue length */

enum {
	QI_FREE,
	QI_IN_USE,
	QI_DONE,
	QI_ABORT
};

#define QI_CC_TYPE		0x1
#define QI_IOTLB_TYPE		0x2
#define QI_DIOTLB_TYPE		0x3
#define QI_IEC_TYPE		0x4
#define QI_IWD_TYPE		0x5
#define QI_EIOTLB_TYPE		0x6
#define QI_PC_TYPE		0x7
#define QI_DEIOTLB_TYPE		0x8
#define QI_PGRP_RESP_TYPE	0x9
#define QI_PSTRM_RESP_TYPE	0xa

#define QI_IEC_SELECTIVE	(((u64)1) << 4)
#define QI_IEC_IIDEX(idx)	(((u64)(idx & 0xffff) << 32))
#define QI_IEC_IM(m)		(((u64)(m & 0x1f) << 27))

#define QI_IWD_STATUS_DATA(d)	(((u64)d) << 32)
#define QI_IWD_STATUS_WRITE	(((u64)1) << 5)

#define QI_IOTLB_DID(did) 	(((u64)did) << 16)
#define QI_IOTLB_DR(dr) 	(((u64)dr) << 7)
#define QI_IOTLB_DW(dw) 	(((u64)dw) << 6)
#define QI_IOTLB_GRAN(gran) 	(((u64)gran) >> (DMA_TLB_FLUSH_GRANU_OFFSET-4))
#define QI_IOTLB_ADDR(addr)	(((u64)addr) & VTD_PAGE_MASK)
#define QI_IOTLB_IH(ih)		(((u64)ih) << 6)
#define QI_IOTLB_AM(am)		(((u8)am))

#define QI_CC_FM(fm)		(((u64)fm) << 48)
#define QI_CC_SID(sid)		(((u64)sid) << 32)
#define QI_CC_DID(did)		(((u64)did) << 16)
#define QI_CC_GRAN(gran)	(((u64)gran) >> (DMA_CCMD_INVL_GRANU_OFFSET-4))

#define QI_DEV_IOTLB_SID(sid)	((u64)((sid) & 0xffff) << 32)
#define QI_DEV_IOTLB_QDEP(qdep)	(((qdep) & 0x1f) << 16)
#define QI_DEV_IOTLB_ADDR(addr)	((u64)(addr) & VTD_PAGE_MASK)
#define QI_DEV_IOTLB_PFSID(pfsid) (((u64)(pfsid & 0xf) << 12) | ((u64)(pfsid & 0xfff) << 52))
#define QI_DEV_IOTLB_SIZE	1
#define QI_DEV_IOTLB_MAX_INVS	32

#define QI_PC_PASID(pasid)	(((u64)pasid) << 32)
#define QI_PC_DID(did)		(((u64)did) << 16)
#define QI_PC_GRAN(gran)	(((u64)gran) << 4)

#define QI_PC_ALL_PASIDS	(QI_PC_TYPE | QI_PC_GRAN(0))
#define QI_PC_PASID_SEL		(QI_PC_TYPE | QI_PC_GRAN(1))

#define QI_EIOTLB_ADDR(addr)	((u64)(addr) & VTD_PAGE_MASK)
#define QI_EIOTLB_GL(gl)	(((u64)gl) << 7)
#define QI_EIOTLB_IH(ih)	(((u64)ih) << 6)
#define QI_EIOTLB_AM(am)	(((u64)am))
#define QI_EIOTLB_PASID(pasid) 	(((u64)pasid) << 32)
#define QI_EIOTLB_DID(did)	(((u64)did) << 16)
#define QI_EIOTLB_GRAN(gran) 	(((u64)gran) << 4)

#define QI_DEV_EIOTLB_ADDR(a)	((u64)(a) & VTD_PAGE_MASK)
#define QI_DEV_EIOTLB_SIZE	(((u64)1) << 11)
#define QI_DEV_EIOTLB_GLOB(g)	((u64)g)
#define QI_DEV_EIOTLB_PASID(p)	(((u64)p) << 32)
#define QI_DEV_EIOTLB_SID(sid)	((u64)((sid) & 0xffff) << 16)
#define QI_DEV_EIOTLB_QDEP(qd)	((u64)((qd) & 0x1f) << 4)
#define QI_DEV_EIOTLB_PFSID(pfsid) (((u64)(pfsid & 0xf) << 12) | ((u64)(pfsid & 0xfff) << 52))
#define QI_DEV_EIOTLB_MAX_INVS	32

/* Page group response descriptor QW0 */
#define QI_PGRP_PASID_P(p)	(((u64)(p)) << 4)
#define QI_PGRP_PDP(p)		(((u64)(p)) << 5)
#define QI_PGRP_RESP_CODE(res)	(((u64)(res)) << 12)
#define QI_PGRP_DID(rid)	(((u64)(rid)) << 16)
#define QI_PGRP_PASID(pasid)	(((u64)(pasid)) << 32)

/* Page group response descriptor QW1 */
#define QI_PGRP_LPIG(x)		(((u64)(x)) << 2)
#define QI_PGRP_IDX(idx)	(((u64)(idx)) << 3)


#define QI_RESP_SUCCESS		0x0
#define QI_RESP_INVALID		0x1
#define QI_RESP_FAILURE		0xf

#define QI_GRAN_ALL_ALL			0
#define QI_GRAN_NONG_ALL		1
#define QI_GRAN_NONG_PASID		2
#define QI_GRAN_PSI_PASID		3

#define qi_shift(iommu)		(DMAR_IQ_SHIFT + !!ecap_smts((iommu)->ecap))

struct qi_desc {
	u64 qw0;
	u64 qw1;
	u64 qw2;
	u64 qw3;
};

/* 1MB - maximum possible interrupt remapping table size */
#define INTR_REMAP_PAGE_ORDER	8
#define INTR_REMAP_TABLE_REG_SIZE	0xf
#define INTR_REMAP_TABLE_REG_SIZE_MASK  0xf

#define INTR_REMAP_TABLE_ENTRIES	65536

enum {
	SR_DMAR_FECTL_REG,
	SR_DMAR_FEDATA_REG,
	SR_DMAR_FEADDR_REG,
	SR_DMAR_FEUADDR_REG,
	MAX_SR_DMAR_REGS
};

#define VTD_FLAG_TRANS_PRE_ENABLED	(1 << 0)
#define VTD_FLAG_IRQ_REMAP_PRE_ENABLED	(1 << 1)

/*
 * 0: Present
 * 1-11: Reserved
 * 12-63: Context Ptr (12 - (haw-1))
 * 64-127: Reserved
 */
struct root_entry {
	u64     lo;
	u64     hi;
};

/*
 * low 64 bits:
 * 0: present
 * 1: fault processing disable
 * 2-3: translation type
 * 12-63: address space root
 * high 64 bits:
 * 0-2: address width
 * 3-6: aval
 * 8-23: domain id
 */
struct context_entry {
	u64 lo;
	u64 hi;
};

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-10: available
 * 11: snoop behavior
 * 12-63: Host physcial address
 */
struct dma_pte {
	u64 val;
};

#endif
