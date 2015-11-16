/*
 * pfmlib_intel_rapl.c : Intel RAPL PMU
 *
 * Copyright (c) 2013 Google, Inc
 * Contributed by Stephane Eranian <eranian@gmail.com>
 *
 * Based on:
 * Copyright (c) 2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
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
 *
 * RAPL PMU (SNB, IVB, HSW)
 */

/* private headers */
#include "pfmlib_priv.h"
/*
 * for now, we reuse the x86 table entry format and callback to avoid duplicating
 * code. We may revisit this later on
 */
#include "pfmlib_intel_x86_priv.h"

extern pfmlib_pmu_t intel_rapl_support;

#define RAPL_COMMON_EVENTS \
  { .name   = "RAPL_ENERGY_CORES",\
    .desc   = "Number of Joules consumed by all cores on the package. Unit is 2^-32 Joules",\
    .cntmsk = 0x1,\
    .code   = 0x1,\
  },\
  { .name   = "RAPL_ENERGY_PKG",\
    .desc   = "Number of Joules consumed by all cores and Last level cache on the package. Unit is 2^-32 Joules",\
    .cntmsk = 0x2,\
    .code   = 0x2,\
  }

static const intel_x86_entry_t intel_rapl_cln_pe[]={
  RAPL_COMMON_EVENTS,
  { .name   = "RAPL_ENERGY_GPU",
    .desc   = "Number of Joules consumed by the builtin GPU. Unit is 2^-32 Joules",
    .cntmsk = 0x8,
    .code   = 0x4,
  }
};

static const intel_x86_entry_t intel_rapl_srv_pe[]={
  RAPL_COMMON_EVENTS,
  { .name   = "RAPL_ENERGY_DRAM",
    .desc   = "Number of Joules consumed by the DRAM. Unit is 2^-32 Joules",
    .cntmsk = 0x4,
    .code   = 0x3,
  },
};

static int
pfm_rapl_detect(void *this)
{
	int ret;

	ret = pfm_intel_x86_detect();
	if (ret != PFM_SUCCESS)
		return ret;

	if (pfm_intel_x86_cfg.family != 6)
		return PFM_ERR_NOTSUPP;

	switch(pfm_intel_x86_cfg.model) {
		case 42: /* Sandy Bridge */
		case 58: /* Ivy Bridge */
		case 60: /* Haswell */
		case 69: /* Haswell */
		case 70: /* Haswell */
		case 71: /* Haswell */
			 /* already setup by default */
			  break;
		case 45: /* Sandy Bridg-EP  */
		case 62: /* Ivy Bridge-EP  */
			intel_rapl_support.pe 	     = intel_rapl_srv_pe;
			intel_rapl_support.pme_count = LIBPFM_ARRAY_SIZE(intel_rapl_srv_pe);
			break;
		default:
			return PFM_ERR_NOTSUPP;
	}
	return PFM_SUCCESS;
}

static int
pfm_intel_rapl_get_encoding(void *this, pfmlib_event_desc_t *e)

{
	const intel_x86_entry_t *pe;

	pe = this_pe(this);

	e->fstr[0] = '\0';

	e->codes[0] = pe[e->event].code;
	e->count = 1;
	evt_strcat(e->fstr, "%s", pe[e->event].name);

	__pfm_vbprintf("[0x%"PRIx64" event=0x%x] %s\n",
		       e->codes[0],
		       e->codes[0], e->fstr);

	return PFM_SUCCESS;
}

/*
 * number modifiers for RAPL
 * define an empty modifier to avoid firing the
 * sanity pfm_intel_x86_validate_table(). We are
 * using this function to avoid duplicating code.
 */
static const pfmlib_attr_desc_t rapl_mods[]=
{};

pfmlib_pmu_t intel_rapl_support={
	.desc			= "Intel RAPL",
	.name			= "rapl",
	.perf_name		= "power",
	.pmu			= PFM_PMU_INTEL_RAPL,
	.pme_count		= LIBPFM_ARRAY_SIZE(intel_rapl_cln_pe),
	.type			= PFM_PMU_TYPE_UNCORE,
	.num_cntrs		= 0,
	.num_fixed_cntrs	= 3,
	.max_encoding		= 1,
	.pe			= intel_rapl_cln_pe, /* default, maybe updated */
	.pmu_detect		= pfm_rapl_detect,
	.atdesc			= rapl_mods,

	.get_event_encoding[PFM_OS_NONE] = pfm_intel_rapl_get_encoding,
	 PFMLIB_ENCODE_PERF(pfm_intel_x86_get_perf_encoding),
	 PFMLIB_OS_DETECT(pfm_intel_x86_perf_detect), \
	.get_event_first	= pfm_intel_x86_get_event_first,
	.get_event_next		= pfm_intel_x86_get_event_next,
	.event_is_valid		= pfm_intel_x86_event_is_valid,
	.validate_table		= pfm_intel_x86_validate_table,
	.get_event_info		= pfm_intel_x86_get_event_info,
	.get_event_attr_info	= pfm_intel_x86_get_event_attr_info,
	 PFMLIB_VALID_PERF_PATTRS(pfm_intel_x86_perf_validate_pattrs),
	.get_event_nattrs	= pfm_intel_x86_get_event_nattrs,
};
