/* Copyright 2015 Google Inc.
 * 
 * See LICENSE for details.
 */

/* We're not going to falll into the trap of only compiling support
 * for AMD OR Intel for an image. It all gets compiled in, and which
 * one you use depends on on cpuinfo, not a compile-time
 * switch. That's proven to be the best strategy.  Conditionally
 * compiling in support is the path to hell.
 */
#include <assert.h>
#include <pmap.h>

// NO . FILES HERE INCLUDE .h
// That forces us to make the includes visible.
#include "intel/vmx_cpufunc.h"
#include "intel/vmcs.h"
#include "intel/vmx.h"
#include "x86.h"
#include "vmm.h"
#include "func.h"

/* this will be the init function for vmm. For now, it just ensures we
   don't break things. */
