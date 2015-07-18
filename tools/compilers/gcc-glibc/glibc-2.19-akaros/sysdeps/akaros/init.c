#include <abort-instr.h>
#include <string.h>
#include <parlib/vcore.h>
#include <stdio.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>
#include <unistd.h>
#include <vcore-tls.c>
#include <ctype.h>

__thread int __vcoreid = 0;
__thread bool __vcore_context = FALSE;

#define failmsg(str) write(2,str"\n",sizeof(str"\n")-1)

void __attribute__((noreturn)) __uthread_vcore_entry(void)
{
	fputs("Define a uthread_vcore_entry() or a vcore_entry(), foo!\n", stderr);
	abort();
	__builtin_unreachable();
}
weak_alias(__uthread_vcore_entry, uthread_vcore_entry)

void __attribute__((noreturn)) __vcore_entry(void)
{
	uthread_vcore_entry();
	fputs("vcore_entry() should never return!\n", stderr);
	abort();
	__builtin_unreachable();
}
weak_alias(__vcore_entry, vcore_entry)

void __libc_vcore_entry(int id)
{
	static int init = 0;
	/* For dynamically-linked programs, the first time through,
	 * __vcore_self_on_entry could be clobbered (on x86), because the linker
	 * will have overwritten eax.  Happily, the first time through, we know we
	 * are vcore 0.  Subsequent entries into this routine do not have this
	 * problem. */
	if (init == 0)
		id = 0;

	/* vcore0 when it comes up again, and all threads besides thread 0 must
	 * acquire a TCB. */
	if (init || (id != 0)) {
		/* The kernel sets the TLS desc for us, based on whatever is in VCPD.
		 *
		 * x86 32-bit TLS is pretty jacked up, so the kernel doesn't set the TLS
		 * desc for us.  it's a little more expensive to do it here, esp for
		 * amd64.  Can remove this when/if we overhaul 32 bit TLS.
		 *
		 * AFAIK, riscv's TLS changes are really cheap, and they don't do it in
		 * the kernel (yet/ever), so they can set their TLS here too. */
		#ifndef __x86_64__
		set_tls_desc((void*)__procdata.vcore_preempt_data[id].vcore_tls_desc);
		#endif
		/* These TLS setters actually only need to happen once, at init time */
		__vcoreid = id;
		__vcore_context = TRUE;
		__ctype_init();	/* set locale info for ctype functions */
		vcore_entry();
		failmsg("why did vcore_entry() return?");
		abort();
		#ifdef ABORT_INSTRUCTION
		ABORT_INSTRUCTION;
		#endif

		while(1);
		__builtin_unreachable();
	}
	init = 1;
}

