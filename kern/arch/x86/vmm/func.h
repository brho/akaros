/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VMM_FUNC_H_
#define	_VMM_FUNC_H_

/* APIs to inject faults into the guest */
void vm_inject_fault(void *vm, int vcpuid, int vector, int errcode_valid,
    int errcode);

static __inline void
vm_inject_ud(void *vm, int vcpuid)
{
	vm_inject_fault(vm, vcpuid, T_ILLOP, 0, 0);
}

static __inline void
vm_inject_gp(void *vm, int vcpuid)
{
	vm_inject_fault(vm, vcpuid, T_GPFLT, 1, 0);
}

static __inline void
vm_inject_ac(void *vm, int vcpuid, int errcode)
{
	vm_inject_fault(vm, vcpuid, T_ALIGN, 1, errcode);
}

static __inline void
vm_inject_ss(void *vm, int vcpuid, int errcode)
{
	vm_inject_fault(vm, vcpuid, T_STACK, 1, errcode);
}

void vm_inject_pf(void *vm, int vcpuid, int error_code, uint64_t cr2);

int vm_restart_instruction(void *vm, int vcpuid);

#endif	/* _VMM_FUNC_H_ */
