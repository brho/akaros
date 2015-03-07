#ifndef _VMM_H_
#define	_VMM_H_

static inline int cpu_has_vmx(void)
{
	unsigned long ecx = cpuid_ecx(1);
	return ecx & (1<<5); /* CPUID.1:ECX.VMX[bit 5] -> VT */
}

/* maybe someday, not today. */
static inline int cpu_has_svm(const char **msg)
{
	return 0;
}

struct vmm {
	qlock_t qlock;
	// always false.
	int amd;
	// true if this is a VMMCP.
	bool vmmcp;

	// Number of cores in this VMMCP.
	int nr_guest_pcores;

	// The VMCS is intel-specific. But, maybe, someday, AMD will
	// be back.  Just make this an anon union and we'll work it
	// all out later. Again, remember, we're compiling in support
	// for both architectures to ensure that we can correctly
	// figure out at boot time what we're on and what we should
	// do. This avoids the problem seen years ago with RH6 where
	// you could install a kernel from the ISO, but the kernel it
	// installed would GPF on a K7.
	union {
		void *svm;
		struct vmx_vcpu **guest_pcores;
	};
};

void vmm_init(void);
void vmm_pcpu_init(void);

int vmm_struct_init(struct vmm *vmm, unsigned int nr_guest_pcores);
void vmm_struct_cleanup(struct vmm *vmm);

int vm_run(uint64_t,uint64_t, uint64_t);
int intel_vmx_start(int id);
int intel_vmx_setup(int nvmcs);

struct vmx_vcpu *vmx_create_vcpu(void);
void vmx_destroy_vcpu(struct vmx_vcpu *vcpu);

#endif	/* _VMM_H_ */
