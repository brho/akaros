/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * x86 CPU feature detection.
 *
 * You can add new items as needed.  Changing __NR_CPU_FEAT will require
 * a rebuild of the world.  Otherwise, you just need to reinstall kernel
 * headers. */

#pragma once

#define CPU_FEAT_X86_VENDOR_INTEL	(__CPU_FEAT_ARCH_START + 0)
#define CPU_FEAT_X86_VENDOR_AMD		(__CPU_FEAT_ARCH_START + 1)
#define CPU_FEAT_X86_FXSR		(__CPU_FEAT_ARCH_START + 2)
#define CPU_FEAT_X86_XSAVE		(__CPU_FEAT_ARCH_START + 3)
#define CPU_FEAT_X86_XSAVEOPT		(__CPU_FEAT_ARCH_START + 4)
#define CPU_FEAT_X86_FSGSBASE		(__CPU_FEAT_ARCH_START + 5)
#define CPU_FEAT_X86_MWAIT		(__CPU_FEAT_ARCH_START + 6)
#define __NR_CPU_FEAT			(__CPU_FEAT_ARCH_START + 64)
