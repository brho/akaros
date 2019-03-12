/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * RISC-V CPU feature detection.
 *
 * You can add new items as needed.  Changing __NR_CPU_FEAT will require
 * a rebuild of the world.  Otherwise, you just need to reinstall kernel
 * headers. */

#pragma once

#define CPU_FEAT_RISCV_FOO	(__CPU_FEAT_ARCH_START + 0)
#define __NR_CPU_FEAT		(__CPU_FEAT_ARCH_START + 64)
