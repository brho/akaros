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

// TODO: put this some common place for user and kernel mode. Once
// we know we need to. Not sure we want to expose this outside
// vmrunkernel anyway. Users may claim they want to write a vmm, but ...
#define VMX_EXIT_REASONS_FAILED_VMENTRY         0x80000000

#define EXIT_REASON_EXCEPTION_NMI       0
#define EXIT_REASON_EXTERNAL_INTERRUPT  1
#define EXIT_REASON_TRIPLE_FAULT        2

#define EXIT_REASON_INTERRUPT_WINDOW    7
#define EXIT_REASON_NMI_WINDOW          8
#define EXIT_REASON_TASK_SWITCH         9
#define EXIT_REASON_CPUID               10
#define EXIT_REASON_HLT                 12
#define EXIT_REASON_INVD                13
#define EXIT_REASON_INVLPG              14
#define EXIT_REASON_RDPMC               15
#define EXIT_REASON_RDTSC               16
#define EXIT_REASON_VMCALL              18
#define EXIT_REASON_VMCLEAR             19
#define EXIT_REASON_VMLAUNCH            20
#define EXIT_REASON_VMPTRLD             21
#define EXIT_REASON_VMPTRST             22
#define EXIT_REASON_VMREAD              23
#define EXIT_REASON_VMRESUME            24
#define EXIT_REASON_VMWRITE             25
#define EXIT_REASON_VMOFF               26
#define EXIT_REASON_VMON                27
#define EXIT_REASON_CR_ACCESS           28
#define EXIT_REASON_DR_ACCESS           29
#define EXIT_REASON_IO_INSTRUCTION      30
#define EXIT_REASON_MSR_READ            31
#define EXIT_REASON_MSR_WRITE           32
#define EXIT_REASON_INVALID_STATE       33
#define EXIT_REASON_MWAIT_INSTRUCTION   36
#define EXIT_REASON_MONITOR_INSTRUCTION 39
#define EXIT_REASON_PAUSE_INSTRUCTION   40
#define EXIT_REASON_MCE_DURING_VMENTRY  41
#define EXIT_REASON_TPR_BELOW_THRESHOLD 43
#define EXIT_REASON_APIC_ACCESS         44
#define EXIT_REASON_EPT_VIOLATION       48
#define EXIT_REASON_EPT_MISCONFIG       49
#define EXIT_REASON_WBINVD              54
#define EXIT_REASON_XSETBV              55
#define EXIT_REASON_INVPCID             58

/* nowhere on my linux system. */
#define ARRAY_SIZE(x) (sizeof((x))/sizeof((x)[0]))

char *vmxexit[] = {
	[EXIT_REASON_EXCEPTION_NMI]        "EXCEPTION_NMI",
	[EXIT_REASON_EXTERNAL_INTERRUPT]   "EXTERNAL_INTERRUPT",
	[EXIT_REASON_TRIPLE_FAULT]         "TRIPLE_FAULT",
	[EXIT_REASON_INTERRUPT_WINDOW]    "INTERRUPT WINDOW (i.e. ok to send interrupts",
	[EXIT_REASON_NMI_WINDOW]           "NMI_WINDOW",
	[EXIT_REASON_TASK_SWITCH]          "TASK_SWITCH",
	[EXIT_REASON_CPUID]                "CPUID",
	[EXIT_REASON_HLT]                  "HLT",
	[EXIT_REASON_INVLPG]               "INVLPG",
	[EXIT_REASON_RDPMC]                "RDPMC",
	[EXIT_REASON_RDTSC]                "RDTSC",
	[EXIT_REASON_VMCALL]               "VMCALL",
	[EXIT_REASON_VMCLEAR]              "VMCLEAR",
	[EXIT_REASON_VMLAUNCH]             "VMLAUNCH",
	[EXIT_REASON_VMPTRLD]              "VMPTRLD",
	[EXIT_REASON_VMPTRST]              "VMPTRST",
	[EXIT_REASON_VMREAD]               "VMREAD",
	[EXIT_REASON_VMRESUME]             "VMRESUME",
	[EXIT_REASON_VMWRITE]              "VMWRITE",
	[EXIT_REASON_VMOFF]                "VMOFF",
	[EXIT_REASON_VMON]                 "VMON",
	[EXIT_REASON_CR_ACCESS]            "CR_ACCESS",
	[EXIT_REASON_DR_ACCESS]            "DR_ACCESS",
	[EXIT_REASON_IO_INSTRUCTION]       "IO_INSTRUCTION",
	[EXIT_REASON_MSR_READ]             "MSR_READ",
	[EXIT_REASON_MSR_WRITE]            "MSR_WRITE",
	[EXIT_REASON_MWAIT_INSTRUCTION]    "MWAIT_INSTRUCTION",
	[EXIT_REASON_MONITOR_INSTRUCTION]  "MONITOR_INSTRUCTION",
	[EXIT_REASON_PAUSE_INSTRUCTION]    "PAUSE_INSTRUCTION",
	[EXIT_REASON_MCE_DURING_VMENTRY]   "MCE_DURING_VMENTRY",
	[EXIT_REASON_TPR_BELOW_THRESHOLD]  "TPR_BELOW_THRESHOLD",
	[EXIT_REASON_APIC_ACCESS]          "APIC_ACCESS",
	[EXIT_REASON_EPT_VIOLATION]        "EPT_VIOLATION",
	[EXIT_REASON_EPT_MISCONFIG]        "EPT_MISCONFIG",
	[EXIT_REASON_WBINVD]               "WBINVD"
};

void showstatus(FILE *f, struct vmctl *v)
{
	int shutdown = v->ret_code;
	char *when = shutdown & VMX_EXIT_REASONS_FAILED_VMENTRY ? "entry" : "exit";
	shutdown &= 0xff;
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
