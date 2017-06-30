/* Copyright (c) 2016 Google Inc.
 * See LICENSE for details.
 *
 * Linux emulation for virtual machines. */

#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <vmm/vmm.h>
#include <errno.h>
#include <sys/syscall.h>
#include <vmm/linux_syscalls.h>
#include <sys/time.h>

/* TODO: have an array which classifies syscall args
 * and "special" system calls (ones with weird return
 * values etc.). For some cases, we don't even do a system
 * call, and in many cases we have to rearrange arguments
 * since Linux and Akaros don't share signatures, so this
 * gets tricky. */
bool
linuxemu(struct guest_thread *gth, struct vm_trapframe *tf)
{
	bool ret = false;

	fprintf(stderr, "vmcall(%s(%d), %p, %p, %p, %p, %p, %p);\n",
	        tf->tf_rax < 311 ? syscalls[tf->tf_rax] : "",
	        tf->tf_rax, tf->tf_rdi, tf->tf_rsi, tf->tf_rdx,
	        tf->tf_rcx, tf->tf_r8, tf->tf_r9);

	tf->tf_rip += 3;
	/* we don't do tic/tou checking here yet. */
	switch (tf->tf_rax) {
	default:
		/* TODO: just return the error to the guest once we are done
		 * debugging this. */
		fprintf(stderr, "System call %d is not implemented\n", tf->tf_rax);
		//tf->tf_rax = -ENOSYS;
		break;
	case DUNE_SYS_GETTIMEOFDAY:
		tf->tf_rax = gettimeofday((struct timeval*)tf->tf_rdi,
		                          (struct timezone*)tf->tf_rsi);
		ret = true;
		break;
	case DUNE_SYS_GETTID:
		tf->tf_rax = 1;
		ret = true;
		break;
	case DUNE_SYS_WRITE:
		/* debug: write to stdout too for now. */
		write(2, (void *)tf->tf_rsi, (size_t)tf->tf_rdx);
		tf->tf_rax = write(tf->tf_rdi, (void *)tf->tf_rsi, (size_t)tf->tf_rdx);
		ret = true;
		break;
	}
	return ret;
}
