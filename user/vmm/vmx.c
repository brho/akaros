#include <stdio.h> 
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <parlib/arch/arch.h>
#include <parlib/ros_debug.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <ros/syscall.h>
#include <sys/mman.h>
#include <vmm/coreboot_tables.h>
#include <ros/common.h>
#include <vmm/vmm.h>
#include <vmm/virtio.h>
#include <vmm/virtio_mmio.h>
#include <vmm/virtio_ids.h>
#include <ros/arch/vmx.h>

/* nowhere on my linux system. */
#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))

char *vmxexit[] = {
	VMX_EXIT_REASONS
};

void showstatus(FILE *f, struct vmctl *v)
{
	int shutdown = v->ret_code;
	char *when = shutdown & VMX_EXIT_REASONS_FAILED_VMENTRY ? "entry" : "exit";
	shutdown &= ~VMX_EXIT_REASONS_FAILED_VMENTRY;
	char *reason = "UNKNOWN";
	if (shutdown < ARRAY_SIZE(vmxexit) && vmxexit[shutdown])
		reason = vmxexit[shutdown];
	fprintf(f, "Shutdown: core %d, %s due to %s(0x%x); ret code 0x%x\n", v->core, when, reason, shutdown, v->ret_code);
	fprintf(f, "  gva %p gpa %p cr3 %p\n", (void *)v->gva, (void *)v->gpa, (void *)v->cr3);

	fprintf(f, "  rax  0x%016lx\n",           v->regs.tf_rax);
	fprintf(f, "  rbx  0x%016lx\n",           v->regs.tf_rbx);
	fprintf(f, "  rcx  0x%016lx\n",           v->regs.tf_rcx);
	fprintf(f, "  rdx  0x%016lx\n",           v->regs.tf_rdx);
	fprintf(f, "  rbp  0x%016lx\n",           v->regs.tf_rbp);
	fprintf(f, "  rsi  0x%016lx\n",           v->regs.tf_rsi);
	fprintf(f, "  rdi  0x%016lx\n",           v->regs.tf_rdi);
	fprintf(f, "  r8   0x%016lx\n",           v->regs.tf_r8);
	fprintf(f, "  r9   0x%016lx\n",           v->regs.tf_r9);
	fprintf(f, "  r10  0x%016lx\n",           v->regs.tf_r10);
	fprintf(f, "  r11  0x%016lx\n",           v->regs.tf_r11);
	fprintf(f, "  r12  0x%016lx\n",           v->regs.tf_r12);
	fprintf(f, "  r13  0x%016lx\n",           v->regs.tf_r13);
	fprintf(f, "  r14  0x%016lx\n",           v->regs.tf_r14);
	fprintf(f, "  r15  0x%016lx\n",           v->regs.tf_r15);
}
