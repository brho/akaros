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
#include <vmm/linuxemu.h>
#include <dlfcn.h>

bool dune_sys_read(struct vm_trapframe *tf)
{
	int retval = read(tf->tf_rdi, (void *)tf->tf_rsi, (size_t)tf->tf_rdx);

	if (retval == -1)
		tf->tf_rax = -errno;
	else
		tf->tf_rax = retval;

	return true;
}

bool dune_sys_write(struct vm_trapframe *tf)
{
	int retval = write(tf->tf_rdi, (void *)tf->tf_rsi, (size_t)tf->tf_rdx);

	if (retval == -1)
		tf->tf_rax = -errno;
	else
		tf->tf_rax = retval;

	return true;
}

bool dune_sys_gettid(struct vm_trapframe *tf)
{
	tf->tf_rax = tf->tf_guest_pcoreid;
	return true;
}

bool dune_sys_gettimeofday(struct vm_trapframe *tf)
{
	int retval = gettimeofday((struct timeval*)tf->tf_rdi,
	                          (struct timezone*)tf->tf_rsi);
	if (retval == -1)
		tf->tf_rax = -errno;
	else
		tf->tf_rax = retval;

	return true;
}

void init_syscall_table(void)
{
	int i;

	for (i = 0; i < dune_max_syscall ; ++i) {
		dune_syscall_table[i].call = NULL;
		dune_syscall_table[i].name = "nosyscall";
	}
	// For now setup the syscalls here,
	// there is probably a better way to do this.
	dune_syscall_table[DUNE_SYS_WRITE].call = dune_sys_write;
	dune_syscall_table[DUNE_SYS_WRITE].name = syscalls[DUNE_SYS_WRITE];
	dune_syscall_table[DUNE_SYS_GETTID].call = dune_sys_gettid;
	dune_syscall_table[DUNE_SYS_GETTID].name = syscalls[DUNE_SYS_GETTID];
	dune_syscall_table[DUNE_SYS_GETTIMEOFDAY].call = dune_sys_gettimeofday;
	dune_syscall_table[DUNE_SYS_GETTIMEOFDAY].name =
		syscalls[DUNE_SYS_GETTIMEOFDAY];
	dune_syscall_table[DUNE_SYS_READ].call = dune_sys_read;
	dune_syscall_table[DUNE_SYS_READ].name = syscalls[DUNE_SYS_READ];
	if (dlopen("liblinuxemu_extend.so", RTLD_NOW) == NULL)
		fprintf(stderr, "Not using any syscall extensions\n Reason: %s\n",
		        dlerror());

}


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

	if (tf->tf_rax >= dune_max_syscall) {
		fprintf(stderr, "System call %d is out of range\n", tf->tf_rax);
		return false;
	}


	if (dune_syscall_table[tf->tf_rax].call == NULL) {
		fprintf(stderr, "System call %d is not implemented\n", tf->tf_rax);
		return false;
	}

	return (dune_syscall_table[tf->tf_rax].call)(tf);
}
