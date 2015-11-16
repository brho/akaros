/*
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
 *
 * This file is part of libpfm, a performance monitoring support library for
 * applications on Linux.
 *
 * PMU: snb_unc (Intel Sandy Bridge uncore PMU)
 */

static const intel_x86_umask_t snb_unc_cbo_xsnp_response[]={
   { .uname  = "MISS",
     .udesc  = "Number of snoop misses",
     .ucode  = 0x100,
     .grpid  = 0,
   },
   { .uname  = "INVAL",
     .udesc  = "Number of snoop invalidates of a non-modified line",
     .ucode  = 0x200,
     .grpid  = 0,
   },
   { .uname  = "HIT",
     .udesc  = "Number of snoop hits of a non-modified line",
     .ucode  = 0x400,
     .grpid  = 0,
   },
   { .uname  = "HITM",
     .udesc  = "Number of snoop hits of a modified line",
     .ucode  = 0x800,
     .grpid  = 0,
   },
   { .uname  = "INVAL_M",
     .udesc  = "Number of snoop invalidates of a modified line",
     .ucode  = 0x1000,
     .grpid  = 0,
   },
   { .uname  = "ANY_SNP",
     .udesc  = "Number of snoops",
     .ucode  = 0x1f00,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
   { .uname  = "EXTERNAL_FILTER",
     .udesc  = "Filter on cross-core snoops initiated by this Cbox due to external snoop request",
     .ucode  = 0x2000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "XCORE_FILTER",
     .udesc  = "Filter on cross-core snoops initiated by this Cbox due to processor core memory request",
     .ucode  = 0x4000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
   { .uname  = "EVICTION_FILTER",
     .udesc  = "Filter on cross-core snoops initiated by this Cbox due to LLC eviction",
     .ucode  = 0x8000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO,
   },
};

static const intel_x86_umask_t snb_unc_cbo_cache_lookup[]={
   { .uname  = "STATE_M",
     .udesc  = "Number of LLC lookup requests for a line in modified state",
     .ucode  = 0x100,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "STATE_E",
     .udesc  = "Number of LLC lookup requests for a line in exclusive state",
     .ucode  = 0x200,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "STATE_S",
     .udesc  = "Number of LLC lookup requests for a line in shared state",
     .ucode  = 0x400,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "STATE_I",
     .udesc  = "Number of LLC lookup requests for a line in invalid state",
     .ucode  = 0x800,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "STATE_MESI",
     .udesc  = "Number of LLC lookup requests for a line",
     .ucode  = 0xf00,
     .grpid  = 0,
     .uflags = INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
   { .uname  = "READ_FILTER",
     .udesc  = "Filter on processor core initiated cacheable read requests",
     .ucode  = 0x1000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "WRITE_FILTER",
     .udesc  = "Filter on processor core initiated cacheable write requests",
     .ucode  = 0x2000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "EXTSNP_FILTER",
     .udesc  = "Filter on external snoop requests",
     .ucode  = 0x4000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO,
   },
   { .uname  = "ANY_FILTER",
     .udesc  = "Filter on any IRQ or IPQ initiated requests including uncacheable, non-coherent requests",
     .ucode  = 0x8000,
     .grpid  = 1,
     .uflags = INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
};

static const intel_x86_umask_t snb_unc_arb_trk_occupancy[]={
   { .uname  = "ALL",
     .udesc  = "Counts cycles weighted by the number of requests waiting for data returning from the memory controller, (includes coherent and non-coherent requests initiated by cores, processor graphic units, or LLC)",
     .ucode = 0x100,
     .uflags= INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
};

static const intel_x86_umask_t snb_unc_arb_trk[]={
   { .uname  = "ALL",
     .udesc  = "Counts number of coherent and in-coherent requests initiated by cores, processor graphic units, or LLC",
     .ucode = 0x100,
     .uflags= INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
   { .uname  = "WRITES",
     .udesc  = "Counts the number of allocated write entries, include full, partial, and LLC evictions",
     .ucode = 0x2000,
     .uflags= INTEL_X86_NCOMBO,
   },
   { .uname  = "EVICTIONS",
     .udesc  = "Counts the number of LLC evictions allocated",
     .ucode = 0x8000,
     .uflags= INTEL_X86_NCOMBO,
   },
};

static const intel_x86_umask_t snb_unc_arb_coh_trk_occupancy[]={
   { .uname  = "ALL",
     .udesc  = "Cycles weighted by number of requests pending in Coherency Tracker",
     .ucode = 0x100,
     .uflags= INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
};

static const intel_x86_umask_t snb_unc_arb_coh_trk_request[]={
   { .uname  = "ALL",
     .udesc  = "Number of requests allocated in Coherency Tracker",
     .ucode = 0x100,
     .uflags= INTEL_X86_NCOMBO | INTEL_X86_DFL,
   },
};

static const intel_x86_entry_t intel_snb_unc_cbo0_pe[]={
{ .name   = "UNC_CLOCKTICKS",
  .desc   = "uncore clock ticks",
  .cntmsk = 1ULL << 32,
  .code = 0xff, /* perf_event pseudo encoding */
  .flags = INTEL_X86_FIXED,
},
{ .name   = "UNC_CBO_XSNP_RESPONSE",
  .desc   = "Snoop responses",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0xff,
  .code = 0x22,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_cbo_xsnp_response),
  .ngrp = 2,
  .umasks = snb_unc_cbo_xsnp_response,
},
{ .name   = "UNC_CBO_CACHE_LOOKUP",
  .desc   = "LLC cache lookups",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0xff,
  .code = 0x34,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_cbo_cache_lookup),
  .ngrp = 2,
  .umasks = snb_unc_cbo_cache_lookup,
},
};

static const intel_x86_entry_t intel_snb_unc_cbo_pe[]={
{ .name   = "UNC_CBO_XSNP_RESPONSE",
  .desc   = "Snoop responses (must provide a snoop type and filter)",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0xff,
  .code = 0x22,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_cbo_xsnp_response),
  .ngrp = 2,
  .umasks = snb_unc_cbo_xsnp_response,
},
{ .name   = "UNC_CBO_CACHE_LOOKUP",
  .desc   = "LLC cache lookups",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0xff,
  .code = 0x34,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_cbo_cache_lookup),
  .ngrp = 2,
  .umasks = snb_unc_cbo_cache_lookup,
},
};

static const intel_x86_entry_t intel_snb_unc_arb_pe[]={
{ .name   = "UNC_ARB_TRK_OCCUPANCY",
  .desc   = "ARB tracker occupancy",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0x1,
  .code = 0x80,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_arb_trk_occupancy),
  .ngrp = 1,
  .umasks = snb_unc_arb_trk_occupancy,
},
{ .name   = "UNC_ARB_COH_TRK_OCCUPANCY",
  .desc   = "Coherency traffic occupancy",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0x1,
  .code = 0x83,
  .flags= INTEL_X86_PEBS,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_arb_coh_trk_occupancy),
  .ngrp = 1,
  .umasks = snb_unc_arb_coh_trk_occupancy,
},
{ .name   = "UNC_ARB_COH_TRK_REQUEST",
  .desc   = "Coherency traffic requests",
  .modmsk = INTEL_SNB_UNC_ATTRS,
  .cntmsk = 0x1,
  .code = 0x84,
  .numasks = LIBPFM_ARRAY_SIZE(snb_unc_arb_coh_trk_request),
  .ngrp = 1,
  .umasks = snb_unc_arb_coh_trk_request,
},
};
