#pragma once

/*
 * vmx.h: VMX Architecture related definitions
 * Copyright (c) 2004, Intel Corporation.
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
 *
 * A few random additions are:
 * Copyright (C) 2006 Qumranet
 *    Avi Kivity <avi@qumranet.com>
 *    Yaniv Kamay <yaniv@qumranet.com>
 *
 */

#include <ros/arch/vmx.h>

#define INTEL_MSR_WRITE_OFFSET			2048

#define INTEL_X2APIC_MSR_START			0x100
#define INTEL_X2APIC_MSR_LENGTH			(0x40/8)

int vmx_init(void);
void vmx_exit(void);
int intel_vmm_init(void);
int intel_vmm_pcpu_init(void);
int ept_fault_pages(void *dir, uint32_t start, uint32_t end);
int ept_check_page(void *dir, unsigned long addr);
int vmx_do_ept_fault(void *dir, unsigned long gpa, unsigned long gva, int fault_flags);

static inline void native_store_idt(pseudodesc_t *dtr);
static inline unsigned long get_desc_base(const struct desc_struct *desc);
static inline void native_store_gdt(pseudodesc_t *dtr);
static inline bool cpu_has_secondary_exec_ctrls(void);
static inline bool cpu_has_vmx_vpid(void);
static inline bool cpu_has_vmx_invpcid(void);
static inline bool cpu_has_vmx_invvpid_single(void);
static inline bool cpu_has_vmx_invvpid_global(void);
static inline bool cpu_has_vmx_ept(void);
static inline bool cpu_has_vmx_invept(void);
static inline bool cpu_has_vmx_invept_individual_addr(void);
static inline bool cpu_has_vmx_invept_context(void);
static inline bool cpu_has_vmx_invept_global(void);
static inline bool cpu_has_vmx_ept_ad_bits(void);
static inline bool cpu_has_vmx_ept_execute_only(void);
static inline bool cpu_has_vmx_eptp_uncacheable(void);
static inline bool cpu_has_vmx_eptp_writeback(void);
static inline bool cpu_has_vmx_ept_2m_page(void);
static inline bool cpu_has_vmx_ept_1g_page(void);
static inline bool cpu_has_vmx_ept_4levels(void);
static inline void __invept(int ext, uint64_t eptp, gpa_t gpa);
static inline void ept_sync_global(void);
static inline void ept_sync_context(uint64_t eptp);
static inline void ept_sync_individual_addr(uint64_t eptp, gpa_t gpa);
static inline void __vmxon(uint64_t addr);
static inline void __vmxoff(void);
static inline void __invvpid(int ext, uint16_t vpid, gva_t gva);
static inline void vpid_sync_gpc_single(uint16_t vpid);
static inline void vpid_sync_gpc_global(void);
static inline void vpid_sync_context(uint16_t vpid);
static inline uint64_t gpc_get_eptp(struct guest_pcore *gpc);

/* no way to get around some of this stuff. */
/* we will do the bare minimum required. */
static inline void native_store_idt(pseudodesc_t *dtr)
{
	asm volatile("sidt %0":"=m" (*dtr));
}

static inline unsigned long get_desc_base(const struct desc_struct *desc)
{
	return (unsigned)(desc->base0 | ((desc->base1) << 16) | ((desc->base2) << 24));
}

#define store_gdt(dtr)                          native_store_gdt(dtr)
static inline void native_store_gdt(pseudodesc_t *dtr)
{
        asm volatile("sgdt %0":"=m" (*dtr));
}

static inline bool cpu_has_secondary_exec_ctrls(void)
{
	return vmcs_config.cpu_based_exec_ctrl &
		CPU_BASED_ACTIVATE_SECONDARY_CONTROLS;
}

static inline bool cpu_has_vmx_vpid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_VPID;
}

static inline bool cpu_has_vmx_invpcid(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_INVPCID;
}

