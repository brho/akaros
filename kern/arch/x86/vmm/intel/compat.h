#ifndef __DUNE_COMPAT_H_
#define __DUNE_COMPAT_H_

#if !defined(VMX_EPT_AD_BIT)
#define VMX_EPT_AD_BIT          (1ull << 21)
#define VMX_EPT_AD_ENABLE_BIT   (1ull << 6)
#endif

#ifndef VMX_EPT_EXTENT_INDIVIDUAL_BIT
#define VMX_EPT_EXTENT_INDIVIDUAL_BIT           (1ull << 24)
#endif

#ifndef X86_CR4_PCIDE
#define X86_CR4_PCIDE		0x00020000 /* enable PCID support */
#endif

#ifndef SECONDARY_EXEC_ENABLE_INVPCID
#define SECONDARY_EXEC_ENABLE_INVPCID	0x00001000
#endif

/*
 * shutdown reasons
 */
enum shutdown_reason {
	SHUTDOWN_SYS_EXIT = 1,
	SHUTDOWN_SYS_EXIT_GROUP,
	SHUTDOWN_SYS_EXECVE,
	SHUTDOWN_FATAL_SIGNAL,
	SHUTDOWN_EPT_VIOLATION,
	SHUTDOWN_NMI_EXCEPTION,
	SHUTDOWN_UNHANDLED_EXIT_REASON,
};

#define SHUTDOWN_REASON(r)	((r) >> 16)
#define SHUTDOWN_STATUS(r)	((r) & 0xffff)

#endif /* __DUNE_COMPAT_H_ */
