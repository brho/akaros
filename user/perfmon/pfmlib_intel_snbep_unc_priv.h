/*
 * pfmlib_intel_snbep_unc_priv.c : Intel SandyBridge/IvyBridge-EP common definitions
 *
 * Copyright (c) 2012 Google, Inc
 * Contributed by Stephane Eranian <eranian@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef __PFMLIB_INTEL_SNBEP_UNC_PRIV_H__
#define __PFMLIB_INTEL_SNBEP_UNC_PRIV_H__

/*
 * Intel x86 specific pmu flags (pmu->flags 16 MSB)
 */
#define INTEL_PMU_FL_UNC_OCC 0x10000	/* PMU has occupancy counter filters */
#define INTEL_PMU_FL_UNC_CBO 0x20000	/* PMU is Cbox */


#define SNBEP_UNC_ATTR_E		0
#define SNBEP_UNC_ATTR_I		1
#define SNBEP_UNC_ATTR_T8		2
#define SNBEP_UNC_ATTR_T5		3
#define SNBEP_UNC_ATTR_TF		4
#define SNBEP_UNC_ATTR_CF		5
#define SNBEP_UNC_ATTR_NF		6 /* for filter0 */
#define SNBEP_UNC_ATTR_FF		7
#define SNBEP_UNC_ATTR_A		8
#define SNBEP_UNC_ATTR_NF1		9 /* for filter1 */
#define SNBEP_UNC_ATTR_ISOC	       10 /* isochronous */
#define SNBEP_UNC_ATTR_NC	       11 /* non-coherent */

#define _SNBEP_UNC_ATTR_I	(1 << SNBEP_UNC_ATTR_I)
#define _SNBEP_UNC_ATTR_E	(1 << SNBEP_UNC_ATTR_E)
#define _SNBEP_UNC_ATTR_T8	(1 << SNBEP_UNC_ATTR_T8)
#define _SNBEP_UNC_ATTR_T5	(1 << SNBEP_UNC_ATTR_T5)
#define _SNBEP_UNC_ATTR_TF	(1 << SNBEP_UNC_ATTR_TF)
#define _SNBEP_UNC_ATTR_CF	(1 << SNBEP_UNC_ATTR_CF)
#define _SNBEP_UNC_ATTR_NF	(1 << SNBEP_UNC_ATTR_NF)
#define _SNBEP_UNC_ATTR_FF	(1 << SNBEP_UNC_ATTR_FF)
#define _SNBEP_UNC_ATTR_A	(1 << SNBEP_UNC_ATTR_A)
#define _SNBEP_UNC_ATTR_NF1	(1 << SNBEP_UNC_ATTR_NF1)
#define _SNBEP_UNC_ATTR_ISOC	(1 << SNBEP_UNC_ATTR_ISOC)
#define _SNBEP_UNC_ATTR_NC	(1 << SNBEP_UNC_ATTR_NC)

#define SNBEP_UNC_IRP_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_R3QPI_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_R3QPI_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_R2PCIE_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_R2PCIE_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_QPI_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_QPI_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_UBO_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_UBO_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)


#define SNBEP_UNC_PCU_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T5)

#define IVBEP_UNC_PCU_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T5)

#define SNBEP_UNC_PCU_BAND_ATTRS \
	(SNBEP_UNC_PCU_ATTRS | _SNBEP_UNC_ATTR_FF)

#define IVBEP_UNC_PCU_BAND_ATTRS \
	(IVBEP_UNC_PCU_ATTRS | _SNBEP_UNC_ATTR_FF)

#define SNBEP_UNC_IMC_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_IMC_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_CBO_ATTRS   \
	(_SNBEP_UNC_ATTR_I   |\
	 _SNBEP_UNC_ATTR_E   |\
	 _SNBEP_UNC_ATTR_T8  |\
	 _SNBEP_UNC_ATTR_CF  |\
	 _SNBEP_UNC_ATTR_TF)

#define IVBEP_UNC_CBO_ATTRS   \
	(_SNBEP_UNC_ATTR_E   |\
	 _SNBEP_UNC_ATTR_T8  |\
	 _SNBEP_UNC_ATTR_CF  |\
	 _SNBEP_UNC_ATTR_TF)

#define SNBEP_UNC_CBO_NID_ATTRS	\
	(SNBEP_UNC_CBO_ATTRS|_SNBEP_UNC_ATTR_NF)

#define IVBEP_UNC_CBO_NID_ATTRS	\
	(IVBEP_UNC_CBO_ATTRS|_SNBEP_UNC_ATTR_NF1)