static inline bool cpu_has_vmx_invvpid_single(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_SINGLE_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invvpid_global(void)
{
	return vmx_capability.vpid & VMX_VPID_EXTENT_GLOBAL_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_ept(void)
{
	return vmcs_config.cpu_based_2nd_exec_ctrl &
		SECONDARY_EXEC_ENABLE_EPT;
}

static inline bool cpu_has_vmx_invept(void)
{
	return vmx_capability.ept & VMX_EPT_INVEPT_BIT;
}

/* the SDM (2015-01) doesn't mention this ability (still?) */
static inline bool cpu_has_vmx_invept_individual_addr(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_INDIVIDUAL_BIT;
}

static inline bool cpu_has_vmx_invept_context(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_CONTEXT_BIT;
}

static inline bool cpu_has_vmx_invept_global(void)
{
	return vmx_capability.ept & VMX_EPT_EXTENT_GLOBAL_BIT;
}

static inline bool cpu_has_vmx_ept_ad_bits(void)
{
	return vmx_capability.ept & VMX_EPT_AD_BIT;
}

static inline bool cpu_has_vmx_ept_execute_only(void)
{
	return vmx_capability.ept & VMX_EPT_EXECUTE_ONLY_BIT;
}

static inline bool cpu_has_vmx_eptp_uncacheable(void)
{
	return vmx_capability.ept & VMX_EPTP_UC_BIT;
}

static inline bool cpu_has_vmx_eptp_writeback(void)
{
	return vmx_capability.ept & VMX_EPTP_WB_BIT;
}

static inline bool cpu_has_vmx_ept_2m_page(void)
{
	return vmx_capability.ept & VMX_EPT_2MB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_1g_page(void)
{
	return vmx_capability.ept & VMX_EPT_1GB_PAGE_BIT;
}

static inline bool cpu_has_vmx_ept_4levels(void)
{
	return vmx_capability.ept & VMX_EPT_PAGE_WALK_4_BIT;
}

static inline void __invept(int ext, uint64_t eptp, gpa_t gpa)
{
	struct {
		uint64_t eptp, gpa;
	} operand = {eptp, gpa};

	asm volatile (ASM_VMX_INVEPT
			/* CF==1 or ZF==1 --> rc = -1 */
			"; ja 1f ; ud2 ; 1:\n"
			: : "a" (&operand), "c" (ext) : "cc", "memory");
}

/* We assert support for the global flush during ept_init() */
static inline void ept_sync_global(void)
{
	__invept(VMX_EPT_EXTENT_GLOBAL, 0, 0);
}

static inline void ept_sync_context(uint64_t eptp)
{
	if (cpu_has_vmx_invept_context())
		__invept(VMX_EPT_EXTENT_CONTEXT, eptp, 0);
	else
		ept_sync_global();
}

static inline void ept_sync_individual_addr(uint64_t eptp, gpa_t gpa)
{
	if (cpu_has_vmx_invept_individual_addr())
		__invept(VMX_EPT_EXTENT_INDIVIDUAL_ADDR,
				eptp, gpa);
	else
		ept_sync_context(eptp);
}

static inline void __vmxon(uint64_t addr)
{
	asm volatile (ASM_VMX_VMXON_RAX
			: : "a"(&addr), "m"(addr)
			: "memory", "cc");
}

static inline void __vmxoff(void)
{
	asm volatile (ASM_VMX_VMXOFF : : : "cc");
}

static inline void __invvpid(int ext, uint16_t vpid, gva_t gva)
{
    struct {
	uint64_t vpid : 16;
	uint64_t rsvd : 48;
	uint64_t gva;
    } operand = { vpid, 0, gva };

    asm volatile (ASM_VMX_INVVPID
		  /* CF==1 or ZF==1 --> rc = -1 */
		  "; ja 1f ; ud2 ; 1:"
		  : : "a"(&operand), "c"(ext) : "cc", "memory");
}

static inline void vpid_sync_gpc_single(uint16_t vpid)
{
	if (vpid == 0) {
		return;
	}

	if (cpu_has_vmx_invvpid_single())
		__invvpid(VMX_VPID_EXTENT_SINGLE_CONTEXT, vpid, 0);
}

static inline void vpid_sync_gpc_global(void)
{
	if (cpu_has_vmx_invvpid_global())
		__invvpid(VMX_VPID_EXTENT_ALL_CONTEXT, 0, 0);
}

static inline void vpid_sync_context(uint16_t vpid)
{
	if (cpu_has_vmx_invvpid_single())
		vpid_sync_gpc_single(vpid);
	else
		vpid_sync_gpc_global();
}

static inline uint64_t gpc_get_eptp(struct guest_pcore *gpc)
{
	return gpc->proc->env_pgdir.eptp;
}

static inline unsigned long vmcs_read(unsigned long field)
{
	unsigned long value;

	asm volatile (ASM_VMX_VMREAD_RDX_RAX : "=a"(value) : "d"(field) : "cc");
	return value;
}

/* Returns true if the op succeeded.  It can fail if the field is unsupported */
static inline bool vmcs_write(unsigned long field, unsigned long value)
{
	uint8_t error;

	asm volatile (ASM_VMX_VMWRITE_RAX_RDX "; setna %0"
	              : "=q"(error) : "a"(value), "d"(field) : "cc");
	return error ? FALSE : TRUE;
}

/*
 * VMX Execution Controls (vmxec)
 * Some bits can be set, others can not (i.e. they are reserved).
 *
 * o all bits listed in here must set or clear all the bits in a word
 *   that are not reserved (coverage).
 * o no bits listed in one of these elements is listed in
 *   another element (conflict)
 * o you are allowed to specify a bit that matches a reserved value
 *   (because it might be settable at some future time).
 * o do your best to find symbolic names for the set_to_1 and set_to_0 values.
 *   In the one case we could not find a name, it turned out to be an
 *   error in kvm constants that went back to the earliest days.
 * We're hoping you almost never have to change this. It's painful.
 * The assumption going in is that the 5 MSRs that define the vmxec
 * values are relatively static. This has been the case for a while.
 */
struct vmxec {
	char *name;
	uint32_t msr;
	uint32_t truemsr;
	uint32_t must_be_1;
	uint32_t must_be_0;
	uint32_t try_set_1;
	uint32_t try_set_0;
};

void vmx_load_guest_pcore(struct guest_pcore *gpc);
void vmx_unload_guest_pcore(struct guest_pcore *gpc);
void vmx_clear_vmcs(void);
