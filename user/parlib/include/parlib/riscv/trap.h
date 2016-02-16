/* Copyright (c) 2016 Google Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 * See LICENSE for details.
 *
 * Arch-specific defines for traps, vmexits, and similar things */

#pragma once

#include <parlib/common.h>
#include <ros/trapframe.h>

__BEGIN_DECLS

#error fix these numbers

#define HW_TRAP_DIV_ZERO		0
#define HW_TRAP_GP_FAULT		1
#define HW_TRAP_PAGE_FAULT		2

#error implement these
static bool has_refl_fault(struct user_context *ctx)
{
	return 0;
}

static void clear_refl_fault(struct user_context *ctx)
{
}

static unsigned int __arch_refl_get_nr(struct user_context *ctx)
{
	return 0;
}

static unsigned int __arch_refl_get_err(struct user_context *ctx)
{
	return 0;
}

static unsigned long __arch_refl_get_aux(struct user_context *ctx)
{
	return 0;
}

__END_DECLS