#define SNBEP_UNC_HA_ATTRS \
	(_SNBEP_UNC_ATTR_I|_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define IVBEP_UNC_HA_ATTRS \
	(_SNBEP_UNC_ATTR_E|_SNBEP_UNC_ATTR_T8)

#define SNBEP_UNC_HA_OPC_ATTRS \
	(SNBEP_UNC_HA_ATTRS|_SNBEP_UNC_ATTR_A)

typedef union {
	uint64_t val;
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_umask:8;	/* unit mask */
		unsigned long unc_res1:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detec */
		unsigned long unc_res2:3;	/* reserved */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_inv:1;	/* invert counter mask */
		unsigned long unc_thres:8;	/* counter mask */
		unsigned long unc_res3:32;	/* reserved */
	} com; /* covers common fields for cbox, ha, imc, ubox, r2pcie, r3qpi */
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_umask:8;	/* unit mask */
		unsigned long unc_res1:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detect */
		unsigned long unc_tid:1;	/* tid filter enable */
		unsigned long unc_res2:2;	/* reserved */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_inv:1;	/* invert counter mask */
		unsigned long unc_thres:8;	/* counter mask */
		unsigned long unc_res3:32;	/* reserved */
	} cbo; /* covers c-box */
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_res1:6;	/* reserved */
		unsigned long unc_occ:2;	/* occ select */
		unsigned long unc_res2:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detec */
		unsigned long unc_res3:1;	/* reserved */
		unsigned long unc_res4:2;	/* reserved */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_inv:1;	/* invert counter mask */
		unsigned long unc_thres:5;	/* threshold */
		unsigned long unc_res5:1;	/* reserved */
		unsigned long unc_occ_inv:1;	/* occupancy invert */
		unsigned long unc_occ_edge:1;	/* occupancy edge detect */
		unsigned long unc_res6:32;	/* reserved */
	} pcu; /* covers pcu */
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_res1:6;	/* reserved */
		unsigned long unc_occ:2;	/* occ select */
		unsigned long unc_res2:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detec */
		unsigned long unc_res3:1;	/* reserved */
		unsigned long unc_ov_en:1;	/* overflow enable */
		unsigned long unc_sel_ext:1;	/* event_sel extension */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_res4:1;	/* reserved */
		unsigned long unc_thres:5;	/* threshold */
		unsigned long unc_res5:1;	/* reserved */
		unsigned long unc_occ_inv:1;	/* occupancy invert */
		unsigned long unc_occ_edge:1;	/* occupancy edge detect */
		unsigned long unc_res6:32;	/* reserved */
	} ivbep_pcu; /* covers ivb-ep pcu */
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_umask:8;	/* unit maks */
		unsigned long unc_res1:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detec */
		unsigned long unc_res2:1;	/* reserved */
		unsigned long unc_res3:1;	/* reserved */
		unsigned long unc_event_ext:1;	/* event code extension */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_inv:1;	/* invert counter mask */
		unsigned long unc_thres:8;	/* threshold */
		unsigned long unc_res4:32;	/* reserved */
	} qpi; /* covers qpi */
	struct {
		unsigned long tid:1;
		unsigned long cid:3;
		unsigned long res0:1;
		unsigned long res1:3;
		unsigned long res2:2;
		unsigned long nid:8;
		unsigned long state:5;
		unsigned long opc:9;
		unsigned long res3:1;
		unsigned long res4:32;
	} cbo_filt; /* cbox filter */
	struct {
		unsigned long tid:1;
		unsigned long cid:4;
		unsigned long res0:12;
		unsigned long state:6;
		unsigned long res1:9;
		unsigned long res2:32;
	} ivbep_cbo_filt0; /* ivbep cbox filter0 */
	struct {
		unsigned long nid:16;
		unsigned long res0:4;
		unsigned long opc:9;
		unsigned long res1:1;
		unsigned long nc:1;
		unsigned long isoc:1;
		unsigned long res2:32;
	} ivbep_cbo_filt1; /* ivbep cbox filter1 */
	struct {
		unsigned long filt0:8; /* band0 freq filter */
		unsigned long filt1:8; /* band1 freq filter */
		unsigned long filt2:8; /* band2 freq filter */
		unsigned long filt3:8; /* band3 freq filter */
		unsigned long res1:32; /* reserved */
	} pcu_filt;
	struct {
		unsigned long res1:6;
		unsigned long lo_addr:26; /* lo order 26b */
		unsigned long hi_addr:14; /* hi order 14b */
		unsigned long res2:18; /* reserved */
	} ha_addr;
	struct {
		unsigned long opc:6; /* opcode match */
		unsigned long res1:26; /* reserved */
		unsigned long res2:32; /* reserved */
	} ha_opc;
	struct {
		unsigned long unc_event:8;	/* event code */
		unsigned long unc_umask:8;	/* unit mask */
		unsigned long unc_res1:1;	/* reserved */
		unsigned long unc_rst:1;	/* reset */
		unsigned long unc_edge:1;	/* edge detec */
		unsigned long unc_res2:3;	/* reserved */
		unsigned long unc_en:1;		/* enable */
		unsigned long unc_res3:1;	/* reserved */
		unsigned long unc_thres:8;	/* counter mask */
		unsigned long unc_res4:32;	/* reserved */
	} irp; /* covers irp */
} pfm_snbep_unc_reg_t;

extern void pfm_intel_snbep_unc_perf_validate_pattrs(void *this, pfmlib_event_desc_t *e);
extern int  pfm_intel_snbep_unc_get_encoding(void *this, pfmlib_event_desc_t *e);
extern const pfmlib_attr_desc_t snbep_unc_mods[];
extern int  pfm_intel_snbep_unc_detect(void *this);
extern int  pfm_intel_ivbep_unc_detect(void *this);
extern int  pfm_intel_snbep_unc_get_perf_encoding(void *this, pfmlib_event_desc_t *e);
extern int  pfm_intel_snbep_unc_can_auto_encode(void *this, int pidx, int uidx);
extern int pfm_intel_snbep_unc_get_event_attr_info(void *this, int pidx, int attr_idx, pfm_event_attr_info_t *info);

static inline int
is_cbo_filt_event(void *this, pfm_intel_x86_reg_t reg)
{
	pfmlib_pmu_t *pmu = this;
	uint64_t sel = reg.sel_event_select;
	/*
	 * umask bit 0 must be 1 (OPCODE)
	 * TOR_INSERT: event code 0x35
	 * TOR_OCCUPANCY: event code 0x36
	 * LLC_LOOKUP : event code 0x34
	 */
	return (pmu->flags & INTEL_PMU_FL_UNC_CBO)
		&& (reg.sel_unit_mask & 0x1)
		&& (sel == 0x35 || sel == 0x36 || sel == 0x34);
}

#endif /* __PFMLIB_INTEL_SNBEP_UNC_PRIV_H__ */
