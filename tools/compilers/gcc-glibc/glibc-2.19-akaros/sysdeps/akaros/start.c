#include <abort-instr.h>
#include <string.h>
#include <parlib/vcore.h>
#include <stdio.h>
#include <ros/syscall.h>
#include <ros/procinfo.h>
#include <unistd.h>
#include <vcore-tls.c>

__thread int __vcoreid = 0;
__thread bool __vcore_context = FALSE;

extern int main(int,char**,char**);
extern void __libc_csu_init(int,char**,char**);
extern void __libc_csu_fini(void);
extern void __libc_start_main(typeof(&main),int,char**,
              typeof(&__libc_csu_init),
              typeof(&__libc_csu_fini),
              void*,void*);

void __uthread_vcore_entry(void)
{
	fputs("Define a uthread_vcore_entry() or a vcore_entry(), foo!\n", stderr);
	abort();
}
weak_alias(__uthread_vcore_entry, uthread_vcore_entry)

void __vcore_entry(void)
{
	uthread_vcore_entry();
}
weak_alias(__vcore_entry, vcore_entry)

void __vcore_event_init(void)
{
	fputs("Build your application with -lparlib\n", stderr);
	abort();
}
weak_alias(__vcore_event_init, vcore_event_init)

#define failmsg(str) write(2,str"\n",sizeof(str"\n")-1)

void __ros_libc_csu_init(int argc, char **argv, char **envp)
{
	vcore_event_init();
	// Note that we want the ctors to be called after vcore_event_init.
	// (They are invoked by the next line.)
	__libc_csu_init(argc, argv, envp);
}

void
_start(void)
{
	// WARNING: __vcore_self_on_entry must be read before
	// anything is register-allocated!
	int id = __vcore_id_on_entry;
	static int init = 0;
	// For dynamically-linked programs, the first time through,
	// __vcore_self_on_entry could be clobbered (on x86), because
	// the linker will have overwritten eax.  Happily, the first
	// time through, we know we are vcore 0.  Subsequent entries
	// into this routine do not have this problem.
	if(init == 0)
		id = 0;
	
	// vcore0 when it comes up again, and all threads besides thread 0 must
	// acquire a TCB.
	if(init || (id != 0))
	{
		/* The kernel sets the TLS desc for us, based on whatever is in VCPD.
		 *
		 * x86 32-bit TLS is pretty jacked up, so the kernel doesn't set the TLS
		 * desc for us.  it's a little more expensive to do it here, esp for
		 * amd64.  Can remove this when/if we overhaul 32 bit TLS.
		 *
		 * AFAIK, riscv's TLS changes are really cheap, and they don't do it in
		 * the kernel (yet/ever), so they can set their TLS here too. */
		#ifndef __x86_64__
		set_tls_desc((void*)__procdata.vcore_preempt_data[id].vcore_tls_desc,
		             id);
		#endif
		/* These TLS setters actually only need to happen once, at init time */
		__vcoreid = id;
		__vcore_context = TRUE;
		__ctype_init();	/* set locale info for ctype functions */
		vcore_entry();
		failmsg("why did vcore_entry() return?");
		goto diediedie;
	}

	init = 1;

	char** argv = (char**)alloca(sizeof(__procinfo.argp));
	memcpy(argv,__procinfo.argp,sizeof(__procinfo.argp));

	char* argbuf = (char*)alloca(sizeof(__procinfo.argbuf));
	memcpy(argbuf,__procinfo.argbuf,sizeof(__procinfo.argbuf));

	// touch up pointers, but don't mess up auxp!
	for(int i = 0, zeros = 0; i < PROCINFO_MAX_ARGP; i++)
	{
		if(argv[i])
			argv[i] += argbuf - __procinfo.argbuf;
		else if(++zeros == 2)
			break;
	}

	int argc = 0;
	while(argv[argc])
		argc++;

	extern char** _environ;
	_environ = argv+argc+1;

	__libc_start_main(&main, argc, argv, &__ros_libc_csu_init, &__libc_csu_fini,
	                  0, 0);

	failmsg("why did main() return?");

diediedie:
	abort();
	#ifdef ABORT_INSTRUCTION
	ABORT_INSTRUCTION;
	#endif
	while(1);
}
